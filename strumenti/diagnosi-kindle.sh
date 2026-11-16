#!/bin/sh
# Raccoglie via SSH tutto quello che serve per capire perché Carbonella non parte.
# Solo lettura: non cambia niente sul Kindle.
#   sh strumenti/diagnosi-kindle.sh        (oppure: make diagnosi)
# Il risultato finisce in build/diagnosi.txt: mandalo a Claude.
KINDLE=${KINDLE:-kindle}
mkdir -p build
OUT=build/diagnosi.txt
ssh "$KINDLE" 'sh -s' > "$OUT" 2>&1 <<'REMOTE'
section() { echo; echo "===== $*"; }
section data; date; uptime
section avvio.log; tail -20 /mnt/us/carbonella/avvio.log 2>&1
section carbonella.log; cat /mnt/us/carbonella/carbonella.log 2>&1
section carbonella.log.prima; cat /mnt/us/carbonella/carbonella.log.prima 2>&1
section file; ls -la /mnt/us/carbonella /mnt/us/carbonella/bin /mnt/us/extensions /mnt/us/extensions/carbonella /mnt/us/extensions/carbonella/bin 2>&1
section mount; mount | grep -E 'mnt/us|base-us'
section lettore; /mnt/us/carbonella/bin/carbonella --versione; echo "codice uscita: $?"
section comandi; for c in setsid nohup lipc-set-prop; do printf '%s: ' "$c"; command -v "$c" || echo MANCA; done; ls -l /etc/init.d/framework
section menu-estensioni; for f in /mnt/us/extensions/*/menu.json; do echo "--- $f"; cat "$f"; done
section kual; ls -la /mnt/us/developer/KUAL/work 2>&1; tail -30 /mnt/us/developer/KUAL/work/crash.log 2>/dev/null
section processi; ps | grep -E 'carbonella|cvm|kual' | grep -v grep
section sistema; uname -a; head -5 /proc/cpuinfo
REMOTE
echo "Fatto: $OUT"
cat "$OUT"
