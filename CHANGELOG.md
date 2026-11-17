# Novità di Carbonella

Le versioni seguono lo schema `MAGGIORE.MINORE.CORREZIONE`. Dalla 1.0.0 le versioni MINORE aggiungono cose, le CORREZIONE sistemano.

## v1.0.4 · 2026-11-17

Le nuove schermate di Simone in Figma.

- **Conferma prima di archiviare**: "Sei sicuro di voler archiviare?" con *Si* (nero) e *no*, come nel
  frame "00 Home Menù Archivio". Destra/sinistra cambiano scelta, Indietro vale no.
- **Archivio nelle Impostazioni** (pagina 2, gruppo ARCHIVIO sopra SISTEMA): *Mostra archivio* apre la
  griglia dei libri archiviati; *Elimina archivio definitivamente* chiede conferma e cancella i file
  dell'archivio. L'archivio non compare più tra le raccolte della Home.
- Nell'archivio la voce *Archivia libro* diventa **Togli dall'archivio**: il libro torna nella Home.
- **Copertine**: titolo più grande e autore più piccolo in tutti e sei i modelli (anche in Figma).

## v1.0.3 · 2026-11-17

- **Standby leggero, risvegliabile con Home.** Prova sul Kindle: nello standby vero di powerd la
  tastiera del K4 è spenta (nessun interrupt), quindi Home non può arrivare. Ora lo *Standby automatico*
  prima mostra la copertina restando sveglio: qualsiasi tasto, Home compreso, riporta la pagina. Dopo
  3 minuti senza tasti parte lo standby vero (sonno profondo, si sveglia solo con il tasto di
  accensione). Il tasto di accensione manda sempre subito nello standby vero.
