#!/bin/sh
# Backup delle partizioni di sistema del Kindle 4, via USBNetwork/SSH.
#
# Uso (dal Mac, con USBNetwork attivo):
#   sh strumenti/backup-k4.sh                 # salva in ~/kindle-backup/AAAA-MM-GG
#   sh strumenti/backup-k4.sh /percorso/cartella
#
# Lo script LEGGE soltanto dal Kindle: non scrive nulla sul dispositivo.
# Salta la partizione dei libri (/mnt/base-us), che si copia già via USB come disco.

set -eu

KINDLE="${KINDLE:-root@192.168.15.244}"
DEST="${1:-$HOME/kindle-backup/$(date +%Y-%m-%d)}"
SOCK="/tmp/k4-backup-$$"

# Una sola connessione SSH riusata per tutti i comandi: la password si chiede una volta.
k4() { ssh -o ControlMaster=auto -o ControlPath="$SOCK" -o ControlPersist=300 "$KINDLE" "$@"; }
cleanup() { ssh -o ControlPath="$SOCK" -O exit "$KINDLE" 2>/dev/null || true; }
trap cleanup EXIT

if command -v md5sum >/dev/null 2>&1; then
    local_md5() { md5sum "$1" | cut -d' ' -f1; }
else
    local_md5() { md5 -q "$1"; } # macOS
fi

mkdir -p "$DEST"
echo "Backup in: $DEST"
echo "Connessione a $KINDLE (se chiede la password, inseriscila)..."
k4 true

echo "1/3  Salvo le informazioni del sistema in info.txt"
k4 "uname -a; echo; cat /proc/partitions; echo; mount; echo; df" > "$DEST/info.txt"

# Partizione dei libri, da saltare (es. mmcblk0p4).
USERSTORE=$(k4 "mount" | awk '$3 == "/mnt/base-us" { sub(".*/", "", $1); print $1 }')
PARTS=$(k4 "cat /proc/partitions" | awk '$4 ~ /^mmcblk0p[0-9]+$/ { print $4 }')
echo "     partizioni trovate: $(echo $PARTS)  (salto quella dei libri: ${USERSTORE:-nessuna})"

echo "2/3  Copio la zona di avvio (bootloader e kernel, prima della prima partizione)"
START=$(k4 "cat /sys/block/mmcblk0/mmcblk0p1/start 2>/dev/null" || true)
if [ -n "$START" ]; then
    k4 "dd if=/dev/mmcblk0 bs=512 count=$START 2>/dev/null" > "$DEST/mmcblk0-avvio.img"
    echo "     mmcblk0-avvio.img: $(wc -c < "$DEST/mmcblk0-avvio.img") byte"
else
    echo "     ATTENZIONE: non trovo l'inizio di mmcblk0p1, salto la zona di avvio."
fi

echo "3/3  Copio le partizioni (la più grande può richiedere qualche minuto)"
for p in $PARTS; do
    [ "$p" = "$USERSTORE" ] && continue
    printf '     %s ... ' "$p"
    t0=$(date +%s)
    k4 "dd if=/dev/$p bs=1048576 2>/dev/null" > "$DEST/$p.img"
    t1=$(date +%s)
    remote=$(k4 "md5sum /dev/$p" | cut -d' ' -f1)
    here=$(local_md5 "$DEST/$p.img")
    if [ "$remote" = "$here" ]; then check="checksum OK"; else check="checksum DIVERSO (normale solo per una partizione in uso, es. /var/local)"; fi
    echo "$(wc -c < "$DEST/$p.img") byte in $((t1 - t0)) s, $check"
    echo "$here  $p.img" >> "$DEST/md5.txt"
done

echo
echo "Fatto. Contenuto di $DEST:"
ls -l "$DEST"
echo "Conserva questa cartella in un posto sicuro (anche una seconda copia)."
