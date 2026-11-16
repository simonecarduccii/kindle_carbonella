# 2026-11-16 · Specifica: pagine dell'edizione cartacea

**Durata:** circa 5 minuti, dalle 09:16 alle 09:21 UTC.
**Risultato:** specifica di design in [docs/pagine-cartacee.md](../docs/pagine-cartacee.md) e una voce
nuova nella roadmap del README. Nessun codice ancora.

## Punto di partenza

Simone voleva vedere il numero di pagine **vero** del libro, quello dell'edizione cartacea, per farsi
un'idea della lunghezza. Le "pagine" del Kindle sono schermate e cambiano con la dimensione del
carattere, quindi non dicono nulla di quanto è lungo un libro.

## Cosa abbiamo deciso

- **Due fonti, in ordine:**
  1. le pagine esatte che molti EPUB contengono già (`page-list` in EPUB 3, `pageList` nel file NCX
     o `page-map.xml` in EPUB 2): si mostra `pag. 142 di 384`;
  2. se mancano, il numero di pagine dell'edizione cartacea da **Open Library** tramite ISBN, distribuito
     in proporzione sui caratteri del testo: si mostra `~pag. 142 di 384`, con la tilde.
- **Il Kindle resta offline.** La ricerca su Open Library la fa uno script sul computer quando si carica
  il libro, e scrive il risultato in un file di testo accanto all'EPUB (`libro.carbonella`).
- **Pagine esatte calcolate sul Kindle**, durante il parsing dell'EPUB: non serve rete e non dipendono
  dal file di accompagnamento.
- **Stima sui caratteri e non sulle schermate**, così non cambia quando si cambia la dimensione del
  carattere.
- **Ebook ≠ cartaceo:** l'ISBN dentro un EPUB spesso è quello dell'ebook, che su Open Library non ha
  pagine. Lo script allora risale all'opera e guarda le altre edizioni, preferendo quelle cartacee
  nella stessa lingua.
- Nuova voce nelle impostazioni, **Numeri di pagina** (Edizione cartacea / Schermate / Percentuale), e
  lunghezza del libro visibile anche in libreria.

## Fonti consultate

- Specifica EPUB 3: `nav` con `epub:type="page-list"`, segnaposto `epub:type="pagebreak"`,
  proprietà `source-of="pagination"` su `dc:source`.
- EPUB 2 / NCX: elemento `pageList` con `pageTarget`; formato Adobe `page-map.xml`.
- API di Open Library: `/isbn/{isbn}.json` (campo `number_of_pages`) e
  `/works/{id}/editions.json`.

## Intoppi

- Dall'ambiente in cui lavoro la rete verso openlibrary.org è bloccata, quindi le chiamate all'API non
  sono state provate dal vivo: la specifica si basa sulla documentazione. Andranno verificate quando
  scriveremo lo script.

## Cose ancora aperte

- Schermata a cavallo di due pagine: mostrare solo la pagina iniziale o l'intervallo `142–143`?
- Libri con più edizioni cartacee diverse: basta correggere a mano il file `.carbonella`?
- Libri TXT: solo totale inserito a mano?
- Aggiungere le tre varianti della barra di stato nel file Figma.
