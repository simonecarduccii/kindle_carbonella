# Novità di Carbonella

Le versioni seguono lo schema `MAGGIORE.MINORE.CORREZIONE`. Finché siamo a `0.x` tutto può cambiare.

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
