# Jailbreak del Kindle 4 (firmware 4.1.4)

Guida passo passo per sbloccare un **Kindle 4 senza touch** (2011/2012, tasti + pad a 5 direzioni)
con firmware **4.1.4**, installare **KUAL** e attivare **USBNetwork** per entrare via SSH.
È il primo passo della roadmap di Carbonella: senza accesso root non possiamo avviare il nostro lettore.

> Verificata sulle fonti a novembre 2026. Il firmware 4.1.4 è l'ultimo uscito per il K4 e il
> jailbreak "diagnostico" funziona ancora: **non serve nessun downgrade, né il cavo seriale**.

---

## 0. Prima di tutto: i rischi, detti chiaramente

- **Brick.** Ogni modifica al sistema ha un rischio di rendere il Kindle inutilizzabile. Con questo
  metodo il rischio è basso (non tocca il kernel né il bootloader, e la procedura è stata ripetuta da
  migliaia di persone) ma **non è zero**. Esiste una procedura di recupero (Kubrick, vedi §8), però è
  complicata.
- **Garanzia.** Il K4 non è più in garanzia da anni, quindi qui non si perde nulla.
- **Store Amazon.** Dal 20 maggio 2026 Amazon non consegna più libri ai Kindle di questa generazione,
  e non uscirà più nessun aggiornamento del firmware. Il jailbreak non ti toglie niente che funzioni ancora.
- **Batteria.** Se si spegne a metà di un'installazione, può fare danni. Caricala almeno al 50%.

Se c'è un passo che non torna (una schermata diversa, un messaggio di errore), **fermati e chiedi**
invece di improvvisare.

---

## 1. Controlla di avere il dispositivo giusto

1. Dalla Home premi **Menu → Impostazioni (Settings)**. In fondo alla pagina leggi la versione:
   deve iniziare con **4.** (tu hai `Kindle 4.1.4 (3543630001)`, va bene).
   - Se iniziasse con **5.**, questa guida è sbagliata: quello è un Kindle Touch/Paperwhite.
2. Nella stessa pagina premi **Menu → Informazioni sul dispositivo (Device Info)** e leggi il numero di serie:
   - inizia con **B00E** → K4 argento (2011);
   - inizia con **B023** → K4 nero (2012).
   Entrambi vanno bene. Annotalo, ti servirà per USBNetwork (§6).

---

## 2. Backup e preparazione

Prima del jailbreak non si può fare un backup completo del sistema (serve proprio il root per
farlo, vedi §7). Quello che puoi e devi fare ora:

1. **Copia i tuoi libri.** Collega il Kindle al computer con il cavo USB: appare un disco chiamato
   `Kindle`. Copia sul computer **tutto il contenuto** del disco (soprattutto la cartella `documents/`).
2. **Libera spazio.** Lascia almeno 250 MB liberi sul disco del Kindle.
3. **Spegni il Wi-Fi** e tienilo spento per tutta la procedura: **Menu → Disattiva wireless**.
4. **Ricarica** il Kindle almeno al 50%.
5. **Già sbloccato in passato?** Se nella libreria vedi già un libro "You are Jailbroken", il Kindle
   ha un vecchio jailbreak. Il nuovo si può comunque rieseguire sopra (è previsto), ma se qualcosa
   non funziona, conviene prima disinstallare quello vecchio (vedi §8).

---

## 3. Cosa scaricare

Scarica tutto sul computer, in una cartella di lavoro (es. `kindle-jb/`).

| Cosa | File | Dove |
|------|------|------|
| Jailbreak | `K4-Jailbreak.zip` (v1.0.0, giugno 2026) | <https://github.com/KindleModding/K4-Jailbreak/releases/latest/download/K4-Jailbreak.zip> |
| Launcher KUAL | `KUAL-KDK-1.0.azw2` | <https://kindlemodding.org/jailbreaking/Legacy/post-jailbreak/installing-kual-mrpi/KUAL-KDK-1.0.azw2> |
| Installer pacchetti (MRPI, facoltativo) | `kual-mrinstaller-1.7.N-r19303.zip` | <https://kindlemodding.org/jailbreaking/Legacy/post-jailbreak/installing-kual-mrpi/kual-mrinstaller-1.7.N-r19303.zip> |
| USBNetwork (SSH) | `kindle-usbnetwork-0.xx.N-k4.zip` (prendi la versione più recente per **K4**) | allegati del thread MobileRead <https://www.mobileread.com/forums/showthread.php?t=88004> e snapshot di NiLuJe <https://www.mobileread.com/forums/showthread.php?t=225030> |

