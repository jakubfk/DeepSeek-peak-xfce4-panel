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
    case DOT_PEAK:    state_text = "godziny szczytu"; break;
    case DOT_SOON:    state_text = "pozaszczyt, szczyt wkrótce"; break;
    case DOT_OFFPEAK:
    default:          state_text = "godziny pozaszczytowe"; break;
    }

  if (minutes_left > 0)
    {
      change_at    = g_date_time_add_minutes (now, (gint) minutes_left);
      change_stamp = g_date_time_format (change_at, "%d.%m %H:%M UTC");
      duration     = format_duration (minutes_left);
      next_text    = peak ? "pozaszczyt" : "szczyt";

      tip = g_strdup_printf ("DeepSeek: %s (%s)\n"
                             "Zmiana za %s → %s od %s",
                             state_text, now_stamp,
                             duration, next_text, change_stamp);

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
  gtk_about_dialog_set_version (GTK_ABOUT_DIALOG (dialog), "2.1");
  gtk_about_dialog_set_comments (GTK_ABOUT_DIALOG (dialog),
                                 "Wskaźnik godzin szczytu DeepSeek API (UTC)\n"
                                 "Pon-pt: 01:00-04:00 i 06:00-10:00 UTC\n"
                                 "Zielona = pozaszczyt, pomarańczowa = szczyt w ciągu 30 min,\n"
                                 "czerwona = szczyt");

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

  item = gtk_menu_item_new_with_label ("Otwórz cennik DeepSeek");
  g_signal_connect (item, "activate", G_CALLBACK (on_open_pricing), ds);
  gtk_menu_shell_append (GTK_MENU_SHELL (ds->menu), item);

  item = gtk_menu_item_new_with_label ("Odśwież");
  g_signal_connect (item, "activate", G_CALLBACK (on_refresh), ds);
  gtk_menu_shell_append (GTK_MENU_SHELL (ds->menu), item);

  gtk_menu_shell_append (GTK_MENU_SHELL (ds->menu), gtk_separator_menu_item_new ());

  item = gtk_menu_item_new_with_label ("O programie");
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

  g_print ("%s %04d-%02d-%02d %02d:%02d UTC -> %-7s / zmiana za %4u min / %s: %s\n",
           label, year, month, day, hour, minute,
           peak ? "peak" : "off-peak", left,
           state == DOT_PEAK ? "red" : (state == DOT_SOON ? "orange" : "green"),
           ok ? "OK" : "FAIL");

  if (!ok)
    g_print ("  oczekiwano: %-7s / zmiana za %4u min / %s\n",
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

  /* peak windows and weekends */
  total++; passed += check (h, 2025, 1, 20,  0, 59, FALSE,   1, DOT_SOON,   "pon przed 1. oknem");
  total++; passed += check (h, 2025, 1, 20,  1,  0, TRUE,  180, DOT_PEAK,   "pon start 1. okna");
  total++; passed += check (h, 2025, 1, 20,  3, 59, TRUE,    1, DOT_PEAK,   "pon koniec 1. okna");
  total++; passed += check (h, 2025, 1, 20,  4,  0, FALSE, 120, DOT_OFFPEAK, "pon po 1. oknie");
  total++; passed += check (h, 2025, 1, 20,  6,  0, TRUE,  240, DOT_PEAK,   "pon start 2. okna");
  total++; passed += check (h, 2025, 1, 20,  9, 45, TRUE,   15, DOT_PEAK,   "pon 15 min do końca szczytu");
  total++; passed += check (h, 2025, 1, 20, 10,  0, FALSE, 900, DOT_OFFPEAK, "pon po 2. oknie");
  total++; passed += check (h, 2025, 1, 25,  1,  0, FALSE, 2880, DOT_OFFPEAK, "sobota");
  total++; passed += check (h, 2025, 1, 26,  7,  0, FALSE, 1080, DOT_OFFPEAK, "niedziela");

  /* orange warning window: only for green -> red */
  total++; passed += check (h, 2025, 1, 20,  0, 30, FALSE,  30, DOT_OFFPEAK, "30 min do zmiany (bez ostrzeżenia)");
  total++; passed += check (h, 2025, 1, 20,  0, 31, FALSE,  29, DOT_SOON,   "29 min do zmiany (ostrzeżenie)");
  total++; passed += check (h, 2025, 1, 20,  0, 29, FALSE,  31, DOT_OFFPEAK, "31 min do zmiany");

  /* holidays */
  total++; passed += check (h, 2025, 1, 29,  2,  0, FALSE, 1380, DOT_OFFPEAK, "święto w środku szczytu");
  total++; passed += check (h, 2025, 1, 28, 23, 59, FALSE, 1501, DOT_OFFPEAK, "dzień przed świętem");

  /* long off-peak stretch: Fri 10:00 -> Mon 01:00 */
  total++; passed += check (h, 2025, 1, 24, 10,  0, FALSE, 3780, DOT_OFFPEAK, "piątek po 2. oknie");

  path = holidays_path ();
  g_print ("\nPlik świąt: %s\n", path);
  g_free (path);

  g_hash_table_destroy (h);

  g_print ("Passed: %d/%d\n", passed, total);
  return passed == total ? 0 : 1;
}
#endif /* DEEPSEEK_TEST */
