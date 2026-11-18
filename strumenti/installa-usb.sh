#!/bin/sh
# Installa (o aggiorna) Carbonella su un Kindle collegato via USB, dal Mac o da Linux.
# Non serve SSH: va bene anche per i Kindle degli amici.
#
#   sh strumenti/installa-usb.sh dist/carbonella-v0.1.0.zip
#
# Il Kindle deve essere già sbloccato (jailbreak) e avere KUAL: vedi docs/installazione.md.
# I libri, le posizioni di lettura e le preferenze (carbonella/dati) non vengono toccati.
set -e

ZIP="$1"
if [ -z "$ZIP" ]; then
    ZIP=$(ls -t dist/carbonella-v*.zip 2>/dev/null | head -1)
fi
if [ ! -f "$ZIP" ]; then
    echo "Pacchetto non trovato. Uso: sh strumenti/installa-usb.sh carbonella-vX.Y.Z.zip"
    exit 1
fi

KINDLE=""
for d in /Volumes/Kindle /media/"$USER"/Kindle /run/media/"$USER"/Kindle; do
    if [ -d "$d/documents" ]; then
        KINDLE="$d"
        break
    fi
done
if [ -z "$KINDLE" ]; then
    echo "Kindle non trovato. Collegalo con il cavo USB e aspetta che compaia come disco \"Kindle\"."
    exit 1
fi
if [ ! -d "$KINDLE/extensions" ]; then
    echo "Attenzione: su $KINDLE non c'è la cartella extensions. KUAL è installato? (docs/installazione.md)"
fi

echo "Installo $(basename "$ZIP") su $KINDLE ..."
unzip -o -q "$ZIP" -d "$KINDLE"
sync
echo "Fatto. Ora espelli il Kindle, scollega il cavo e apri KUAL → Carbonella → Apri Carbonella."

case "$(uname)" in
Darwin) diskutil eject "$KINDLE" >/dev/null 2>&1 && echo "Kindle espulso." || true ;;
esac
