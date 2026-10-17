#!/usr/bin/env bash
#
# Installs the DeepSeek Indicator Xfce4 panel plugin (GTK3 module).
#
#   ./install.sh              # install for current user (~/.local)
#   ./install.sh --system     # install system-wide (/usr, needs sudo)
#   ./install.sh --uninstall  # remove (user copy; add --system for /usr)
#
set -euo pipefail

MODE=user
ACTION=install

for arg in "$@"; do
  case "$arg" in
    --user)      MODE=user ;;
    --system)    MODE=system ;;
    --uninstall) ACTION=uninstall ;;
    -h|--help)
      sed -n '3,7p' "$0" | sed 's/^# \{0,1\}//'
      exit 0 ;;
    *) echo "Nieznany argument: $arg" >&2; exit 2 ;;
  esac
done

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$HERE/plugin"
MODULE="libdeepseek-indicator.so"
DESKTOP="deepseek-indicator.desktop"
PKGS="gtk+-3.0 libxfce4panel-2.0"
APT_HINT="sudo apt install build-essential pkg-config libgtk-3-dev libxfce4panel-2.0-dev libxfce4ui-2-dev"

die() { echo "Błąd: $*" >&2; exit 1; }

restart_panel() {
  if command -v xfce4-panel >/dev/null && pgrep -x xfce4-panel >/dev/null; then
    xfce4-panel -r
    echo "Panel zrestartowany."
  else
    echo "Panel nie działa — uruchom go lub wyloguj się i zaloguj ponownie."
  fi
}

# --- uninstall (does not need compilers or -dev packages) -----------------
if [[ "$ACTION" == uninstall ]]; then
  find "$HOME/.local" -type f \
       \( -name "$MODULE" -o -name "$DESKTOP" \) \
       -path '*xfce4/panel/plugins*' -delete 2>/dev/null || true
  if [[ "$MODE" == system ]]; then
    sudo find /usr -type f \
         \( -name "$MODULE" -o -name "$DESKTOP" \) \
         -path '*xfce4/panel/plugins*' -delete
  fi
  echo "Odinstalowano ($MODE)."
  restart_panel
  exit 0
fi

# --- dependencies ---------------------------------------------------------
for cmd in gcc make pkg-config; do
  command -v "$cmd" >/dev/null || die "brak $cmd ($APT_HINT)"
done
for p in $PKGS; do
  if ! pkg-config --exists "$p"; then
    reason="$(pkg-config --print-errors --exists "$p" 2>&1 || true)"
    die "pkg-config nie widzi '$p':
${reason:-brak pliku .pc}
Zainstaluj: $APT_HINT"
  fi
done

# --- target paths (must match xfce4-panel's panel-module-factory.c) ------
PREFIX="$(pkg-config --variable=prefix libxfce4panel-2.0)"
LIBDIR="$(pkg-config --variable=libdir libxfce4panel-2.0)"
DATADIR="$(pkg-config --variable=datadir libxfce4panel-2.0)"
LIB_SUFFIX="${LIBDIR#"$PREFIX"}"     # np. /lib/x86_64-linux-gnu

if [[ "$MODE" == system ]]; then
  LIB_DST="${LIBDIR}/xfce4/panel/plugins"
  DATA_DST="${DATADIR}/xfce4/panel/plugins"
  INSTALL="sudo install -Dm644"
else
  LIB_DST="$HOME/.local${LIB_SUFFIX}/xfce4/panel/plugins"
  DATA_DST="$HOME/.local/share/xfce4/panel/plugins"
  INSTALL="install -Dm644"
fi

# --- build ----------------------------------------------------------------
echo "Budowanie wtyczki ($MODE)..."
make -C "$SRC_DIR" clean
make -C "$SRC_DIR"

# --- install --------------------------------------------------------------
$INSTALL "$SRC_DIR/$MODULE"  "$LIB_DST/$MODULE"
$INSTALL "$SRC_DIR/$DESKTOP" "$DATA_DST/$DESKTOP"

echo
echo "Zainstalowano:"
echo "  $LIB_DST/$MODULE"
echo "  $DATA_DST/$DESKTOP"
echo
echo "Panel:   $(xfce4-panel --version 2>/dev/null || echo '?')"
echo "Sprawdź: PANEL_DEBUG=module-factory xfce4-panel -r   (szukaj: new module deepseek-indicator)"
echo

restart_panel
echo
echo "Teraz: prawy klik na panel -> Panel -> Dodaj nowe elementy -> 'DeepSeek Indicator'."
