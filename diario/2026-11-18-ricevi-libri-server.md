# Ricevi libri: il server sul Kindle e il QR code (18 nov 2026, back end)

**Richiesta di Simone (15:50 UTC):** "ho bisogno di renderlo facile [...] quando faccio i kindle e li do ai
miei amici anche loro avranno questa necessità", "puoi fare un qrcode generato sul kindle, lo inquadro da
telefono e glieli mando? stessa cosa tenendo il cavo usb". Poi: "ok proviamo a farlo".

Divisione del lavoro: il design ha disegnato le schermate in Figma (sezione "Ricevi libri (18 nov)",
testi in [2026-11-18-ricevi-libri](2026-11-18-ricevi-libri.md)), il front end fa la voce di menu e la
schermata sul Kindle, il back end (questo diario) fa il QR code, il server e la pagina per il telefono.

## Come funziona

1. Sul Kindle si apre "Ricevi libri". Carbonella accende il Wi-Fi se era spento, apre la porta 8080 nel
   firewall del Kindle e mostra un QR code con l'indirizzo, per esempio `http://192.168.1.23:8080/k7f2`.
2. Il telefono inquadra il codice e apre una pagina con il pulsante "Scegli i libri" (testi e aspetto dal
   frame Figma del telefono). Il titolo usa il nome del Kindle: "Manda libri al Kindle di Simone".
3. Ogni libro viaggia da solo (una richiesta HTTP PUT per file) con la barra di avanzamento. Sul Kindle
   arriva prima in un file nascosto `.ricevo-*.part`, e prende il suo nome solo quando è completo: un
   invio interrotto non lascia libri a metà nella Home.
4. All'uscita dalla schermata (Indietro, Home, standby, 15 minuti senza niente) il server si spegne, la
   porta si richiude e il Wi-Fi torna spento se lo era.

## Sicurezza e batteria

- Il server esiste solo mentre la schermata è aperta.
- L'indirizzo ha un codice casuale di 4 caratteri (`/k7f2`): senza quello risponde "Indirizzo non
  valido", quindi un altro telefono sulla stessa rete non può mandare file a caso.
- Accetta solo `.epub` e `.txt`, al massimo 300 MB, solo se c'è spazio. Il nome del file viene pulito:
  niente cartelle, niente `..`, niente caratteri che il disco del Kindle rifiuta. Un libro non
  sovrascrive mai un altro: il secondo diventa "Titolo (2).epub".
- Non si può leggere, scaricare o cancellare niente dal Kindle: c'è solo la pagina e l'invio.
- HTTP semplice, senza HTTPS: il K4 non regge la crittografia moderna e i libri restano nella rete di casa.

## Il cavo USB

- **Sul Kindle di Simone** (USBNetwork attivo) lo stesso server risponde anche sul cavo, a
  `http://192.168.15.244:8080/<codice>`: `rx_url_usb()` lo dà alla schermata. Funziona anche senza Wi-Fi:
  se il Wi-Fi non si accende ma il cavo c'è, "Ricevi libri" parte lo stesso.
- **Sui Kindle degli amici** (senza USBNetwork) il cavo fa vedere il Kindle come un disco solo quando
  Carbonella è chiuso: lì basta trascinare i libri nella cartella `documents`.

## Il Wi-Fi del Kindle (letto dal front end sul Kindle, 15:54 UTC)

- Con Carbonella aperto (framework fermo) `wifid` risponde: vede le reti, ha 2 profili salvati.
- Il firewall accetta su `wlan0` solo le connessioni già aperte: la porta va aperta con
  `/usr/sbin/iptables` (non è nel PATH) e richiusa alla fine. Su `usb0` passa tutto.
