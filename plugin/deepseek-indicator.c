/*
 * deepseek-indicator.c -- Xfce4 panel plugin: DeepSeek API peak-hours dot.
 *
 * Build as a loadable panel module (libdeepseek-indicator.so).
 * Registered via XFCE_PANEL_PLUGIN_REGISTER(), i.e. the panel calls
 * xfce_panel_module_construct() from <libxfce4panel/libxfce4panel.h>.
 *
 * Colors:
 *   green  -- off-peak
 *   orange -- off-peak, but peak starts in less than WARN_MINUTES
 *   red    -- peak
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtk.h>
#include <libxfce4panel/libxfce4panel.h>

#include <string.h>

#define REFRESH_SECONDS   30
#define WARN_MINUTES      30        /* green -> orange threshold */
#define MAX_SCAN_MINUTES  (14 * 24 * 60)

#define PLUGIN_CONFIG_DIR "deepseek-indicator"
#define HOLIDAYS_FILE      "holidays.txt"

/* 1970-01-01 in GDate julian-day numbering (0001-01-01 == 1, not JDN) */
#define UNIX_EPOCH_JULIAN 719163

typedef enum
{
  DOT_OFFPEAK,
  DOT_SOON,
  DOT_PEAK
} DotState;

/* ------------------------------------------------------------------ */
/* translation: no gettext, the wrapper inherits the user's locale     */
/* ------------------------------------------------------------------ */

enum
{
  T_PRICING,
  T_REFRESH,
  T_ABOUT,
  T_STATE_PEAK,
  T_STATE_SOON,
  T_STATE_OFFPEAK,
  T_NEXT_PEAK,
  T_NEXT_OFFPEAK,
  T_CHANGE_LINE,
  T_ABOUT_COMMENT,
  T_COUNT
};

static const struct
{
  const gchar *pl;
  const gchar *en;
} translations[T_COUNT] =
{
  [T_PRICING]       = { "Otwórz cennik DeepSeek", "Open DeepSeek pricing" },
  [T_REFRESH]       = { "Odśwież", "Refresh" },
  [T_ABOUT]         = { "O programie", "About" },
  [T_STATE_PEAK]    = { "godziny szczytu", "peak hours" },
  [T_STATE_SOON]    = { "pozaszczyt, szczyt wkrótce", "off-peak, peak starting soon" },
  [T_STATE_OFFPEAK] = { "godziny pozaszczytowe", "off-peak hours" },
  [T_NEXT_PEAK]     = { "szczyt", "peak" },
  [T_NEXT_OFFPEAK]  = { "pozaszczyt", "off-peak" },
  [T_CHANGE_LINE]   = { "Zmiana za %s → %s od %s", "Change in %s → %s from %s" },
  [T_ABOUT_COMMENT] =
      { "Wskaźnik godzin szczytu DeepSeek API (UTC)\n"
        "Pon-pt: 01:00-04:00 i 06:00-10:00 UTC\n"
        "Zielona = pozaszczyt, pomarańczowa = szczyt w ciągu 30 min,\n"
        "czerwona = szczyt",
        "DeepSeek API peak-hours indicator (UTC)\n"
        "Mon-Fri: 01:00-04:00 and 06:00-10:00 UTC\n"
        "Green = off-peak, orange = peak starts within 30 min,\n"
        "red = peak" },
};

static gboolean g_polish = TRUE;

/* Polish only when the user's locale is Polish, otherwise English.
 * g_get_language_names() honours LANGUAGE/LC_ALL/LC_MESSAGES/LANG. */
static gboolean
locale_is_polish (void)
{
  const gchar * const *names = g_get_language_names ();

  return names != NULL && names[0] != NULL
      && g_str_has_prefix (names[0], "pl");
}

static const gchar *
tr (guint id)
{
  g_return_val_if_fail (id < T_COUNT, "");

  return g_polish ? translations[id].pl : translations[id].en;
}

typedef struct
{
  XfcePanelPlugin *plugin;
  GtkWidget       *area;
  GtkWidget       *menu;
  DotState         state;
  guint            timeout_id;
} DeepSeekPlugin;

/* ------------------------------------------------------------------ */
/* peak-hour logic                                                     */
/* ------------------------------------------------------------------ */

