# 2026-11-16 · Chiave SSH e rete USB sempre attiva

**Stato:** ✅ accesso senza password funzionante (11:58 UTC). 🟡 Rete automatica: dopo il riavvio il Mac non raggiunge il Kindle, da capire.

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

## Prova del riavvio (13:49–14:19 ora italiana)

- Simone ha riavviato il Kindle tenendolo collegato. Dopo il riavvio il Kindle **non compare più come
  disco** nel Finder: la modalità rete sembra partita da sola.
- Però dal Mac `ping 192.168.15.244` non risponde e `ssh kindle` va in timeout, anche dopo aver
  staccato e riattaccato il cavo.
- Sessione chiusa qui. **Da riprendere:** controllare sul Mac se l'interfaccia "RNDIS/Ethernet Gadget"
  è attiva e ha l'IP `192.168.15.201` (`ifconfig`), e in alternativa resettare la rete a mano dal Kindle
  (`;debugOn`, `~usbNetwork`, `;debugOff`: il comando accende/spegne).
- Per tornare alla modalità disco, se serve: nella ricerca del Kindle `;debugOn`, `~usbNetwork`, `;debugOff`
  spegne la rete per questa sessione. Per renderlo permanente bisogna rinominare `usbnet/auto` in
  `usbnet/DISABLED_auto` (dal Finder, quando il Kindle è in modalità disco).
