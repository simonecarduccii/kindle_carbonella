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

L'elenco dei libri sul Kindle è arrivato dal front end alle 18:34 UTC
(`/mnt/project-files/carbonella/libri-sul-kindle-2026-11-17.md`). Ho cercato le pagine dell'edizione
italiana dello stesso editore dell'EPUB, preferendo il sito dell'editore.

| Libro | Pagine | Edizione | Fonte |
| --- | --- | --- | --- |
| Mr Gwyn, Alessandro Baricco | 158 | Feltrinelli, Universale Economica 2012 | [libreriauniversitaria.it](https://www.libreriauniversitaria.it/mr-gwyn-baricco-alessandro-feltrinelli/libro/9788807880018) |
| Il mito di Sisifo, Albert Camus | 144 | Bompiani, Tascabili 2001 | [bompiani.it](https://www.bompiani.it/catalogo/il-mito-di-sisifo-9788845246425), uguale su IBS, Hoepli, Libraccio |
| La morte della Pizia, Friedrich Dürrenmatt | 68 | Adelphi, Piccola Biblioteca 216 | [adelphi.it](https://www.adelphi.it/libro/9788845902963) (stesso ISBN dell'EPUB) |
| Follia, Patrick McGrath | 296 | Adelphi, gli Adelphi 408 | [adelphi.it](https://www.adelphi.it/libro/9788845926983) (libreriauniversitaria dice 304: vale l'editore) |
| Così parlò Zarathustra, Friedrich Nietzsche | 425 | Adelphi, Piccola Biblioteca 36 | [adelphi.it](https://www.adelphi.it/libro/9788845906558): "pp. XVII-425", conto le 425 arabe |
| 1984, George Orwell | 321 | Mondadori, Oscar Moderni Cult 2019, trad. Nicola Gardini | [ibs.it](https://www.ibs.it/1984-libro-george-orwell/e/9788804719137) |

**Dubbi:**

- *Mr Gwyn*: l'EPUB è dell'edizione I Narratori 2011, ma nessun catalogo letto ne dà le pagine. Ho usato
  l'edizione economica, che ha lo stesso testo.
- *1984*: prima avevo messo 336 (Mondadori 2015, traduzione Manferlotti). Il front end ha letto il
  frontespizio dell'EPUB: "Nuova traduzione e postfazione di Nicola Gardini", © 2019. Il cartaceo con
  quella traduzione è l'Oscar Moderni Cult del 2019 (ISBN 9788804719137): 321 pagine secondo IBS.
  L'ebook su mondadori.it dice 348 pagine, ma sono pagine dell'ebook, non del libro di carta.
- *I miei ritagli* e *You are Jailbroken* non sono libri: niente pagine.

Il log sul Kindle dopo l'installazione del front end (e14f54f, 18:30 UTC) diceva "gives paper pages for 2
books": erano le due copie di *Mr Gwyn*. Con questa tabella diventano 8.

Tempo: circa 30 minuti per il codice, 20 per la ricerca delle pagine.

## Libri caricati da Simone: "Pagine del cartaceo" (18:45 UTC)

Simone: "Ma quando caricherò io in autonomia un libro come fa la ricerca?". Ho proposto (1) uno script sul
Mac che cerca le pagine nei cataloghi quando copia il libro e (2) una voce nel menu del libro per scriverle
a mano. Simone: "2 mi piace". Niente script per ora.

Lato back end c'è `print_pages_set(data_dir, entry, pagine)`: scrive o sostituisce la riga del libro in
`dati/pagine_carta.tsv` (che vale più di tutto) con la nota "scritto a mano sul Kindle". Con 0 la riga si
toglie e tornano a valere la tabella di Carbonella o la lista dell'EPUB, dopo una nuova lettura della
libreria. La voce di menu la disegna il front end.

Prova: scritte 250 pagine per un libro, 430 e poi 440 per un altro (resta una riga sola, con 440), poi
tolta l'ultima: torna il 425 della tabella. AddressSanitizer pulito.