Pagine di riferimento, da tenere aperte:

- Guida ufficiale KindleModding per il K4: <https://kindlemodding.org/jailbreaking/Legacy/K4-Jailbreak/>
- Repository del jailbreak: <https://github.com/KindleModding/K4-Jailbreak>
- Wiki MobileRead del K4: <https://wiki.mobileread.com/wiki/Kindle4NTHacking>
- Guida 2026 molto curata (in inglese e francese): <https://github.com/kxrz/kindle4-revival>

**Perché `K4-Jailbreak` e non il vecchio `kindle-k4-jailbreak-1.8.N`?** Il pacchetto storico di
NiLuJe usa certificati sviluppatore **scaduti ad aprile 2025**: dopo il jailbreak, KUAL e gli altri
pacchetti rischiano di dare l'errore *"not authorized as a test Kindle"*. `K4-Jailbreak` v1.0.0 è la
stessa procedura con certificati nuovi (validi fino al 2099), quindi evita il problema.
Se per qualche motivo usi il pacchetto vecchio, dopo il jailbreak devi installare anche il keystore
aggiornato `DevCerts-20250419-KeyStore.zip` (thread MobileRead t=225030).

---

## 4. Il jailbreak

1. Estrai `K4-Jailbreak.zip` sul computer.
2. Collega il Kindle via USB. Copia nella **radice** del disco `Kindle` (non dentro `documents/`)
   questi tre elementi presi dallo zip:
   - `data.tar.gz`
   - `ENABLE_DIAGS`
   - la cartella `diagnostic_logs`

   La radice deve apparire così:
   ```
   Kindle/
   ├── data.tar.gz
   ├── ENABLE_DIAGS
   ├── diagnostic_logs/
   ├── documents/
   └── ...
   ```
3. **Espelli** il disco dal computer (Espelli su Mac, "Rimozione sicura" su Windows), poi scollega il cavo.
4. Sul Kindle: **Menu → Impostazioni → Menu → Riavvia (Restart)**.
5. Il Kindle si riavvia in **modalità diagnostica** (schermata testuale con un menu a lettere).
   Muoviti con il pad a 5 direzioni:
   - scegli **`D) Exit, Reboot or Disable Diags`**
   - poi **`R) Reboot System`**
   - poi **`Q) To continue`**

   Per confermare una voce si usa il **tasto sinistro** del pad.
6. Aspetta circa 20 secondi: compare la schermata del jailbreak, poi il Kindle si riavvia da solo
   nella normale interfaccia. Non toccare nulla in questa fase.

### Come verificare che ha funzionato

Nella libreria (Home) compare un libro intitolato **"You are Jailbroken"**. Se c'è, sei dentro.

Se non compare: ricollega il Kindle e controlla che `data.tar.gz`, `ENABLE_DIAGS` e
`diagnostic_logs/` fossero nella radice e non in una sottocartella, poi ripeti dal punto 4.
Il jailbreak è fatto per poter essere rieseguito senza danni.

---

## 5. KUAL (il "lanciatore" di estensioni)

KUAL è un'app che appare come un libro e apre un menu di strumenti (USBNetwork, KOReader e, un giorno, Carbonella).

1. Collega il Kindle e copia `KUAL-KDK-1.0.azw2` nella cartella **`documents/`**.
   - Usa **solo** la versione `KDK-1.0`. La `KDK-2.0` e i file `Update_KUALBooklet_*.bin` sono per i
     Kindle Touch e successivi, e sul K4 fanno solo confusione.
2. Espelli e scollega.
3. Nella libreria apri **KUAL** come se fosse un libro: deve comparire un menu.

Facoltativo: estrai `kual-mrinstaller-1.7.N-r19303.zip` e copia le cartelle `extensions/` e
`mrpackages/` nella radice del Kindle. Da KUAL avrai la voce *Helper → Install MR Packages*, comoda
per installare pacchetti in futuro.

### Se KUAL dà errore

