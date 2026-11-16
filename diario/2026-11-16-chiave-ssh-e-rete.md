# 2026-11-16 · Chiave SSH e rete USB sempre attiva

**Stato:** ✅ accesso senza password funzionante (11:58 UTC). Rete automatica attiva dal prossimo riavvio.

## Perché

Ogni `ssh`, `scp` e `make run` chiedeva la password di root, e la rete via USB andava riattivata a
mano (`;debugOn`, `~usbNetwork`, `;debugOff`) a ogni riavvio. Simone ha chiesto di eliminare entrambe le cose.

## Chiave SSH (11:23–11:58 UTC, circa 35 minuti)

1. Primo tentativo con `strumenti/chiave-ssh.sh`: chiave **ed25519**, copiata in
   `/mnt/us/usbnet/etc/authorized_keys`. Il Kindle rispondeva `Permission denied (publickey,password)`.
2. Diagnosi (fatta direttamente sul Mac di Simone):
   - il server SSH del Kindle è **Dropbear 2015.68**;
   - Dropbear supporta le chiavi ed25519 solo dalla versione 2020: la chiave veniva proposta e rifiutata;
   - il percorso `usbnet/etc/authorized_keys` era giusto.
3. Soluzione: chiave **RSA** `~/.ssh/kindle_rsa` e, nel blocco `Host kindle` di `~/.ssh/config`,
   `PubkeyAcceptedAlgorithms +ssh-rsa` e `HostKeyAlgorithms +ssh-rsa` (i Mac recenti disattivano
   l'algoritmo RSA vecchio che Dropbear 2015 usa).
4. Verifica: `ssh -o BatchMode=yes kindle echo OK` → `OK`, senza password. 🎉
5. Lo script `strumenti/chiave-ssh.sh` ora crea direttamente la chiave RSA.

Nota di sicurezza: prima della chiave il Kindle accettava sia la password vuota sia `mario`, quindi
probabilmente qualunque password. Va bene finché la rete è solo sul cavo USB verso il Mac; **non
attivare SSH via Wi-Fi**.

## Rete USB sempre attiva

- La documentazione del pacchetto USBNetwork sul Kindle è in `/mnt/us/usbnet/info.txt` (non c'è un
  `README_FIRST`). Dice di rinominare `usbnet/DISABLED_auto` in `usbnet/auto`: fatto.
- Effetto: **dal prossimo riavvio** USBNetwork parte da solo. Il Kindle collegato al Mac non apparirà
  più come disco USB: i file si copiano con `scp`.
- Per tornare alla modalità disco: `ssh kindle mv /mnt/us/usbnet/auto /mnt/us/usbnet/DISABLED_auto` e riavvia.
- Sul Mac l'interfaccia "RNDIS/Ethernet Gadget" aveva già l'IP manuale `192.168.15.201` (maschera
  `255.255.255.0`): resta salvato, non serve rifarlo.

## File cambiati

- Mac: `~/.ssh/kindle_rsa` e `.pub` (nuovi), `~/.ssh/config` (blocco `Host kindle`).
- Kindle: una riga in più in `/mnt/us/usbnet/etc/authorized_keys`; `usbnet/DISABLED_auto` → `usbnet/auto`.
  Nessuna modifica al sistema (niente `mntroot rw`), nessun riavvio.

## Da verificare

- Al prossimo riavvio: che la rete parta da sola e che `ssh kindle` funzioni subito.
