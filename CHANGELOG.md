# Novità di Carbonella

Le versioni seguono lo schema `MAGGIORE.MINORE.CORREZIONE`. Finché siamo a `0.x` tutto può cambiare.

## v0.3.0 · 2026-11-17 · "Appunti di Simone"

- **Sfondo bianco puro** ovunque (prima era il grigio "Carta" #EEE): sull'e-ink il bianco appare
  comunque un po' grigio, ma il contrasto è il massimo possibile.
- **Il menu di lettura si apre sopra la pagina**: il testo resta visibile sopra al pannello, con il
  titolo del libro in alto. Cambiando la dimensione del carattere si vede subito la pagina nuova.
- **Pagina Impostazioni** come nel frame Figma "04 · Impostazioni", dalla Home e dal menu di lettura:
  - *Lettura*: obiettivo giornaliero, libri da leggere quest'anno;
  - *Aspetto*: carattere (Literata), dimensione, interlinea (1,3–1,8), margini (stretti, medi, larghi),
    refresh completo (ogni 1–20 pagine), visualizzazione (copertine);
  - pagina 2: ordina per, aggiorna l'elenco dei libri, prova dei tasti, versione.
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
