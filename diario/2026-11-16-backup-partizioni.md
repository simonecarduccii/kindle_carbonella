# 2026-11-16 · Backup delle partizioni del Kindle

**Stato:** 🟡 script pronto, da eseguire sul Mac di Simone.

## Perché

Ora che abbiamo il root via SSH, prima di far girare qualsiasi codice nostro salviamo un'immagine
del sistema. Se un giorno Carbonella rompesse qualcosa, questa è la copia da cui ripartire
(il recupero vero si fa con Kubrick, vedi `docs/jailbreak.md` §8).

## Cosa abbiamo preparato (10:08–10:20 UTC, circa 12 minuti)

- `strumenti/backup-k4.sh`: script per il Mac che, via SSH, copia:
  - la **zona di avvio** del chip di memoria (`/dev/mmcblk0` fino all'inizio della prima partizione:
    bootloader e kernel);
  - ogni **partizione di sistema** `mmcblk0pN`, saltando quella dei libri (`/mnt/base-us`);
  - `info.txt` con `uname`, `/proc/partitions`, `mount` e `df`;
  - `md5.txt` con i checksum, confrontati con quelli calcolati sul Kindle.
- Scelte:
  - lo script **legge soltanto** (`dd if=...`): sul Kindle non scrive niente;
  - una sola connessione SSH condivisa (`ControlMaster`), così la password si chiede una volta;
  - `bs=1048576` invece di `bs=1M`: la `busybox` del Kindle è vecchia e potrebbe non capire i suffissi;
  - le partizioni in uso in scrittura (es. `/var/local`) possono dare un checksum diverso: è normale.
- `docs/jailbreak.md` §7 aggiornato per usare lo script.

## Passi per Simone

1. Attiva USBNetwork sul Kindle (`;debugOn`, `~usbNetwork`, `;debugOff`) e verifica `ping 192.168.15.244`.
2. Nel Terminale del Mac, dalla cartella del repository: `sh strumenti/backup-k4.sh`
3. Inserisci la password di root quando la chiede.
4. Aspetta la fine (la partizione di sistema è qualche centinaio di MB: alcuni minuti).
5. Fai uno screenshot del Terminale e copia un'altra volta la cartella `~/kindle-backup/` (disco esterno o cloud).

## Risultato

_Da compilare dopo il backup: dimensioni delle partizioni, tempi, checksum OK/diversi, screenshot._
