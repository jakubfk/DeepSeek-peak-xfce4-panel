# DeepSeek XFCE Indicator

Kropka w panelu Xfce sygnalizująca godziny szczytu (peak) API DeepSeek.

- **Poniedziałek – piątek**
- **01:00 – 04:00 UTC** (02:00 – 05:00 czasu polskiego zimą)
- **06:00 – 10:00 UTC** (07:00 – 11:00 czasu polskiego zimą)

Kolory:

| kropka | znaczenie |
|--------|-----------|
| zielona | pozaszczyt |
| pomarańczowa | pozaszczyt, ale szczyt zaczyna się za mniej niż 30 min |
| czerwona | szczyt |

Tooltip pokazuje aktualny stan, godzinę UTC, czas do najbliższej zmiany
oraz godzinę i datę tej zmiany, np.:

```
DeepSeek: pozaszczyt, szczyt wkrótce (00:47 UTC)
Zmiana za 13 min → szczyt od 01:00 UTC
```

Wszystkie pozostałe godziny, weekendy oraz daty z
`~/.config/xfce4/deepseek-indicator/holidays.txt` są traktowane jako
off-peak. Prawy klik: cennik DeepSeek, odświeżenie, „O programie”.

## Dlaczego wcześniejsza wersja nie działała

Panel Xfce (4.16+) **nie obsługuje już `X-XFCE-Exec`** — to mechanizm
zewnętrznych wtyczek GtkPlug/GtkSocket z Xfce 4.4–4.12. Współczesny panel
czyta wyłącznie `X-XFCE-Module` i ładuje moduł `.so` przez `GModule`,
szukając symbolu `xfce_panel_module_init` lub `xfce_panel_module_construct`.
Wpis `.desktop` bez `X-XFCE-Module` jest pomijany **cicho** — dlatego wtyczka
nigdy nie pojawiała się na liście „Dodaj nowe elementy”, niezależnie od
liczby restartów panelu. Skryptu w Pythonie nie da się też załadować jako
moduł panelu: PyGObject nie eksportuje wymaganych symboli C.

Ta wersja to prawdziwy moduł panelu w C (`deepseek-indicator.c`),
rejestrowany przez `XFCE_PANEL_PLUGIN_REGISTER()` i ładowany symbolem
`xfce_panel_module_construct`. Wymaga panelu **4.16+** (API 2.0/GTK3);
starsze 4.14 odrzuci wpis z komunikatem o API 1.0. `X-XFCE-Internal=false`
znaczy „uruchom moduł w osobnym procesie (wrapper)” — to normalne,
panel sam nim zarządza.

## Wymagania

Debian/Ubuntu:

```bash
sudo apt install build-essential pkg-config libgtk-3-dev libxfce4panel-2.0-dev libxfce4ui-2-dev
```

`libxfce4ui-2-dev` jest konieczne: `libxfce4panel-2.0.pc` ma
`Requires.private: libxfce4ui-2`, ale pakiet `libxfce4panel-2.0-dev`
w Debianie **nie** ciągnie `libxfce4ui-2-dev`, więc bez niego
`pkg-config --exists libxfce4panel-2.0` zgłasza brak pakietu.

## Instalacja

```bash
chmod +x install.sh
./install.sh            # tylko dla bieżącego użytkownika (~/.local)
./install.sh --system   # systemowo (/usr, wymaga sudo)
```

Skrypt kompiluje moduł i instaluje:

| tryb | moduł | plik .desktop |
|------|-------|---------------|
| user | `~/.local/lib/<multiarch>/xfce4/panel/plugins/libdeepseek-indicator.so` | `~/.local/share/xfce4/panel/plugins/deepseek-indicator.desktop` |
| system | `/usr/lib/<multiarch>/xfce4/panel/plugins/libdeepseek-indicator.so` | `/usr/share/xfce4/panel/plugins/deepseek-indicator.desktop` |

Następnie restartuje panel (`xfce4-panel -r`). Dodanie wtyczki:

**prawy klik na panel → Panel → Dodaj nowe elementy → „DeepSeek Indicator”**.

## Weryfikacja

```bash
xfce4-panel --version
ls -l ~/.local/lib/*/xfce4/panel/plugins/libdeepseek-indicator.so
PANEL_DEBUG=module-factory xfce4-panel -r 2>&1 | grep deepseek
```

W debugu powinno pojawić się
`new module deepseek-indicator, filename=..., internal=false`.

## Testy logiki

```bash
cd plugin && make test
```

15 przypadków: granice obu okien peak, weekendy, progi ostrzeżenia 29/30/31 min,
święta (także łańcuch święto + weekend) oraz wyliczony czas do zmiany.

## Święta / dni wolne

Plik `~/.config/xfce4/deepseek-indicator/holidays.txt` tworzony przy pierwszym
uruchomieniu. Jedna data `YYYY-MM-DD` na linię, `#` = komentarz. Zmiany
działają bez restartu panelu (odczyt co 30 s).

## Odinstalowanie

```bash
./install.sh --uninstall
```

## Struktura

```
plugin/deepseek-indicator.c          # kod wtyczki (Gtk.DrawingArea + logika peak)
plugin/deepseek-indicator.desktop    # wpis X-XFCE-Module=deepseek-indicator
plugin/Makefile                      # build + make test
install.sh                           # build + instalacja user/system/uninstall
```