static gchar *
holidays_path (void)
{
  return g_build_filename (g_get_user_config_dir (), "xfce4",
                           PLUGIN_CONFIG_DIR, HOLIDAYS_FILE, NULL);
}

/* Set of julian days parsed from holidays.txt. Re-read on every refresh,
 * so edits apply without restarting the panel. */
static GHashTable *
holidays_load (void)
{
  GHashTable *set = g_hash_table_new (NULL, NULL);
  gchar      *path = holidays_path ();
  gchar      *contents = NULL;

  if (!g_file_test (path, G_FILE_TEST_EXISTS))
    {
      gchar *dir = g_path_get_dirname (path);
      g_mkdir_with_parents (dir, 0755);
      g_free (dir);

      g_file_set_contents (path,
                           "# DeepSeek: dni wolne (YYYY-MM-DD), jedna data na linię\n"
                           "# 2025-01-29\n",
                           -1, NULL);
    }
  else if (g_file_get_contents (path, &contents, NULL, NULL))
    {
      gchar **lines = g_strsplit (contents, "\n", -1);
      gchar **line;

      for (line = lines; *line != NULL; line++)
        {
          gchar *entry = g_strstrip (*line);
          gint   y, m, d;
          GDate  date;

          if (*entry == '\0' || *entry == '#')
            continue;

          if (sscanf (entry, "%d-%d-%d", &y, &m, &d) != 3)
            continue;

          g_date_clear (&date, 1);
          g_date_set_dmy (&date, (GDateDay) d, (GDateMonth) m, (GDateYear) y);
          if (g_date_valid (&date))
            g_hash_table_add (set, GUINT_TO_POINTER (g_date_get_julian (&date)));
        }

      g_strfreev (lines);
      g_free (contents);
    }

  g_free (path);
  return set;
}

static gboolean
is_holiday_day (gint64      days_since_epoch,
                GHashTable *holidays)
{
  GDate date;

  if (holidays == NULL || g_hash_table_size (holidays) == 0)
    return FALSE;

  g_date_clear (&date, 1);
  g_date_set_julian (&date, (guint32) (UNIX_EPOCH_JULIAN + days_since_epoch));
  return g_hash_table_contains (holidays, GUINT_TO_POINTER (g_date_get_julian (&date)));
}

/* Peak: Mon-Fri, 01:00-04:00 and 06:00-10:00 UTC, excluding holidays.
 * Works on a unix timestamp so the "next change" scan needs no allocations. */
static gboolean
is_peak_unix (gint64      t,
              GHashTable *holidays)
{
  gint64 days = t / 86400;
  gint   weekday;
  gint   minutes;

  weekday = (gint) ((days + 3) % 7);   /* 0 = Monday ... 6 = Sunday */
  if (weekday >= 5)
    return FALSE;

  if (is_holiday_day (days, holidays))
    return FALSE;

  minutes = (gint) ((t - days * 86400) / 60);
  return (minutes >= 60  && minutes < 240)    /* 01:00-04:00 */
      || (minutes >= 360 && minutes < 600);   /* 06:00-10:00 */
}

static gboolean
is_peak_now (GDateTime  *now_utc,
             GHashTable *holidays)
{
  return is_peak_unix (g_date_time_to_unix (now_utc), holidays);
}

/* Minutes until the dot flips color (0 if nothing found in MAX_SCAN_MINUTES). */
static guint
minutes_until_change (gint64      now_unix,
                      gboolean    now_peak,
                      GHashTable *holidays)
{
  guint m;

  for (m = 1; m <= MAX_SCAN_MINUTES; m++)
    {
      if (is_peak_unix (now_unix + (gint64) m * 60, holidays) != now_peak)
        return m;
    }

  return 0;
}

static DotState
dot_state_for (gboolean peak,
               guint    minutes_left)
{
  if (peak)
    return DOT_PEAK;

  /* orange only when a green dot is about to turn red */
  if (minutes_left > 0 && minutes_left < WARN_MINUTES)
    return DOT_SOON;

  return DOT_OFFPEAK;
}

/* ------------------------------------------------------------------ */
/* widget code                                                         */
/* ------------------------------------------------------------------ */

