# 18 novembre 2026 · Evidenziazione, copertine nei componenti, vecchi componenti in Evoluzione

Tempo: circa 25 minuti (14:50–15:15 UTC).

Simone: "togli vai a pagina, metti inizia ad evidenziare, fai in modo che tasto destro e tasto sinistro mi faccia passare da un capitolo all'altro, altrimenti da 10 pagine a 10 pagine. ho modificato la pagina di "pagina del cartaceo", metti le copertine sui component, sposta i vecchi components in archivio (evoluzione). L'evidenziazione inizia nel mezzo e lampeggia, quando premo il tasto centrale inizio ad evidenziare".

## Menu del libro e "Pagine del cartaceo"

Li ha rifatti Simone in Figma: nel menu del libro "Vai a pagina…" non c'è più, ci sono "Inizia a evidenziare" e "Evidenziazioni". "Pagine del cartaceo" ha il numero tra le frecce "< 158 >"; la schermata "non note" è stata tolta. Presi così come sono.

## Evidenziazione (colonna 02 Lettura)

Partendo dalle prove di Simone:

1. **Evidenzia 1 · cursore al centro (lampeggia)**: una barra verticale a metà pagina; nel piede "centro per iniziare a evidenziare".
2. **Evidenzia 2 · selezione**: il passo scelto in negativo; nel piede "centro per salvare, indietro per annullare".
3. **Evidenzia 3 · salvata** (nuova): il passo resta sottolineato con una linea nera di 2px, il piede torna "12 di 684".

![Selezione](img/2026-11-18-evidenzia-selezione.png)
![Salvata](img/2026-11-18-evidenzia-salvata.png)

Tasti nella lettura (solo comportamento, passato al front end): destra e sinistra del pad vanno al capitolo successivo o precedente; se il libro non ha capitoli, avanti o indietro di 10 pagine.

## Libreria componenti

- I sei modelli di copertina 600×800 e la "Raccolta 600×800 / Due faldoni" sono ora dentro la Libreria componenti, in un gruppo "Copertine 600×800 · modelli editori e raccolta".
- In Evoluzione › Superati: "Riga libro", "Cartella", "Riga archivio" con la schermata "01 · Libreria Text" (vista a elenco mai arrivata nel codice), e le due sezioni di prova (bozza modelli 600×800, card "due faldoni").
