#!/bin/sh
# Carbonella - avvio sul Kindle 4.
#
# 1. ferma l'interfaccia Amazon (il "framework"), altrimenti disegnerebbe sopra di noi;
# 2. impedisce lo screensaver mentre leggiamo (Carbonella ha un suo timer di inattività);
# 3. avvia il lettore;
# 4. quando il lettore finisce, in QUALSIASI modo (uscita normale, errore, crash),
#    rimette tutto come prima e fa ripartire l'interfaccia Amazon.
#
# Niente di tutto questo tocca il sistema: se qualcosa va storto basta riavviare il Kindle
# (tenere premuto il tasto di accensione per 20 secondi) e si torna al Kindle originale.

BASE=/mnt/us/carbonella
LOG="$BASE/carbonella.log"

[ -f "$LOG" ] && mv -f "$LOG" "$LOG.prima"
exec >"$LOG" 2>&1
echo "Carbonella: avvio $(date)"

restore() {
    echo "Carbonella: ripristino $(date)"
    lipc-set-prop com.lab126.powerd preventScreenSaver 0 2>/dev/null
    if [ -n "$FRAMEWORK_STOPPED" ]; then
        /etc/init.d/framework start
    fi
    rm -f /var/run/carbonella.pid
}
trap restore EXIT
trap 'exit 1' INT TERM

if [ -f /var/run/carbonella.pid ] && kill -0 "$(cat /var/run/carbonella.pid)" 2>/dev/null; then
    echo "Carbonella: già in esecuzione"
    trap - EXIT
    exit 0
fi
echo $$ > /var/run/carbonella.pid

# Lascia il tempo a KUAL di chiudere il suo menu.
sleep 1
lipc-set-prop com.lab126.powerd preventScreenSaver 1 2>/dev/null
/etc/init.d/framework stop
FRAMEWORK_STOPPED=1
# Il framework a volte impiega qualche istante a liberare lo schermo.
sleep 2

"$BASE/bin/carbonella" --base "$BASE" "$@"
echo "Carbonella: uscita con codice $?"
