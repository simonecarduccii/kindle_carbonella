#!/bin/sh
# Avvia Carbonella staccandola da KUAL: KUAL vive dentro l'interfaccia Amazon, che Carbonella
# ferma. Senza "nohup ... &" Carbonella verrebbe chiusa insieme a KUAL.
nohup /bin/sh /mnt/us/carbonella/carbonella.sh "$@" >/dev/null 2>&1 &
exit 0
