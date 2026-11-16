# Pagine dell'edizione cartacea

Specifica di design · stato: **proposta** · 2026-11-16

## Obiettivo

Mentre leggo su Carbonella voglio sapere a che pagina sarei nel **libro di carta**, e quante pagine ha
in tutto, per farmi un'idea della lunghezza vera del libro. Esempio in barra di stato:

```
pag. 142 di 384      ← numero esatto, preso dall'EPUB
~pag. 142 di 384     ← stima, quando l'EPUB non ha le pagine cartacee
```

Il numero di "schermate" del Kindle (che cambia con la dimensione del carattere) resta disponibile, ma
non è più l'unico riferimento.

## Due fonti, in ordine di priorità

| # | Fonte | Precisione | Dove si calcola | Rete |
|---|-------|-----------|-----------------|------|
| 1 | Elenco pagine dentro l'EPUB (`page-list` / `page-map` / `pageList`) | esatta | sul Kindle, all'apertura del libro | no |
| 2 | Numero di pagine da Open Library tramite ISBN, distribuito in proporzione sul testo | stima (`~`) | pagine totali sul computer, posizione sul Kindle | sì, ma solo sul computer |
| 3 | Nessuna delle due | — | — | — |

Nel caso 3 Carbonella mostra le schermate (`12 / 530`) o la percentuale, come oggi.

Il Kindle **non si connette mai**: tutto quello che richiede internet si fa sul computer, quando si
carica il libro, e finisce in un piccolo file accanto all'EPUB (vedi [File di accompagnamento](#file-di-accompagnamento-sidecar)).

## 1 · Pagine vere dall'EPUB

Molti EPUB recenti, soprattutto quelli prodotti dagli editori insieme alla stampa (e quelli
accessibili, che lo richiedono), segnano nel testo dove inizia ogni pagina dell'edizione cartacea.

### Dove si trovano

**EPUB 3.** Nel documento di navigazione (`nav.xhtml`) c'è un elenco dedicato:

```html
<nav epub:type="page-list" hidden="">
  <ol>
    <li><a href="cap01.xhtml#p1">1</a></li>
    <li><a href="cap01.xhtml#p2">2</a></li>
    …
    <li><a href="cap12.xhtml#p384">384</a></li>
  </ol>
</nav>
```

e nel testo ci sono i segnaposto corrispondenti:

```html
<span epub:type="pagebreak" role="doc-pagebreak" id="p142" aria-label="142"/>
```

Spesso l'OPF dichiara anche da quale edizione vengono le pagine:

```xml
<dc:source id="src">urn:isbn:9788806xxxxxxx</dc:source>
<meta refines="#src" property="source-of">pagination</meta>
```

**EPUB 2.** Due varianti, da controllare entrambe:

- `pageList` dentro il file NCX (`toc.ncx`):
  ```xml
  <pageList>
    <pageTarget id="p142" type="normal" value="142" playOrder="150">
      <navLabel><text>142</text></navLabel>
      <content src="cap05.xhtml#p142"/>
    </pageTarget>
  </pageList>
  ```
- `page-map.xml` (formato Adobe), indicato nell'OPF con `<spine page-map="map">` e
  `<item id="map" media-type="application/oebps-page-map+xml">`:
  ```xml
  <page-map>
    <page name="142" href="cap05.xhtml#p142"/>
  </page-map>
  ```

Ordine di ricerca: `nav` EPUB 3 → `pageList` NCX → `page-map.xml`. Il primo trovato vince.

### Come si usa

1. **All'apertura** del libro, Carbonella legge l'elenco e ottiene una lista di coppie
   *(etichetta, file, ancora)*, per esempio `("142", "cap05.xhtml", "p142")`.
2. **Durante l'impaginazione** il parser XHTML, quando incontra un elemento con quell'`id`, registra la
   posizione nel flusso di testo: *(indice del capitolo nello spine, offset in caratteri)*. Se un'ancora
   non viene trovata (EPUB malfatti), si usa l'inizio del file.
3. Il risultato è una tabella ordinata per posizione, in memoria:
   ```c
   struct print_page {
       uint16_t spine;      /* capitolo */
       uint32_t offset;     /* carattere di inizio nel capitolo */
       char     label[8];   /* "142", "xii", … */
   };
   ```
   Per un libro di 400 pagine sono circa 6 KB: nessun problema per il Kindle 4.
