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
PIDFILE=/tmp/carbonella.pid

[ -f "$LOG" ] && mv -f "$LOG" "$LOG.prima"
exec >"$LOG" 2>&1
say() { echo "$(date '+%H:%M:%S') carbonella.sh: $*"; }
say "avvio, argomenti: $*"

# Fermare il framework può mandare segnali anche a noi (eravamo partiti da KUAL):
# li ignoriamo, il lettore li gestisce da sé.
trap '' HUP TERM INT

restore() {
    say "ripristino"
    lipc-set-prop com.lab126.powerd preventScreenSaver 0 2>/dev/null
    if [ -n "$FRAMEWORK_STOPPED" ]; then
        say "riavvio il framework"
        /etc/init.d/framework start
    fi
    rm -f "$PIDFILE"
    say "fine"
}

if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
    say "già in esecuzione, esco"
    exit 0
fi
echo $$ > "$PIDFILE"
trap restore EXIT

# Il disco del Kindle (FAT) a volte perde i permessi: rendiamo eseguibile il lettore.
chmod +x "$BASE/bin/carbonella" 2>/dev/null
if ! "$BASE/bin/carbonella" --versione; then
    say "ERRORE: il lettore non parte su questo Kindle (vedi sopra), non fermo il framework"
    exit 1
fi

# Lascia il tempo a KUAL di chiudere il suo menu.
sleep 1
lipc-set-prop com.lab126.powerd preventScreenSaver 1 2>/dev/null
say "fermo il framework"
/etc/init.d/framework stop
FRAMEWORK_STOPPED=1
# Il framework a volte impiega qualche istante a liberare lo schermo.
sleep 2

say "avvio il lettore"
"$BASE/bin/carbonella" --base "$BASE" "$@"
say "il lettore è uscito con codice $?"