static gboolean
on_draw (GtkWidget *widget,
         cairo_t   *cr,
         gpointer   user_data)
{
  DeepSeekPlugin *ds = user_data;
  GtkAllocation   alloc;
  gdouble         cx, cy;

  gtk_widget_get_allocation (widget, &alloc);
  cx = alloc.width / 2.0;
  cy = alloc.height / 2.0;

  switch (ds->state)
    {
    case DOT_PEAK:
      cairo_set_source_rgb (cr, 0.90, 0.10, 0.10);   /* red */
      break;
    case DOT_SOON:
      cairo_set_source_rgb (cr, 1.00, 0.65, 0.00);   /* orange */
      break;
    case DOT_OFFPEAK:
    default:
      cairo_set_source_rgb (cr, 0.10, 0.80, 0.10);   /* green */
      break;
    }

  cairo_arc (cr, cx, cy, 5.0, 0.0, 2.0 * G_PI);
  cairo_fill (cr);

  return FALSE;
}

static gchar *
format_duration (guint minutes)
{
  guint days  = minutes / (24 * 60);
  guint hours = (minutes % (24 * 60)) / 60;
  guint mins  = minutes % 60;

  if (days > 0)
    return g_strdup_printf ("%u d %u h %u min", days, hours, mins);
  if (hours > 0)
    return g_strdup_printf ("%u h %u min", hours, mins);

  return g_strdup_printf ("%u min", mins);
}

static gboolean
deepseek_update (gpointer user_data)
{
  DeepSeekPlugin *ds = user_data;
  GDateTime      *now;
  GDateTime      *change_at;
  GHashTable     *holidays;
  gboolean        peak;
  guint           minutes_left;
  gchar          *now_stamp;
  gchar          *change_stamp;
  gchar          *duration;
  gchar          *line;
  gchar          *tip;
  const gchar    *state_text;
  const gchar    *next_text;

  now      = g_date_time_new_now_utc ();
  holidays = holidays_load ();
  peak     = is_peak_now (now, holidays);

  minutes_left = minutes_until_change (g_date_time_to_unix (now), peak, holidays);

  ds->state = dot_state_for (peak, minutes_left);
  gtk_widget_queue_draw (ds->area);

  now_stamp = g_date_time_format (now, "%H:%M UTC");

  switch (ds->state)
    {
    case DOT_PEAK:    state_text = tr (T_STATE_PEAK); break;
    case DOT_SOON:    state_text = tr (T_STATE_SOON); break;
    case DOT_OFFPEAK:
    default:          state_text = tr (T_STATE_OFFPEAK); break;
    }

  if (minutes_left > 0)
    {
      change_at    = g_date_time_add_minutes (now, (gint) minutes_left);
      change_stamp = g_date_time_format (change_at, "%d.%m %H:%M UTC");
      duration     = format_duration (minutes_left);
      next_text    = peak ? tr (T_NEXT_OFFPEAK) : tr (T_NEXT_PEAK);

      line = g_strdup_printf (tr (T_CHANGE_LINE),
                              duration, next_text, change_stamp);
      tip  = g_strdup_printf ("DeepSeek: %s (%s)\n%s",
                              state_text, now_stamp, line);

      g_free (line);
      g_free (change_stamp);
      g_free (duration);
      g_date_time_unref (change_at);
    }
  else
    {
      tip = g_strdup_printf ("DeepSeek: %s (%s)", state_text, now_stamp);
    }

  gtk_widget_set_tooltip_text (GTK_WIDGET (ds->plugin), tip);

  g_free (tip);
  g_free (now_stamp);
  g_hash_table_destroy (holidays);
  g_date_time_unref (now);

  return G_SOURCE_CONTINUE;
}

static void
on_open_pricing (GtkMenuItem *item,
                 gpointer     user_data)
{
  (void) item;
  (void) user_data;

  g_app_info_launch_default_for_uri ("https://api-docs.deepseek.com/quick_start/pricing",
                                     NULL, NULL);
}

static void
on_refresh (GtkMenuItem *item,
            gpointer     user_data)
{
  (void) item;

  deepseek_update (user_data);
}