4. **A ogni cambio pagina** si prende la posizione del primo carattere della schermata e si cerca con
   una ricerca binaria l'ultima pagina cartacea che inizia prima: quella è la pagina corrente.

### Totale e casi particolari

- **"di Y"**: è l'ultima etichetta in numeri arabi dell'elenco (le pagine finali in romano, se ci
  sono, si ignorano).
- **Numeri romani** (premessa, introduzione): si mostrano così come sono, `pag. xii di 384`.
- **Etichette non numeriche** (es. "Tavola 3", "A-1"): si mostrano come sono; non entrano nel calcolo
  del totale.
- **Pagine bianche** o saltate: compaiono come salti di numero (da 40 a 42). Va bene così, è quello che
  succede anche sulla carta.
- **Una schermata contiene due inizi di pagina**: mostriamo la pagina in cui **inizia** la schermata.
  In alternativa si potrebbe mostrare `pag. 142–143` (vedi [Domande aperte](#domande-aperte)).

## 2 · Stima quando l'EPUB non ha le pagine

### Sul computer: trovare il numero di pagine

Uno script da riga di comando, `tools/carbonella-pagine` (Python 3, nessuna dipendenza oltre alla
libreria standard), che si lancia quando si copia un libro sul Kindle:

```
carbonella-pagine libro.epub            # cerca e scrive libro.carbonella
carbonella-pagine libro.epub --pagine 384   # numero inserito a mano, nessuna ricerca
```

Passi:

1. **Se l'EPUB ha già un elenco pagine** non serve nulla: lo script lo segnala e si ferma (il Kindle
   userà le pagine esatte).
2. **ISBN.** Si legge dall'OPF, in quest'ordine:
   1. `dc:source` con `source-of = pagination` (è proprio l'ISBN dell'edizione stampata);
   2. `dc:identifier` con `urn:isbn:…` o schema `ISBN`;
   3. qualunque `dc:identifier` che sia un ISBN-10/13 valido (cifra di controllo verificata).
3. **Open Library, edizione.** `GET https://openlibrary.org/isbn/{isbn}.json`. Se la risposta ha
   `number_of_pages`, si usa quello.
4. **Open Library, opera.** Spesso l'ISBN dell'EPUB è quello dell'**ebook**, che non ha pagine. In quel
   caso si segue `works[0].key` e si leggono le altre edizioni:
   `GET https://openlibrary.org/works/{id}/editions.json`. Si tengono quelle con `number_of_pages`,
   preferendo nell'ordine: stessa lingua dell'EPUB, formato cartaceo (`physical_format` tipo
   *Paperback* / *Hardcover*), stesso editore, anno più vicino. Se restano più candidati si prende la
   mediana e si salvano le alternative nel file per poterle scegliere a mano.
5. **Nessun risultato.** Lo script chiede il numero a mano (o lo si passa con `--pagine`). Se non lo si
   inserisce, il libro resta senza pagine cartacee.
6. Lo script stampa sempre cosa ha trovato, per esempio:
   ```
   ISBN 9788806218911 (ebook) → opera OL1234W → 3 edizioni cartacee
     scelta: Einaudi 2014, brossura, 384 pagine
   scritto libro.carbonella
   ```

Note su Open Library:

- È gratuita e senza chiave. Le linee guida chiedono di identificare il client con un `User-Agent`
  esplicito (es. `Carbonella/0.1`) e di non fare raffiche di richieste: una richiesta per libro va
  benissimo.
- Il campo `number_of_pages` di solito conta le pagine numerate in arabo, quindi è coerente con il
  "di Y" del caso esatto.
- Se in futuro servisse una seconda fonte, Google Books (`pageCount`) si può aggiungere allo stesso
  script senza cambiare nulla sul Kindle.

### Sul Kindle: la posizione

Con il totale `Y` nel file di accompagnamento, la pagina si calcola in proporzione sui caratteri del
testo:

```
caratteri_prima = caratteri di tutti i capitoli precedenti + offset nel capitolo corrente
pagina          = 1 + floor(caratteri_prima × Y / caratteri_totali)   (al massimo Y)
```

- `caratteri_totali` conta solo il testo visibile (senza tag) dei capitoli `linear="yes"` dello spine,
  quindi copertina e note a parte non falsano il conto. Si calcola una volta all'apertura e si salva
  nella cache del libro, insieme all'ultima pagina letta.
- Si usano i caratteri e non le schermate, così la stima **non cambia** quando si cambia la dimensione
  del carattere.
- La stima è grossolana all'inizio e alla fine (premesse, ringraziamenti, indice analitico occupano
  carta in modo diverso dal testo), ma a metà libro di solito si sbaglia di poche pagine. Per questo si
  mostra sempre la tilde: `~pag. 142 di 384`.

