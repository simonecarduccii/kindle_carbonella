#!/bin/sh
# Carbonella - avvio sul Kindle 4.
#
# 1. ferma l'interfaccia Amazon (il "framework"), altrimenti disegnerebbe sopra di noi;
# 2. impedisce lo screensaver automatico mentre leggiamo (Carbonella ha un suo timer di inattività);
# 3. avvia il lettore e gli inoltra gli eventi di standby del Kindle (tasto di accensione):
#    SIGUSR1 = va in standby (Carbonella mostra la copertina del libro), SIGUSR2 = si risveglia;
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
    [ -n "$WATCHER" ] && kill "$WATCHER" 2>/dev/null
    killall -9 lipc-wait-event 2>/dev/null # ignora SIGTERM
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
# Niente preventScreenSaver: con quello powerd ignora il tasto di accensione e lo standby non arriva
# (v0.5.2). Il lettore rinvia lo standby automatico mentre leggi (touchScreenSaverTimeout).
lipc-set-prop com.lab126.powerd preventScreenSaver 0 2>/dev/null
say "fermo il framework"
/etc/init.d/framework stop
FRAMEWORK_STOPPED=1
# Il framework a volte impiega qualche istante a liberare lo schermo.
sleep 2

say "avvio il lettore"
"$BASE/bin/carbonella" --base "$BASE" "$@" &
READER=$!

# Standby: powerd (il servizio del Kindle che gestisce il tasto di accensione) annuncia quando il
# Kindle va in standby e quando si risveglia; noi lo diciamo al lettore.
# Prima chiudiamo i watcher rimasti da avvii precedenti (se il lettore era stato chiuso da fuori).
for P in $(ps | grep goingToScreenSaver | grep -v grep | awk '{print $1}'); do kill -9 "$P" 2>/dev/null; done
if which lipc-wait-event >/dev/null 2>&1; then
    # Un evento alla volta (senza -m): lipc-wait-event esce e scrive subito, niente buffer nella pipe.
    ( while kill -0 "$READER" 2>/dev/null; do
        EV=$(lipc-wait-event com.lab126.powerd goingToScreenSaver,outOfScreenSaver 2>/dev/null)
        EV=${EV%% *}
        [ -z "$EV" ] && { sleep 1; continue; }
        say "powerd: $EV"
        case "$EV" in
            goingToScreenSaver) kill -USR1 "$READER" 2>/dev/null ;;
            outOfScreenSaver) kill -USR2 "$READER" 2>/dev/null ;;
        esac
    done ) &
    WATCHER=$!
else
    say "lipc-wait-event non c'è: niente copertina in standby"
fi

wait "$READER"
say "il lettore è uscito con codice $?"