- Modello **Feltrinelli** aggiornato alla reference di Simone: illustrazione quadrata 496×496 più
  chiara (#AAA), autore 38 px, marchio "Universale Economica Feltrinelli" a 15 px.
- **Titoli e autori normalizzati** per tutti i libri, anche quelli già in libreria: niente note tra
  parentesi nel titolo, autore sempre "Nome Cognome". "Calvino, Italo" diventa "Italo Calvino"; se il
  file si chiama "Nome Cognome - Titolo.epub" vale l'ordine del file; altrimenti, se l'ultima parola è
  un nome proprio noto e la prima no ("Camus Albert"), si inverte.
- Tolto il tentativo della v1.0.1 (Home nello standby vero), che non poteva funzionare.

## v1.0.2 · 2026-11-17

- Titoli più puliti: le note tra parentesi alla fine del titolo dell'EPUB vengono tolte, per esempio
  "Così parlò Zarathustra (Piccola biblioteca Adelphi) (Italian Edition)" diventa "Così parlò Zarathustra".

## v1.0.1 · 2026-11-17

- **Il tasto Home risveglia il Kindle** dallo standby, come il tasto di accensione (Simone). Funziona
  finché il Kindle è nello stato "screen saver"; nel sonno profondo il processore è spento e lo
  risveglia solo il tasto di accensione.

## v1.0.0 · 2026-11-17 · "Carbonella"

La prima versione completa: le copertine sono tutte nel formato dello schermo e ogni libro ha la sua.

- **Copertine 600×800 (3:4)**: i sei modelli di Simone (Sellerio, Adelphi, Einaudi, Feltrinelli, Bompiani,
  Mondadori) sono disegnati alla misura dello schermo del Kindle, come i componenti "Copertine 600×800"
  in Figma. In standby la copertina riempie lo schermo 1:1, senza bande ai lati.
  - Il blocco "Illustrazione" segue l'auto layout di Figma: se il titolo va su più righe, l'illustrazione
    si sposta o si accorcia.
- **Home e raccolte nel nuovo formato**: copertine 172×229 (prima 172×249), celle 181×262, la pila delle
  raccolte con copertine 168×224. Come nel frame Home di Figma.
- **Grigi a contrasto costante**: ogni libro sposta tutti i grigi della sua copertina (sfondo, pannelli,
  illustrazione e testo insieme) di 0, 1 o 2 gradini dell'e-ink (17 su 255). Il contrasto tra sfondo e
  testo resta lo stesso. Sellerio scurisce, gli altri schiariscono, Bompiani resta bianco.
- **Illustrazioni generate**: nel rettangolo "Illustrazione" di ogni modello c'è un disegno scelto dal
  titolo e dall'autore, sempre uguale per lo stesso libro. Otto motivi: cerchi, strisce, punti, sole,
  montagne, onde, blocchi, scacchi. Il disegno usa il grigio del rettangolo spostato di 4 gradini,
  l'accento di 8.
- **Disegno più veloce**: dischi e poligoni ora si riempiono riga per riga (stessa qualità, 4×4
  campioni). Una copertina a tutto schermo passa da 2–4 s a 0,2–0,6 s sotto qemu.
- `--tavola FILE`: salva una tavola di prova con 30 copertine generate (modello, motivo e gradino sotto
  ciascuna).

## v0.6.3 · 2026-11-17

- **Nuovo modello di copertina: Mondadori**, disegnato da Simone direttamente a 600×800. Testa con il
  logo tondo e "Oscar / Libri", autore e titolo in Libre Baskerville, illustrazione #666, "MONDADORI" in
  fondo. Ora i modelli sono sei: anche i libri senza editore noto possono ricevere Mondadori, quindi
  alcune copertine cambiano modello.

## v0.6.2 · 2026-11-17

- Pannello delle raccolte (*Aggiungi alla raccolta*): bordo di 1 px #222 intorno, come nel frame di Simone.
- Nuova icona della batteria nell'intestazione: corpo 24×13 con bordo di 2 px e angoli arrotondati, carica 18×7, polo 2×6.

## v0.6.1 · 2026-11-17

- *Standby automatico dopo*: sul Kindle 4 powerd non ha la proprietà `powerButton` che usava la v0.6.0.
  Ora la pressione del tasto di accensione viene simulata con `powerd_test -p`, lo strumento di prova
  di powerd che c'è già sul Kindle.

## v0.6.0 · 2026-11-17

Le modifiche di Simone in Figma e le copertine dai suoi modelli di editori.

- **Copertine**: ogni libro è disegnato con uno dei cinque modelli di Simone (Frame 6 in Figma): Sellerio,
  Adelphi, Einaudi, Feltrinelli, Bompiani, con il suo titolo e il suo autore.
  - Se l'editore scritto nell'EPUB (`dc:publisher`) è uno dei cinque, si usa il suo modello, con il logo
    come in Figma.
  - Altrimenti il modello è scelto da titolo e autore, sempre lo stesso per lo stesso libro, e al posto
    del logo c'è il nome del vero editore (vuoto se l'EPUB non lo dice).
  - Le copertine generate (23 stili) non ci sono più.
  - Nuovi caratteri (SIL OFL, ridotti all'alfabeto latino): Libre Bodoni, Libre Baskerville (normale e
    corsivo), IBM Plex Mono Bold.
  - Il file `dati/libreria.tsv` ha una colonna in più (l'editore): al primo avvio i libri vengono riletti.
- **Crea nuova raccolta**: bordo tratteggiato (6 px pieni, 6 vuoti) nel menu della Home e nel pannello
  delle raccolte.
- **Segnalibro**: nuova linguetta, un angolo piegato in due grigi (#AAA sopra, #CCC sotto).
- **Avviso obiettivo**: il quadratino accanto a "30 min oggi" è nero.
- **Impostazioni, pagina 2**: nuova voce *Standby automatico dopo* (‹ 5 m ›, si sceglie fra 1, 2, 3, 5 e
  10 minuti). Dopo quel tempo senza tasti Carbonella chiede lo standby al Kindle, e compare la copertina.
- **Home**:
  - La selezione è una cornice nera quadrata intorno al libro, e la copertina non si sposta più.
  - Etichetta **NUOVO** nera per i libri mai aperti.
  - Sotto la copertina c'è solo il numero delle pagine, senza "pagine".
- **Lettura**: il numero "12 di 684" è grafite (#222), e la traccia della barra è #999.

## v0.5.3 · 2026-11-17

Piccole modifiche di Simone in Figma ("ho fatto dei minimi cambiamenti aggiorna tutto"). Le ho trovate
confrontando i frame con la copia congelata delle 09:36 nella pagina Evoluzione.

- La barra di avanzamento in lettura (e nel menu di lettura) ora è nera, prima era grafite.
- La schermata Obiettivi non ha più "Pagina 1 di …" in fondo.
- Le copertine della Home hanno cambiato di 1 px in Figma: sono arrotondamenti, non portati.

## v0.5.2 · 2026-11-17

- **Standby di nuovo funzionante**: premendo il tasto di accensione compare la copertina del libro.
  - Il problema: `carbonella.sh` metteva `preventScreenSaver` a 1, e con quello il Kindle ignora il tasto
    di accensione ("Prevent screen saver set. Ignoring power button" in /var/log/messages).
  - Ora resta a 0. Mentre leggi, il lettore rinvia lo standby automatico al massimo una volta al minuto
    (`touchScreenSaverTimeout`).
- Il "watcher" degli eventi di standby legge un evento alla volta, così non resta bloccato nel buffer
  della pipe.
- All'avvio vengono chiusi i watcher rimasti dagli avvii precedenti (ne erano rimasti 8).

## v0.5.1 · 2026-11-17

Simone ha aggiunto una voce al menu della Home mentre installavo la v0.5.0.

- **Menu della Home**: nuova voce *Archivia libro* (›), prima di *Tutte le impostazioni*. Sposta il libro
  con il riquadro nella raccolta "Archivio", creata se non c'è. Il libro lascia la Home ma resta sul
  Kindle e si riprende con *Aggiungi alla raccolta*.
- *Salva come finito* ora ha la freccia (›) invece del +, come nel frame.

## v0.5.0 · 2026-11-17 · "Obiettivi"

Simone ha migliorato i frame in Figma ("ho migliorato tutto, aggiorna ora"). Riportati parola per parola.

- **Nuova schermata Obiettivi** (frame "06 · Obiettivi"), da Impostazioni → *Guarda gli obiettivi*:
  - minuti letti oggi, barra verso l'obiettivo, "mancano N min";
  - giorni di fila con l'obiettivo raggiunto, record, minuti della settimana;
  - calendario del mese: nero = raggiunto, grigio = letto meno, riquadro = oggi (si riempie dal basso);
  - libri finiti quest'anno su quelli da leggere, "in linea" o "in ritardo", barra;
  - *Imposta*: Obiettivo giornaliero e Libri da leggere nell'anno, con sinistra/destra.
- **Lettura**: quando l'obiettivo del giorno è raggiunto, in alto al centro compare "■ 30 min oggi"
  (frame "02 · Lettura · obiettivo raggiunto").
- **Menu della Home**: nuovo ordine, *Crea nuova raccolta* in cima.
- **Aggiungi alla raccolta**: ora è un piccolo pannello al centro, sopra la Home sbiadita (frame
  "00 Home Menù Raccolta"), con *Crea nuova raccolta* e l'elenco delle raccolte.
  "Nessuna raccolta" non c'è più, perché non è nel design.
- **Impostazioni**:
  - pagina 1: *Guarda gli obiettivi*, *Obiettivo giornaliero*, *Libri da leggere nell'anno*, poi Aspetto;
  - i valori sono tutti "‹ valore ›";
  - interlinea scritta come "1,5 pt";
  - tolta la riga *Refresh completo*, che non è più nel frame (la chiave `lampeggio_ogni` resta nel file
    di configurazione);
  - nell'intestazione non ci sono più i minuti di oggi, come nel frame.

## v0.4.1 · 2026-11-17

Simone ha cambiato il menu della Home in Figma mentre preparavo la v0.4.0: ora è come il nuovo frame.

- **Menu della Home**: Ordina per ‹ Più recente ›, Obbiettivo giornaliero ‹ 30 m › (si cambiano con
  sinistra/destra), Crea nuova raccolta, Aggiungi alla raccolta, Salva come finito, Tutte le impostazioni.
  - *Aggiungi alla raccolta* e *Salva come finito* valgono per il libro con il riquadro nella Home.
  - Tolte "Sposta libri" e "Aggiungi libro da leggere" (non sono più nel design).
- Dimensione carattere nel menu di lettura sempre come "‹ 20 pt ›", come nel frame.
- Nel simulatore la batteria è al 100%, come nei frame aggiornati.

## v0.4.0 · 2026-11-17 · "I menu di Figma"

Simone: *"perché se in Figma il menù ha delle scritte tu le devi cambiare"*. Da ora i testi e l'ordine
delle voci sono esattamente quelli dei frame Figma, parola per parola.

- **Menu della Home** (come "00 Home Menù"): Ordina per, Obbiettivo giornaliero, Crea nuova raccolta,
  Sposta libri, Aggiungi libro da leggere, Impostazioni.
  - *Crea nuova raccolta*: il nome si scrive con la tastiera sullo schermo; la raccolta compare nella
    Home anche vuota.
  - *Sposta libri*: scegli il libro, poi la raccolta (o "Nessuna raccolta", o una nuova). La posizione
    di lettura segue il libro.
  - *Aggiungi libro da leggere*: tra i libri non ancora iniziati, segna quelli da leggere; nella Home
    sotto la copertina compare "Da leggere".
- **Menu di lettura** (come "02 · Lettura Menù"): Aggiungi segnalibro, Cerca nel libro, Dimensione
  carattere, Vai a pagina… (con "pag. 12 di 684"), Segnalibri (con il numero), Indice.
  - *Segnalibro*: l'angolo piegato in alto a destra, come in "02 · Lettura Segnalibro". Sulla stessa
    pagina, "Aggiungi segnalibro" lo toglie.
  - *Cerca nel libro*: tastiera sullo schermo, poi l'elenco dei punti trovati con la pagina.
  - *Vai a pagina…*: il numero si cambia con le frecce (1 o 10 pagine) e con i tasti laterali (100).
- **Tastiera sullo schermo** per il 5-way: lettere, accentate, punteggiatura, Spazio, Cancella, Fatto.
- **Impostazioni**: testi come nei frame (anche "Obbiettivo giornaliero", "Torna al kindle"); tolte le
  voci che avevo aggiunto io (Prova dei tasti, Aggiorna l'elenco dei libri).
- Tolte dai menu le voci che non sono in Figma: Pulisci lo schermo, Torna alla Home e Torna al Kindle
  (restano il tasto Home e "Torna al kindle" nelle Impostazioni).

## v0.3.1 · 2026-11-17

- **La Home compare subito**: con alcune copertine (lo stile "Zigzag") il primo disegno della Home
  richiedeva circa 7 secondi sul Kindle. Le linee a zigzag ora si disegnano un tratto alla volta:
  stesso risultato, circa 12 volte più veloce.
- Pagina 2 delle Impostazioni come il frame Figma (gruppo Sistema) e "1 pagina" al singolare.

## v0.3.0 · 2026-11-17 · "Appunti di Simone"

- **Sfondo bianco puro** ovunque (prima era il grigio "Carta" #EEE): sull'e-ink il bianco appare
  comunque un po' grigio, ma il contrasto è il massimo possibile.
- **Il menu di lettura si apre sopra la pagina**: il testo resta visibile sopra al pannello, con il
  titolo del libro in alto. Cambiando la dimensione del carattere si vede subito la pagina nuova.
- **Pagina Impostazioni** come nel frame Figma "04 · Impostazioni", dalla Home e dal menu di lettura:
  - *Lettura*: obiettivo giornaliero, libri da leggere quest'anno;
  - *Aspetto*: carattere (Literata), dimensione, interlinea (1,3–1,8), margini (stretti, medi, larghi),
    refresh completo (ogni 1–20 pagine), visualizzazione (copertine);
  - pagina 2 (*Sistema*): nome dispositivo, Wi-Fi acceso/spento, data e ora, informazioni, torna al Kindle;
    *Strumenti*: prova dei tasti, aggiorna l'elenco dei libri.
  Su/giù per scegliere, centro o destra per il valore successivo, sinistra per il precedente,
  Indietro per uscire. Tutto viene salvato in `dati/preferenze.conf`.
- Il menu della Home ha la voce "Impostazioni"; le altre voci restano.

## v0.2.0 · 2026-11-17 · "Come in Figma"

Tutte le schermate rifatte dal design Figma, con le stesse misure, gli stessi font e gli stessi grigi.

- **Home a copertine**: 3 colonne con la terza riga tagliata a metà, si scorre per righe. Ogni libro ha
  una copertina generata (23 stili del design, sempre la stessa per lo stesso libro), con percentuale e pagine.
- **Raccolte**: i libri finiti stanno nella pila "Libri letti"; ogni sottocartella di `documents`
  diventa una pila con il suo nome.
- **Intestazione**: nome del Kindle (`nome` nelle impostazioni), minuti letti oggi rispetto
  all'obiettivo, ora e batteria.
- **Menu a pannello** dal basso, nella Home (ordine, obiettivo giornaliero, aggiorna, torna al Kindle)
  e nella lettura (indice, dimensione carattere con ‹ ›, pulizia schermo, Home, Kindle).
- **Pagina di lettura** come nel design: margini 48, interlinea 1,5, in basso "12 di 684" e la barra
  di avanzamento del libro.
- **Standby**: quando il Kindle va in standby, la copertina del libro a schermo intero; al risveglio
  si torna alla stessa pagina.
- Font nuovi: IBM Plex Mono, IBM Plex Sans Medium, Literata SemiBold (SIL OFL).
- `make prova` usa una libreria finta con i libri del design, per confrontare le schermate con Figma;
  `carbonella --copertine` mostra i 23 stili.

## v0.1.1 · 2026-11-16

Correzioni per il primo avvio sul Kindle vero (da KUAL Carbonella non si apriva).

- **Causa trovata**: il BusyBox del K4 (1.7.2) non ha `nohup`, quindi lo script di KUAL falliva subito
  senza lasciare traccia. Ora l'avvio usa `setsid` se c'è, altrimenti un metodo che funziona con la sola shell.
- L'avvio si stacca da KUAL e lo script ignora i segnali mandati
  quando si ferma l'interfaccia Amazon: prima poteva venire chiuso insieme a KUAL.
- Prima di fermare l'interfaccia Amazon controlla che il lettore parta davvero su questo Kindle
  (`carbonella --versione`): se no, non tocca niente e scrive l'errore nel registro.
- Ogni pressione di "Apri Carbonella" lascia una riga in `carbonella/avvio.log`; il registro
  `carbonella.log` ora ha l'ora di ogni passo.
- Nuovo `make diagnosi` (`strumenti/diagnosi-kindle.sh`): raccoglie in un file tutto quello che serve per capire un problema.
- La riga in basso mostra il capitolo giusto anche quando più capitoli stanno nello stesso file.

## v0.1.0 · 2026-11-16 · "Prima brace"

Prima versione da installare e provare sul Kindle 4.

**Cosa fa**

- **Libreria**: elenco dei libri `.epub` e `.txt` nella cartella `documents` (anche nelle sottocartelle),
  con titolo, autore e percentuale letta; in cima quelli letti di recente.
- **Lettura EPUB e TXT** con un motore scritto da noi: capitoli, titoli, corsivo e grassetto,
  interruzioni di scena, indice del libro.
- **Impaginazione da libro**: testo giustificato, rientro della prima riga, **sillabazione italiana**
  (va a capo dividendo le parole in sillabe), un titolo non resta mai da solo in fondo alla pagina.
- **Font Literata** (pensato per leggere sugli schermi), 7 dimensioni da 14 a 28 px, 18 px predefinita.
- **Riprende dove avevi lasciato**: posizione salvata ogni 5 pagine e all'uscita.
- **Menu di lettura**: indice, testo più grande/più piccolo, pulizia dello schermo, libreria, ritorno al Kindle.
- **Schermo e-ink**: aggiornamento veloce a ogni pagina e "lampeggio" di pulizia ogni 6 pagine.
- **Convive con il Kindle originale**: si apre da KUAL, ferma l'interfaccia Amazon mentre leggi e la fa
  ripartire quando esci (anche se Carbonella si blocca). Dopo 20 minuti senza toccare tasti torna al
  Kindle da sola, così il Kindle può andare in standby.
- **Registro delle letture** (`dati/letture.tsv`): inizio, fine e pagine di ogni sessione, base per la
  futura schermata *Obiettivi*.
- **Prova dei tasti** dal menu KUAL, per controllare che ogni tasto faccia la cosa giusta.

**Limiti noti**

- Le immagini dentro gli EPUB non vengono mostrate (le pagine di sola immagine si saltano).
- Niente copertine né griglia a 3 colonne: la libreria è un elenco. Il design di Figma arriva nelle
  prossime versioni.
- I libri con DRM di Amazon (`.azw`, `.kfx`) non si possono aprire: servono EPUB senza DRM.
- I numeri di pagina sono quelli di Carbonella (per capitolo), non ancora quelli dell'edizione cartacea.
- Da verificare sul Kindle vero: codici dei tasti, lampeggio di pulizia, comportamento del tasto di
  accensione mentre Carbonella è aperta.