static void
on_about (GtkMenuItem *item,
          gpointer     user_data)
{
  DeepSeekPlugin *ds = user_data;
  GtkWidget      *dialog;
  GtkWidget      *toplevel;

  (void) item;

  dialog = gtk_about_dialog_new ();
  gtk_about_dialog_set_program_name (GTK_ABOUT_DIALOG (dialog), "DeepSeek Status Indicator");
  gtk_about_dialog_set_version (GTK_ABOUT_DIALOG (dialog), "2.2");
  gtk_about_dialog_set_comments (GTK_ABOUT_DIALOG (dialog), tr (T_ABOUT_COMMENT));

  toplevel = gtk_widget_get_toplevel (GTK_WIDGET (ds->plugin));
  if (GTK_IS_WINDOW (toplevel))
    gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (toplevel));

  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
}

static gboolean
on_button_press (GtkWidget      *widget,
                 GdkEventButton *event,
                 gpointer        user_data)
{
  DeepSeekPlugin *ds = user_data;

  if (event->button != 3)
    return FALSE;

  (void) widget;
  gtk_menu_popup_at_pointer (GTK_MENU (ds->menu), (GdkEvent *) event);
  return TRUE;
}

static void
deepseek_free (XfcePanelPlugin *plugin,
               gpointer         user_data)
{
  DeepSeekPlugin *ds = user_data;

  (void) plugin;

  if (ds->timeout_id != 0)
    {
      g_source_remove (ds->timeout_id);
      ds->timeout_id = 0;
    }

  g_free (ds);
}

static void
deepseek_construct (XfcePanelPlugin *plugin)
{
  DeepSeekPlugin *ds;
  GtkWidget      *item;

  g_polish = locale_is_polish ();

  ds = g_new0 (DeepSeekPlugin, 1);
  ds->plugin = plugin;

  ds->area = gtk_drawing_area_new ();
  gtk_widget_set_size_request (ds->area, 12, 12);
  gtk_widget_add_events (ds->area, GDK_BUTTON_PRESS_MASK);

  g_signal_connect (ds->area, "draw", G_CALLBACK (on_draw), ds);
  g_signal_connect (ds->area, "button-press-event", G_CALLBACK (on_button_press), ds);

  gtk_container_add (GTK_CONTAINER (plugin), ds->area);
  xfce_panel_plugin_set_small (plugin, TRUE);

  ds->menu = gtk_menu_new ();

  item = gtk_menu_item_new_with_label (tr (T_PRICING));
  g_signal_connect (item, "activate", G_CALLBACK (on_open_pricing), ds);
  gtk_menu_shell_append (GTK_MENU_SHELL (ds->menu), item);

  item = gtk_menu_item_new_with_label (tr (T_REFRESH));
  g_signal_connect (item, "activate", G_CALLBACK (on_refresh), ds);
  gtk_menu_shell_append (GTK_MENU_SHELL (ds->menu), item);

  gtk_menu_shell_append (GTK_MENU_SHELL (ds->menu), gtk_separator_menu_item_new ());

  item = gtk_menu_item_new_with_label (tr (T_ABOUT));
  g_signal_connect (item, "activate", G_CALLBACK (on_about), ds);
  gtk_menu_shell_append (GTK_MENU_SHELL (ds->menu), item);

  gtk_widget_show_all (ds->menu);
  gtk_widget_show_all (GTK_WIDGET (plugin));

  ds->timeout_id = g_timeout_add_seconds (REFRESH_SECONDS, deepseek_update, ds);
  deepseek_update (ds);

  g_signal_connect (plugin, "free-data", G_CALLBACK (deepseek_free), ds);
}

/* register the plugin with the panel */
XFCE_PANEL_PLUGIN_REGISTER (deepseek_construct)

/* ------------------------------------------------------------------ */
/* optional logic self-test: make test                                 */
/* ------------------------------------------------------------------ */
#ifdef DEEPSEEK_TEST
static GHashTable *
test_holidays (void)
{
  GHashTable *set = g_hash_table_new (NULL, NULL);
  GDate       date;

  /* 2025-01-29 (Wednesday) */
  g_date_clear (&date, 1);
  g_date_set_dmy (&date, 29, 1, 2025);
  g_hash_table_add (set, GUINT_TO_POINTER (g_date_get_julian (&date)));

  return set;
}