- **"Main class instantiation threw a RuntimeException"**: di solito c'è un vecchio `KUALBooklet.jar`
  nel sistema. Leggi il file `developer/KUAL/work/crash.log` sul disco del Kindle, e segui la
  correzione descritta nella guida kxrz (`tools/fix-kual-booklet/` nel repository
  <https://github.com/kxrz/kindle4-revival>). Leggi lo script prima di lanciarlo.
- **"not authorized as a test Kindle"**: certificati scaduti. Rifai il jailbreak con `K4-Jailbreak`
  v1.0.0 (§4), che li aggiorna.

---

## 6. USBNetwork: SSH sul Kindle (necessario per Carbonella)

USBNetwork trasforma il cavo USB in una scheda di rete: il computer vede il Kindle come
`192.168.15.244` e ci si collega in SSH come `root`. È così che copieremo e lanceremo Carbonella.

### Installazione

1. Estrai `kindle-usbnetwork-0.xx.N-k4.zip` e **leggi `README_FIRST.txt`**: è la fonte ufficiale per la
   tua versione, e ha la precedenza su questa guida se c'è qualche differenza.
2. Copia nella radice del Kindle il file `Update_*.bin` **per K4** indicato nel README (il nome contiene
   `k4`). Nella radice non devono esserci altri file `.bin`.
3. Espelli, scollega, e sul Kindle: **Menu → Impostazioni → Menu → Aggiorna il tuo Kindle (Update Your Kindle)**.
   Conferma, attendi il riavvio.
4. Sul disco del Kindle comparirà una cartella `usbnet/` con la configurazione (`usbnet/etc/`).

### Attivare la rete via USB

1. Dalla Home premi il tasto **Tastiera** per aprire la ricerca e scrivi queste tre righe, una alla volta.
   Sul K4 non c'è un tasto Invio: per confermare, scendi con il pad a 5 direzioni sulla voce di ricerca
   sotto il campo di testo e premi il tasto centrale.
   ```
   ;debugOn
   ~usbNetwork
   ;debugOff
   ```
   La scorciatoia `;un` / `;uns` vale per i Kindle Touch e successivi: sul K4 di solito non fa nulla.
   **Per averla sempre attiva:** rinomina `usbnet/DISABLED_auto` in `usbnet/auto` (via SSH:
   `mv /mnt/us/usbnet/DISABLED_auto /mnt/us/usbnet/auto`). Dal riavvio successivo USBNetwork parte da
   solo e il Kindle non appare più come disco USB. **Attacca il cavo solo dopo che il Kindle ha finito
   di avviarsi**: se è già attaccato durante l'avvio, la rete resta bloccata. Per tornare indietro, rinominalo di nuovo
   (la documentazione del pacchetto è in `usbnet/info.txt`).
   Mentre USBNetwork è attivo, il Kindle **non** appare più come disco USB: è il segnale che ha funzionato.
2. Collega il cavo. Sul computer apparirà una nuova interfaccia di rete (su Mac "RNDIS/Ethernet
   Gadget" o simile, su Linux `usb0`). Assegnale un IP fisso:
   - IP **`192.168.15.201`**, maschera **`255.255.255.0`**, nessun gateway.
   - Su Linux: `sudo ip addr add 192.168.15.201/24 dev usb0 && sudo ip link set usb0 up`
   - Su Mac: Impostazioni di Sistema → Rete → l'interfaccia del Kindle → Dettagli → TCP/IP → Manualmente.
3. Prova: `ping 192.168.15.244`.

### Entrare in SSH

```sh
ssh root@192.168.15.244
```

La password di `root` del K4 è legata al numero di serie. Hai tre strade:

- **Chiave SSH (consigliata):** dopo il primo accesso con password, lancia dal Mac
  `sh strumenti/chiave-ssh.sh`: crea la chiave RSA `~/.ssh/kindle_rsa` (il Dropbear 2015 del K4 non accetta ed25519), la copia in `usbnet/etc/authorized_keys`
  sul Kindle e configura `~/.ssh/config`. Da lì in poi niente password, e basta `ssh kindle`.
- **Calcolatore della password** dal numero di serie annotato al §1 (citato dalla wiki MobileRead):
  <http://www.sven.de/kindle/>.
- **Kindle recuperato con un'immagine di debrick:** la password di solito è `mario`.

Se sei dentro, vedrai un prompt tipo `[root@kindle root]#`. Comandi utili:

```sh
mntroot rw        # rende scrivibile il filesystem di sistema (di base è in sola lettura)
mntroot ro        # lo rimette in sola lettura: fallo sempre quando hai finito
lipc-set-prop -- com.lab126.powerd preventScreenSaver 1   # niente salvaschermo mentre sviluppi
```

> **Da verificare sul tuo Kindle:** il nome esatto del file `Update_*.bin`, la gestione delle chiavi
> SSH e l'opzione per l'SSH via Wi-Fi dipendono dalla versione di USBNetwork. Le fonti online che ho
> potuto leggere non le riportano: fai fede a `README_FIRST.txt` e al file `usbnet/etc/config`.

---

## 7. Dopo il jailbreak: backup vero e blocco aggiornamenti

**Backup del sistema (da fare subito, una volta).** Ora che hai il root, salva un'immagine delle
partizioni: è il tuo paracadute se un giorno Carbonella rompesse qualcosa. Dal computer, con USBNetwork attivo:

```sh
sh strumenti/backup-k4.sh
```

Lo script (`strumenti/backup-k4.sh`) **legge soltanto** dal Kindle e salva in `~/kindle-backup/<data>/`:
la zona di avvio (bootloader e kernel), ogni partizione di sistema (`mmcblk0p1` è il sistema, ecc.),
un `info.txt` con la mappa delle partizioni e i checksum MD5 per verificare che le copie siano integre.
Salta la partizione dei libri, che copi già come disco USB. A mano, il cuore dello script è:

```sh
ssh root@192.168.15.244 "cat /proc/partitions"                   # elenca le partizioni
ssh root@192.168.15.244 "dd if=/dev/mmcblk0p1 bs=1048576" > mmcblk0p1.img
```

Conserva la cartella del backup (e il seriale) in un posto sicuro, meglio in due copie. Non scrivere mai una partizione con `dd`
verso il Kindle senza aver chiesto prima: è lì che si fanno i brick veri.

**Blocco aggiornamenti.** Amazon non pubblica più firmware per il K4, quindi il rischio è minimo.
Se vuoi stare tranquillo, l'estensione KUAL *Block Amazon*
(<https://github.com/Darthagnon/kindle-kual-blockamazon/tags>) blocca i server Amazon. Tieni comunque
il Wi-Fi spento quando non serve.

---

## 8. Se qualcosa va storto

- **Disinstallare il jailbreak:** prima disinstalla i pacchetti (USBNetwork, ecc.), poi copia nella
  radice il file di disinstallazione incluso nello zip del jailbreak (es. `Update_jailbreak_1.8.N_uninstall.bin`)
  e usa **Menu → Impostazioni → Menu → Aggiorna il tuo Kindle**.
- **Il Kindle resta in modalità diagnostica a ogni avvio:** collega il cavo, cancella `ENABLE_DIAGS`
  dalla radice e scegli `D) Exit, Reboot or Disable Diags → D) Disable Diagnostics`.
- **Il Kindle non si avvia più (brick):** non andare nel panico e non provare a caso. Il recupero si fa
  con **Kubrick**, una chiavetta Linux avviabile che riscrive il sistema via USB
  (thread MobileRead <https://www.mobileread.com/forums/showthread.php?t=206064>). Chiedi aiuto prima di usarlo.

---

## Fonti

- KindleModding, *K4 Jailbreak*: <https://kindlemodding.org/jailbreaking/Legacy/K4-Jailbreak/> e <https://github.com/KindleModding/K4-Jailbreak>
- KindleModding, *Installing KUAL & MRPI*: <https://kindlemodding.org/jailbreaking/Legacy/post-jailbreak/installing-kual-mrpi/>
- KindleModding, *Debug commands* (`;un` / `;uns`): <https://kindlemodding.org/kindle-hacking/debug-commands.html>
- MobileRead Wiki, *Kindle4NTHacking* (metodo valido da 4.0.0 a 4.1.4, IP, SSH): <https://wiki.mobileread.com/wiki/Kindle4NTHacking>
- kxrz, *kindle4-revival* (guida 2026, certificati, errori KUAL): <https://github.com/kxrz/kindle4-revival>
- Longplay Tech, *How to Jailbreak the 4th Gen Kindle* (maggio 2026, FW 4.1.4): <https://longplaytech.com/posts/jailbreak-kindle-4th-gen/>
- makerblock, *Jailbreaking a Kindle 4 Non Touch (2012), Take 2* (giugno 2025, FW 4.1.4): <https://makerblock.com/2025/06/jailbreaking-a-kindle-4-non-touch-2012-take-2/>
