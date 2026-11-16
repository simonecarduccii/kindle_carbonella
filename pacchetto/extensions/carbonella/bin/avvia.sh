#!/bin/sh
# Avvia Carbonella staccandola da KUAL.
#
# KUAL vive dentro l'interfaccia Amazon, che Carbonella ferma: se Carbonella restasse "figlia" di KUAL
# verrebbe chiusa insieme a lui. Per questo la lanciamo in una nuova sessione (setsid) o almeno
# immune alla chiusura del terminale (nohup), e torniamo subito a KUAL.
#
# Ogni avvio lascia una riga in /mnt/us/carbonella/avvio.log: così sappiamo se KUAL ci ha chiamati.

BASE=/mnt/us/carbonella
echo "$(date '+%Y-%m-%d %H:%M:%S') avvia.sh $* (utente $(id -u))" >> "$BASE/avvio.log"

if command -v setsid >/dev/null 2>&1; then
    setsid /bin/sh "$BASE/carbonella.sh" "$@" </dev/null >/dev/null 2>&1 &
elif command -v nohup >/dev/null 2>&1; then
    nohup /bin/sh "$BASE/carbonella.sh" "$@" </dev/null >/dev/null 2>&1 &
else
    ( trap '' HUP TERM; exec /bin/sh "$BASE/carbonella.sh" "$@" ) </dev/null >/dev/null 2>&1 &
fi
exit 0