## File di accompagnamento (sidecar)

Accanto a ogni libro, con lo stesso nome ed estensione `.carbonella`:

```
documents/
├── Il nome della rosa.epub
└── Il nome della rosa.carbonella
```

Testo semplice `chiave=valore`, una riga per voce, facile da leggere in C e da correggere a mano:

```ini
# Carbonella · dati del libro
versione=1
pagine.totale=384
pagine.fonte=openlibrary          # openlibrary | manuale
pagine.isbn=9788806218911
pagine.edizione=Einaudi 2014, brossura
pagine.openlibrary=OL24229110M
pagine.alternative=384 Einaudi 2014; 512 Bompiani 1980
```

- Le righe sconosciute si ignorano, così il formato può crescere (segnalibri, ultima posizione, ecc.).
- Se il file manca o è illeggibile, il libro si apre normalmente senza pagine stimate.
- Le pagine **esatte** non finiscono nel file: si leggono sempre dall'EPUB, che è la fonte giusta.

## Interfaccia

### Barra di stato in lettura

Nel design Figma la riga in basso contiene capitolo, pagina e avanzamento. La parte "pagina" diventa:

| Caso | Testo | Note |
|------|-------|------|
| esatto | `pag. 142 di 384` | |
| esatto, pagina romana | `pag. xii di 384` | |
| stimato | `~pag. 142 di 384` | la tilde in grigio Cenere G7 |
| nessun dato | `12 / 530` | schermate, come oggi |

Carattere IBM Plex Mono, così le cifre non si spostano a ogni cambio pagina. La barra di avanzamento
resta basata sui caratteri (uguale in tutti i casi).

### Impostazioni

Nuova voce **Numeri di pagina**, valori scelti con ← →:

- *Edizione cartacea* (predefinito): usa 1 o 2, se mancano ricade sulle schermate.
- *Schermate*: il comportamento classico del Kindle.
- *Percentuale*: solo `42%`.

### Libreria

Nella riga del libro, accanto all'autore, si può mostrare la lunghezza: `384 pag.` (o `~384 pag.`).
Utile per scegliere cosa leggere.

### Vai a pagina

La schermata "Vai a pagina" (ancora da disegnare) accetta il **numero di pagina cartacea** quando c'è:
con le pagine esatte si salta all'ancora; con quelle stimate alla posizione proporzionale. Così si può
seguire un riferimento tipo "vedi pag. 210" di un saggio o di un amico che legge il cartaceo.

## Lavoro da fare

1. Parser EPUB sul Kindle: leggere `nav` / NCX / `page-map` e registrare le ancore durante il parsing
   XHTML (fa parte del supporto EPUB già in roadmap).
2. Tabella delle pagine e ricerca binaria; testo nella barra di stato.
3. Conteggio dei caratteri per capitolo, salvato in cache.
4. Lettura del file `.carbonella`.
5. Script `tools/carbonella-pagine` sul computer.
6. Voce nelle impostazioni e componenti nel file Figma (barra di stato con le tre varianti).
7. Libri di prova: uno con `page-list` EPUB 3, uno con `pageList` NCX, uno con `page-map.xml`, uno
   senza niente.

## Domande aperte

- **Schermata a cavallo di due pagine:** mostrare la pagina di inizio (proposta attuale) o l'intervallo
  `142–143`?
- **Più edizioni cartacee** con numeri diversi (es. brossura e tascabile): basta la scelta automatica
  con la possibilità di correggerla a mano nel file, o serve sceglierla dal Kindle?
- **Libri TXT:** non hanno ISBN né pagine. Si potrebbe permettere solo l'inserimento manuale del totale.