- Acceso il Wi-Fi per 15 secondi, il Kindle **non** si è collegato da solo (stato PENDING, nessun
  indirizzo). Da capire sul Kindle: se serve più tempo o un comando `cmConnect`. Senza questo, "Ricevi
  libri" resta su "Accendo il Wi-Fi…". È il prossimo passo, insieme alla scelta della rete con la password
  (richiesta di Simone: anche l'hotspot del telefono).

## Nel codice

- `src/qr.c`: `qr_draw(canvas, x, y, lato, testo)`. Lato totale con il bordo bianco di 4 moduli, moduli
  di pixel interi (bordi netti sull'e-ink). Codificatore: qrcodegen di Project Nayuki (licenza MIT) in
  `third_party/qrcodegen`.
- `src/ricevi.c`: `rx_start`, `rx_step` (non si blocca mai, un evento alla volta), `rx_stop` (sicuro
  sempre), `rx_url`, `rx_url_usb`, `rx_error`, `rx_wifi_state` (0 spento, 1 acceso senza rete,
  2 collegato) anche per l'icona Wi-Fi nell'intestazione che Simone ha chiesto. Nel simulatore
  l'indirizzo è finto e arriva un libro finto, per le schermate di `make prova`.

## Prove (nel cloud)

Con un programmino di prova e `curl`, compilato con AddressSanitizer:

- pagina con il nome del Kindle (con `<` e `>` resi innocui); codice sbagliato: 403;
- un `.pdf`: rifiutato con il testo di errore; un `.txt` mandato due volte: `a.txt` e `a (2).txt`;
- nome `../../etc/passwd.txt`: salvato come `passwd.txt` dentro i libri;
- un EPUB di 3 MB insieme a un altro file: arrivano tutti e due, il primo identico byte per byte
  (avanzamento ogni 5%);
- invio interrotto a metà: nessun file rimasto, evento "Il telefono ha interrotto l'invio.".
- La pagina vista come su un iPhone 13 (Chromium): stati iniziale e finale come nel frame Figma.
- Compila per ARM (zig). **Non ancora provato sul Kindle**: Wi-Fi, firewall e telefono vero.

Tempo: circa 45 minuti.

## Scegliere la rete dentro "Ricevi libri" (16:00–16:15 UTC)

Simone, davanti allo stato che rimandava alle Impostazioni: "non farmi tornare indietro, voglio selzionare
qui il wifi". Il design ha disegnato gli stati 5–9 (scegli la rete, nessuna rete, password, mi collego,
password sbagliata): testi in [2026-11-18-scegli-rete-wifi](2026-11-18-scegli-rete-wifi.md).

Prove sul Kindle fatte dal front end (`/mnt/project-files/carbonella/wifi-kindle-2-2026-11-18.md`):

- Con Carbonella aperto, `com.lab126.cmd wirelessEnable 1` **non** accende la radio: wifid dice
  "Wireless: Off" e `wlan0` non esiste. Ieri, con l'interfaccia Amazon accesa, bastava.
- `cmConnect` su una rete salvata non fa niente finché la radio è spenta.
- L'elenco delle reti si legge con `lipc-hash-prop -n com.lab126.wifid scanList` nel formato
  `{ essid = "…", secured = "yes", known = "no", signal = (1), signal_max = (5) }`.

`src/wifi.c` (nuovo): `wifi_radio_on` (accende anche `com.lab126.wifid enable`), `wifi_scan`,
`wifi_list` (reti salvate prima, poi per segnale; segnale in 3 gradini Forte/Medio/Debole; stesso nome
una volta sola), `wifi_connect` (rete salvata, aperta o con password: la password va a wifid con
`createProfile` attraverso un file temporaneo in RAM cancellato subito, mai nel log), `wifi_connect_poll`
(collegato / password sbagliata / non riesco; dopo 45 s senza indirizzo una rete appena aggiunta viene
dimenticata e dice "password sbagliata"). Nel simulatore: le reti del frame Figma, e "TIM-38492011" dà
password sbagliata, per le schermate.

Provato nel cloud: lettura dell'elenco (virgolette nel nome, doppioni, nome vuoto), AddressSanitizer
pulito. **Da verificare sul Kindle**: che `com.lab126.wifid enable 1` accenda davvero la radio, il formato
di `createProfile` (sono le chiavi che usa KOReader sui Kindle) e cosa dice `cmState` con una password
sbagliata.

Terza prova del front end (16:05 UTC): `lipc-set-prop -i com.lab126.wifid enable 1` accende davvero la
radio (wifid "On", compare `wlan0`, parte `wpa_supplicant`). Per spegnerla `enable 0` è servito due volte.
Quindi ora Carbonella decide se il Wi-Fi è acceso guardando se `wlan0` esiste (non `wirelessEnable`, che
può dire 1 a radio spenta), e all'uscita ripete lo spegnimento se `wlan0` c'è ancora. Il collegamento
vero a una rete non è ancora stato provato: l'hotspot di Simone non era visibile.

## Le reti restano salvate (16:55 UTC)

Simone: "salva sempre le password del wifi in modo che resti connesso".

- La password scritta diventa un profilo di wifid (il servizio Wi-Fi del Kindle), che la conserva anche
  dopo il riavvio. Viene tolta solo se il primo collegamento con quella password fallisce: così una
  password sbagliata non resta salvata.
- Con l'interfaccia Amazon ferma nessuno chiede a wifid di collegarsi da solo: ora lo fa Carbonella. Circa
  6 secondi dopo aver acceso la radio, se non c'è ancora un indirizzo, sceglie la rete salvata con il
  segnale migliore e si collega (`wifi_autoconnect_tick`, chiamata da sola da `rx_step` e
  `rx_wifi_state`).
- `wifi_connected_essid()`: il nome della rete a cui il Kindle è collegato, per segnarla nell'elenco.

Da verificare sul Kindle con una rete salvata in portata.
