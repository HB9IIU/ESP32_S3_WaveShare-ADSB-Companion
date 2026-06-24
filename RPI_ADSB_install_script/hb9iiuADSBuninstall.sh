#!/bin/bash
# HB9IIU ADS-B – full uninstall / clean-slate script
# Run as root:  sudo bash hb9iiuADSBuninstall.sh

set -euo pipefail

if [ "$EUID" -ne 0 ]; then
  echo "❌ Run as root: sudo bash $0" >&2
  exit 1
fi

log()  { echo -e "\n🟦 $*\n"; }
ok()   { echo -e "✅ $*"; }
warn() { echo -e "⚠️  $*"; }

# Detect target user (same logic as installer)
TARGET_USER="${SUDO_USER:-}"
if [ -z "$TARGET_USER" ]; then
  read -r -t 30 -p "Username used during install [pi]: " TARGET_USER || true
  TARGET_USER="${TARGET_USER:-pi}"
fi
TARGET_HOME="$(getent passwd "$TARGET_USER" | cut -d: -f6)"
BASE_DIR="${TARGET_HOME}/pythonApps"

echo
echo "════════════════════════════════════════════════"
echo "  HB9IIU ADS-B uninstaller"
echo "  Target user : $TARGET_USER  ($TARGET_HOME)"
echo "  Apps dir    : $BASE_DIR"
echo "════════════════════════════════════════════════"
echo
read -r -p "Continue and wipe everything? [y/N]: " CONFIRM
if [[ "${CONFIRM,,}" != "y" ]]; then
  echo "Aborted."
  exit 0
fi

# ==========================================================
# 1) Our custom systemd units
# ==========================================================
log "Stopping + disabling custom services"

_remove_unit() {
  local unit="$1"
  systemctl stop    "$unit" 2>/dev/null || true
  systemctl disable "$unit" 2>/dev/null || true
  rm -f "/etc/systemd/system/$unit"
  ok "Removed $unit"
}

_remove_unit adsb-stats.timer
_remove_unit adsb-stats.service
_remove_unit image-builder.service
_remove_unit pi-status-web.service
_remove_unit photo-gallery.timer
_remove_unit photo-gallery.service
_remove_unit rtl-biast.service

systemctl daemon-reload
ok "systemd reloaded"

# ==========================================================
# 2) Python apps + venv
# ==========================================================
log "Removing Python apps dir: $BASE_DIR"
rm -rf "$BASE_DIR"
ok "Removed $BASE_DIR"

# ==========================================================
# 3) adsb-stats state backup
# ==========================================================
log "Removing adsb-stats state"
rm -rf /var/lib/adsb-stats
ok "Removed /var/lib/adsb-stats"

# ==========================================================
# 4) Web assets
# ==========================================================
log "Removing web assets"
rm -rf /var/www/html/hex \
       /var/www/html/jpg \
       /var/www/html/jpglarge \
       /var/www/html/flags \
       /var/www/html/index.html \
       /var/www/html/stats.html \
       /var/www/html/gallery.html
ok "Web assets removed"

# ==========================================================
# 5) Bias-T helper script + blacklist
# ==========================================================
log "Removing Bias-T script and DVB blacklist"
rm -f /usr/local/sbin/rtl-biast-apply.sh
rm -f /etc/modprobe.d/rtl-sdr-blacklist.conf
ok "Removed bias-T artefacts"

# ==========================================================
# 6) tar1090  (wiedehopf uninstaller)
# ==========================================================
log "Uninstalling tar1090"
if [ -f /usr/local/share/tar1090/tar1090.sh ]; then
  bash /usr/local/share/tar1090/tar1090.sh uninstall || true
  ok "tar1090 uninstalled"
else
  warn "tar1090 not found – skipping"
fi

# ==========================================================
# 7) readsb  (wiedehopf uninstaller)
# ==========================================================
log "Uninstalling readsb"
if command -v readsb >/dev/null 2>&1; then
  bash -c "$(wget -q -O - https://raw.githubusercontent.com/wiedehopf/adsb-scripts/master/readsb-install.sh)" -- uninstall || true
  apt-get remove -y readsb 2>/dev/null || true
  ok "readsb uninstalled"
else
  warn "readsb not found – skipping"
fi

# ==========================================================
# 8) Samba
# ==========================================================
log "Removing Samba"
systemctl stop  smbd nmbd 2>/dev/null || true
systemctl disable smbd nmbd 2>/dev/null || true
apt-get remove -y --purge samba samba-common-bin samba-common 2>/dev/null || true
apt-get autoremove -y 2>/dev/null || true
# Restore original config if backup exists
if [ -f /etc/samba/smb.conf.bak ]; then
  cp /etc/samba/smb.conf.bak /etc/samba/smb.conf
  ok "smb.conf restored from backup"
fi
ok "Samba removed"

# ==========================================================
# 9) Done
# ==========================================================
log "UNINSTALL COMPLETE"
echo "Everything installed by hb9iiuADSBsetupRPI.sh has been removed."
echo "You can now run the installer again for a clean install."
echo
echo "73 de HB9IIU"