static int
check (GHashTable  *holidays,
       gint         year,
       gint         month,
       gint         day,
       gint         hour,
       gint         minute,
       gboolean     expect_peak,
       guint        expect_left,
       DotState     expect_state,
       const gchar *label)
{
  GDateTime *dt;
  gboolean   peak;
  guint      left;
  DotState   state;
  gint64     t;
  int        ok;

  dt    = g_date_time_new_utc (year, month, day, hour, minute, 0);
  t     = g_date_time_to_unix (dt);
  peak  = is_peak_now (dt, holidays);
  left  = minutes_until_change (t, peak, holidays);
  state = dot_state_for (peak, left);

  ok = (peak == expect_peak) && (left == expect_left) && (state == expect_state);

  g_print ("%s %04d-%02d-%02d %02d:%02d UTC -> %-7s / change in %4u min / %s: %s\n",
           label, year, month, day, hour, minute,
           peak ? "peak" : "off-peak", left,
           state == DOT_PEAK ? "red" : (state == DOT_SOON ? "orange" : "green"),
           ok ? "OK" : "FAIL");

  if (!ok)
    g_print ("  expected:  %-7s / change in %4u min / %s\n",
             expect_peak ? "peak" : "off-peak", expect_left,
             expect_state == DOT_PEAK ? "red"
               : (expect_state == DOT_SOON ? "orange" : "green"));

  g_date_time_unref (dt);
  return ok;
}

int
main (void)
{
  GHashTable *h = test_holidays ();
  int passed = 0;
  int total  = 0;
  gchar *path;

  g_print ("Detected language: %s\n", locale_is_polish () ? "pl" : "en(+other)");

  /* peak windows and weekends */
  total++; passed += check (h, 2025, 1, 20,  0, 59, FALSE,   1, DOT_SOON,   "Mon before peak 1");
  total++; passed += check (h, 2025, 1, 20,  1,  0, TRUE,  180, DOT_PEAK,   "Mon peak 1 start");
  total++; passed += check (h, 2025, 1, 20,  3, 59, TRUE,    1, DOT_PEAK,   "Mon peak 1 end");
  total++; passed += check (h, 2025, 1, 20,  4,  0, FALSE, 120, DOT_OFFPEAK, "Mon after peak 1");
  total++; passed += check (h, 2025, 1, 20,  6,  0, TRUE,  240, DOT_PEAK,   "Mon peak 2 start");
  total++; passed += check (h, 2025, 1, 20,  9, 45, TRUE,   15, DOT_PEAK,   "Mon 15 min before peak end");
  total++; passed += check (h, 2025, 1, 20, 10,  0, FALSE, 900, DOT_OFFPEAK, "Mon after peak 2");
  total++; passed += check (h, 2025, 1, 25,  1,  0, FALSE, 2880, DOT_OFFPEAK, "Saturday");
  total++; passed += check (h, 2025, 1, 26,  7,  0, FALSE, 1080, DOT_OFFPEAK, "Sunday");

  /* orange warning window: only for green -> red */
  total++; passed += check (h, 2025, 1, 20,  0, 30, FALSE,  30, DOT_OFFPEAK, "30 min to change (no warning)");
  total++; passed += check (h, 2025, 1, 20,  0, 31, FALSE,  29, DOT_SOON,   "29 min to change (warning)");
  total++; passed += check (h, 2025, 1, 20,  0, 29, FALSE,  31, DOT_OFFPEAK, "31 min to change");

  /* holidays */
  total++; passed += check (h, 2025, 1, 29,  2,  0, FALSE, 1380, DOT_OFFPEAK, "holiday inside peak");
  total++; passed += check (h, 2025, 1, 28, 23, 59, FALSE, 1501, DOT_OFFPEAK, "day before holiday");

  /* long off-peak stretch: Fri 10:00 -> Mon 01:00 */
  total++; passed += check (h, 2025, 1, 24, 10,  0, FALSE, 3780, DOT_OFFPEAK, "Friday after peak 2");

  /* every string must exist in both languages */
  {
    guint i;

    for (i = 0; i < T_COUNT; i++)
      {
        gboolean ok = translations[i].pl != NULL && translations[i].en != NULL
                   && *translations[i].pl != '\0' && *translations[i].en != '\0';

        total++;
        if (ok)
          passed++;
        else
          g_print ("FAIL: missing translation for id %u\n", i);
      }
  }

  path = holidays_path ();
  g_print ("\nHolidays file: %s\n", path);
  g_free (path);

  g_hash_table_destroy (h);

  g_print ("Passed: %d/%d\n", passed, total);
  return passed == total ? 0 : 1;
}
#endif /* DEEPSEEK_TEST */
