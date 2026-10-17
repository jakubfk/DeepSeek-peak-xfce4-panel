# DeepSeek XFCE Indicator

A panel dot showing DeepSeek API peak hours.
**English** · [Polski](#polski)

---

## English

### Peak hours

- **Monday – Friday**
- **01:00 – 04:00 UTC**
- **06:00 – 10:00 UTC**

| dot | meaning |
|-----|---------|
| green | off-peak |
| orange | off-peak, but peak starts in less than 30 min |
| red | peak |

Hovering the dot shows the current state, the UTC time, and how long until the
next change:

```
DeepSeek: off-peak, peak starting soon (00:47 UTC)
Change in 13 min → peak from 01:00 UTC
```

Weekends and dates listed in `~/.config/xfce4/deepseek-indicator/holidays.txt`
count as off-peak. Right click: DeepSeek pricing, refresh, About.

### Language

The UI follows the system locale: Polish only when the locale starts with `pl`
(`g_get_language_names()`, i.e. `LANGUAGE`/`LC_ALL`/`LC_MESSAGES`/`LANG`);
English otherwise. The plugin's `.desktop` entry ships English + Polish names.


### Requirements

Debian/Ubuntu:

```bash
sudo apt install build-essential pkg-config libgtk-3-dev libxfce4panel-2.0-dev libxfce4ui-2-dev
```

`libxfce4ui-2-dev` is required: `libxfce4panel-2.0.pc` has
`Requires.private: libxfce4ui-2`, but the Debian `libxfce4panel-2.0-dev`
package **does not** pull `libxfce4ui-2-dev`, so without it
`pkg-config --exists libxfce4panel-2.0` fails.

### Install

```bash
chmod +x install.sh
./install.sh            # current user only (~/.local)
./install.sh --system   # system-wide (/usr, needs sudo)
```

| mode | module | .desktop |
|------|--------|----------|
| user | `~/.local/lib/<multiarch>/xfce4/panel/plugins/libdeepseek-indicator.so` | `~/.local/share/xfce4/panel/plugins/deepseek-indicator.desktop` |
| system | `/usr/lib/<multiarch>/xfce4/panel/plugins/libdeepseek-indicator.so` | `/usr/share/xfce4/panel/plugins/deepseek-indicator.desktop` |

The script restarts the panel (`xfce4-panel -r`). Add the plugin via
**right click on the panel → Panel → Add New Items → "DeepSeek Indicator"**.

### Verify

```bash
xfce4-panel --version
ls -l ~/.local/lib/*/xfce4/panel/plugins/libdeepseek-indicator.so
PANEL_DEBUG=module-factory xfce4-panel -r 2>&1 | grep deepseek
```

You should see `new module deepseek-indicator, filename=..., internal=false`.

### Tests

```bash
cd plugin && make test
```

25 cases: both peak windows and their boundaries, weekends, the 29/30/31 min
warning thresholds, holidays (including a holiday + weekend chain), computed
time to the next change, and completeness of both translation tables.

### Holidays

`~/.config/xfce4/deepseek-indicator/holidays.txt` is created on first run.
One `YYYY-MM-DD` date per line, `#` starts a comment. Edits apply without
restarting the panel (re-read every 30 s).

### Uninstall

```bash
./install.sh --uninstall            # user copy
./install.sh --uninstall --system   # /usr copy
```

### Layout

```
plugin/deepseek-indicator.c          # plugin code (Gtk.DrawingArea + peak logic + i18n)
plugin/deepseek-indicator.desktop    # X-XFCE-Module=deepseek-indicator entry
plugin/Makefile                      # build + make test
install.sh                           # build + install user/system/uninstall
```

### License

This project is open source, released under the **Apache License 2.0**.
See the [LICENSE](LICENSE) file for the full text.

You may use, modify, and redistribute this software, including for commercial
purposes, provided you comply with the license terms: keep the copyright,
license, and attribution notices, include a copy of the license, state
significant changes, and do not use the project's trademarks to imply
endorsement. The license also grants an express patent license from
contributors.

### Disclaimer

This is an unofficial, community-made project. It is **not affiliated with,
endorsed by, sponsored by, or in any other way connected to** DeepSeek
(Hangzhou DeepSeek Artificial Intelligence Basic Technology Research Co., Ltd.)
or any of its affiliates, subsidiaries, or partners.

"DeepSeek" and all related names, logos, and trademarks are the property of
their respective owners. The name is used here **solely for informational and
descriptive purposes** — to state what the tool displays (the publicly
documented peak-hour schedule of the DeepSeek API) — in a nominative way. The
author claims **no rights whatsoever** to the name, logo, or brand, and no
endorsement or partnership is implied.

All product and company names mentioned are trademarks or registered
trademarks of their respective holders. Peak-hour windows, pricing, and the
API itself may change at any time and without notice; this tool is not an
official source of truth — always verify against the official DeepSeek
documentation.

The software is provided "as is", without warranty of any kind, express or
implied, including but not limited to the warranties of merchantability,
fitness for a particular purpose and noninfringement.

---

## Polski

Kropka w panelu Xfce sygnalizująca godziny szczytu (peak) API DeepSeek.

### Godziny szczytu

- **Poniedziałek – piątek**
- **01:00 – 04:00 UTC** (02:00 – 05:00 czasu polskiego zimą)
- **06:00 – 10:00 UTC** (07:00 – 11:00 czasu polskiego zimą)

| kropka | znaczenie |
|--------|-----------|
| zielona | pozaszczyt |
| pomarańczowa | pozaszczyt, ale szczyt zaczyna się za mniej niż 30 min |
| czerwona | szczyt |

Tooltip pokazuje aktualny stan, godzinę UTC, czas do najbliższej zmiany oraz
godzinę i datę tej zmiany:

```
DeepSeek: pozaszczyt, szczyt wkrótce (00:47 UTC)
Zmiana za 13 min → szczyt od 01:00 UTC
```

Weekendy oraz daty z `~/.config/xfce4/deepseek-indicator/holidays.txt` są
traktowane jako off-peak. Prawy klik: cennik DeepSeek, odświeżenie,
„O programie”.

### Język

Interfejs idzie za językiem systemu: polski tylko gdy locale zaczyna się od
`pl` (`g_get_language_names()`, czyli `LANGUAGE`/`LC_ALL`/`LC_MESSAGES`/`LANG`),
w pozostałych przypadkach angielski. Wpis `.desktop` ma nazwy i opisy w obu
językach.


### Wymagania

```bash
sudo apt install build-essential pkg-config libgtk-3-dev libxfce4panel-2.0-dev libxfce4ui-2-dev
```

`libxfce4ui-2-dev` jest konieczne: `libxfce4panel-2.0.pc` ma
`Requires.private: libxfce4ui-2`, ale pakiet `libxfce4panel-2.0-dev`
w Debianie **nie** ciągnie `libxfce4ui-2-dev`, więc bez niego
`pkg-config --exists libxfce4panel-2.0` zgłasza brak pakietu.

### Instalacja

```bash
chmod +x install.sh
./install.sh            # tylko dla bieżącego użytkownika (~/.local)
./install.sh --system   # systemowo (/usr, wymaga sudo)
```

| tryb | moduł | plik .desktop |
|------|-------|---------------|
| user | `~/.local/lib/<multiarch>/xfce4/panel/plugins/libdeepseek-indicator.so` | `~/.local/share/xfce4/panel/plugins/deepseek-indicator.desktop` |
| system | `/usr/lib/<multiarch>/xfce4/panel/plugins/libdeepseek-indicator.so` | `/usr/share/xfce4/panel/plugins/deepseek-indicator.desktop` |

Skrypt restartuje panel (`xfce4-panel -r`). Dodanie wtyczki:
**prawy klik na panel → Panel → Dodaj nowe elementy → „DeepSeek Indicator”**.

### Weryfikacja

```bash
xfce4-panel --version
ls -l ~/.local/lib/*/xfce4/panel/plugins/libdeepseek-indicator.so
PANEL_DEBUG=module-factory xfce4-panel -r 2>&1 | grep deepseek
```

W debugu powinno pojawić się
`new module deepseek-indicator, filename=..., internal=false`.

### Testy

```bash
cd plugin && make test
```

25 przypadków: granice obu okien peak, weekendy, progi ostrzeżenia
29/30/31 min, święta (także łańcuch święto + weekend), wyliczony czas do
zmiany oraz kompletność obu tablic tłumaczeń.

### Święta / dni wolne

Plik `~/.config/xfce4/deepseek-indicator/holidays.txt` tworzony przy pierwszym
uruchomieniu. Jedna data `YYYY-MM-DD` na linię, `#` = komentarz. Zmiany
działają bez restartu panelu (odczyt co 30 s).

### Odinstalowanie

```bash
./install.sh --uninstall            # kopia użytkownika
./install.sh --uninstall --system   # kopia w /usr
```

### Struktura

```
plugin/deepseek-indicator.c          # kod wtyczki (Gtk.DrawingArea + logika peak + i18n)
plugin/deepseek-indicator.desktop    # wpis X-XFCE-Module=deepseek-indicator
plugin/Makefile                      # build + make test
install.sh                           # build + instalacja user/system/uninstall
```

### Licencja

Projekt jest otwartoźródłowy i udostępniany na **licencji Apache 2.0**.
Pełny tekst znajduje się w pliku [LICENSE](LICENSE).

Możesz używać, modyfikować i redystrybuować to oprogramowanie, również
komercyjnie, pod warunkiem zachowania warunków licencji: zachowania informacji
o prawach autorskich, licencji i autorstwie, dołączenia kopii licencji,
wskazania istotnych zmian oraz nieużywania znaków towarowych projektu
w sposób sugerujący poparcie. Licencja zawiera też wyraźny grant patentowy
od współtwórców.

### Zastrzeżenia prawne

To nieoficjalny, społecznościowy projekt. **Nie jest powiązany, sponsorowany,
autoryzowany ani w żaden inny sposób związany** z DeepSeek (Hangzhou DeepSeek
Artificial Intelligence Basic Technology Research Co., Ltd.) ani z jej
podmiotami powiązanymi, spółkami zależnymi lub partnerami.

„DeepSeek” oraz wszystkie powiązane nazwy, logotypy i znaki towarowe należą do
ich właścicieli. Nazwa jest tu używana **wyłącznie w celach informacyjnych
i opisowych** — aby wskazać, co prezentuje narzędzie (publicznie udokumentowany
harmonogram godzin szczytu API DeepSeek) — w sposób nominatywny. Autor **nie
rości sobie żadnych praw** do nazwy, logo ani marki; użycie nazwy nie oznacza
poparcia, współpracy ani partnerstwa.

Wszystkie wymienione nazwy produktów i firm są znakami towarowymi lub
zastrzeżonymi znakami towarowymi ich właścicieli. Godziny szczytu, cennik oraz
samo API mogą się zmienić w każdej chwili i bez powiadomienia; to narzędzie nie
jest oficjalnym źródłem informacji — zawsze weryfikuj dane w oficjalnej
dokumentacji DeepSeek.

Oprogramowanie dostarczane jest „tak jak jest”, bez jakiejkolwiek gwarancji,
dorozumianej lub wyraźnej, w tym bez gwarancji przydatności handlowej,
przydatności do określonego celu i nienaruszania praw osób trzecich.
