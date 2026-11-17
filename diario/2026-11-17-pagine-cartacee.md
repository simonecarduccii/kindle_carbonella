# Pagine dell'edizione cartacea (17 nov 2026, back end)

**Richiesta di Simone (14:05 UTC):** "vorrei sapere quante pagine hanno i libri veri quelli cartacei, sulla
home mostra il numero di pagine vere, dentro le pagine di visualizzazione a schermo".

Quindi due numeri diversi:

- **Home**: le pagine del libro di carta (per esempio 158 per *Mr Gwyn*).
- **Dentro il libro**: le pagine a schermo, come prima. Questo non cambia.

## Da dove vengono le pagine di carta

In quest'ordine (vince l'ultimo che ha un numero):

1. **La lista delle pagine dentro l'EPUB.** Alcuni editori mettono nell'EPUB dove cadono le pagine del
   libro stampato (EPUB 3: `nav epub:type="page-list"`, EPUB 2: `<pageList>` nel file NCX). Carbonella
   la legge da sola e prende il numero di pagina più alto. Il risultato resta in `libreria.tsv` (ottava
   colonna), così non riapre i libri a ogni avvio.
2. **`carbonella/pagine_carta.tsv`**, che arriva con Carbonella: le pagine cercate a mano nei cataloghi
   (editore, IBS, librerie online), con l'edizione da cui vengono.
3. **`carbonella/dati/pagine_carta.tsv`**, facoltativo: le correzioni di Simone. Vale più degli altri e
   non si perde con gli aggiornamenti.

Il formato è una riga per libro: `titolo <TAB> autore <TAB> pagine <TAB> edizione`. Il libro si
riconosce da titolo e autore, non dal percorso del file, quindi le pagine restano anche quando il libro
viene spostato in una raccolta o nell'Archivio. Maiuscole, accenti e punteggiatura non contano.

Se non si trova niente, il numero vale 0 e la Home non mostra le pagine (decide il front end come).

## Nel codice

- `LibEntry.print_pages`: le pagine di carta da mostrare sulla Home (0 = sconosciute).
- `LibEntry.print_edition`: l'edizione da cui vengono, se presa dalla tabella.
- `LibEntry.print_pages_epub`: solo quelle della lista nell'EPUB.
- `book_peek_meta()` legge anche la lista delle pagine; `library_scan()` riceve la cartella base per
  trovare la tabella.
- Alla prima apertura dopo l'aggiornamento Carbonella rilegge tutti i libri una volta, perché
  `libreria.tsv` ha una colonna in più.

## Prove

Tre EPUB finti (uno con `page-list` EPUB 3 con pagine romane e 214 arabe, uno con `pageList` NCX da
160, uno senza) più le due tabelle. Risultato: 214 dalla lista EPUB, 170 dalla tabella in `dati/` che
vince sui 160 dell'EPUB, 400 per "Così parlò Zarathustra" scritto senza accenti nella tabella. Compilato
con AddressSanitizer: nessun errore. `make prova` funziona come prima.

## Libri cercati

| Libro | Pagine | Edizione | Fonte |
| --- | --- | --- | --- |
| Mr Gwyn, Alessandro Baricco | 158 | Feltrinelli, Universale Economica 2012 | [libreriauniversitaria.it](https://www.libreriauniversitaria.it/mr-gwyn-baricco-alessandro-feltrinelli/libro/9788807880018) |

L'EPUB di *Mr Gwyn* dice ISBN 9788807018626 (Feltrinelli, I Narratori 2011), ma per quella edizione
nessun catalogo che ho letto riporta le pagine. Ho usato l'edizione economica, stesso testo.

Tempo: circa 30 minuti.
