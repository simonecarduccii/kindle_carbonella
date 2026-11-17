# Evoluzione di Carbonella

Il riassunto di tutto il percorso, dal Kindle appena sbloccato alla v1.1.0. I dettagli di ogni passo
sono nei file del diario linkati qui sotto. Le ore sono in UTC e vengono dai commit.

La stessa evoluzione è anche in Figma, nella pagina **Evoluzione**: lì ci sono le copie congelate delle
schermate di ogni passo.
<https://www.figma.com/design/TD5USYE5DwkvWALoR821lL>

## Linea del tempo

| Quando | Passo | Diario |
| --- | --- | --- |
| 16 nov 00:23 | Repository creato, guida al jailbreak del Kindle 4 | [guida-jailbreak](2026-11-16-guida-jailbreak.md) |
| 16 nov 09:55–10:12 | USBNetwork e SSH funzionanti (circa 12 min di troubleshooting) | [usbnetwork-ssh](2026-11-16-usbnetwork-ssh.md) |
| 16 nov 10:12–10:44 | Backup completo delle partizioni | [backup-partizioni](2026-11-16-backup-partizioni.md) |
| 16 nov 11:24 | Primo "Ciao" disegnato sull'e-ink | [primo-ciao](2026-11-16-primo-ciao.md) |
| 16 nov 11:24–13:52 | Chiave SSH RSA, USB sempre attiva (cavo da collegare dopo l'avvio) | [chiave-ssh-e-rete](2026-11-16-chiave-ssh-e-rete.md) |
| 16 nov 14:02 | Prima prova del design Figma sul Kindle vero: grigi, linee, contrasto | [prova-design-kindle](2026-11-16-prova-design-kindle.md) |
| 16 nov 16:33–19:05 | **v0.1 / v0.1.1 "Prima brace"**: lettore EPUB/TXT nativo in C, pacchetto KUAL | [v01-prima-brace](2026-11-16-v01-prima-brace.md) |
| 17 nov 00:23 | **v0.2 "Come in Figma"**: Home a copertine, menu, piè di pagina, copertina in standby | [v02-come-in-figma](2026-11-17-v02-come-in-figma.md) |
| 17 nov 00:41–00:50 | **v0.3.0 / v0.3.1**: bianco puro, menu sopra il testo, Impostazioni, Home 7× più veloce | [v03-appunti-di-simone](2026-11-17-v03-appunti-di-simone.md) |
| 17 nov 01:10–01:13 | **v0.4.0 / v0.4.1 "I menu di Figma"**: testi parola per parola, segnalibri, Cerca, Vai a pagina, raccolte | [v04-menu-di-figma](2026-11-17-v04-menu-di-figma.md) |
| 17 nov 01:34–01:35 | **v0.5.0 / v0.5.1 "Obiettivi"**: schermata Obiettivi, avviso obiettivo raggiunto, popup raccolte, Archivia libro | [v05-obiettivi](2026-11-17-v05-obiettivi.md) |
| 17 nov 01:54 | **v0.5.2**: lo standby con la copertina funziona davvero (il tasto di accensione veniva ignorato) | [v05-obiettivi](2026-11-17-v05-obiettivi.md#v052-lo-standby-non-funzionava) |
| 17 nov 09:50 | **v0.5.3**: ritocchi di Simone (barra di lettura nera, Obiettivi senza piè di pagina) | [v05-obiettivi](2026-11-17-v05-obiettivi.md#v053-0950-utc-ritocchi-di-simone) |
| 17 nov 10:10 | **v0.6.0 / v0.6.1 "Copertine dagli editori"**: copertine dai 5 modelli di Simone, Standby automatico dopo, NUOVO, selezione quadrata, linguetta | [v06-copertine-editori](2026-11-17-v06-copertine-editori.md) |
| 17 nov 10:20–10:40 | **v0.6.2 / v0.6.3**: bordo del pannello raccolte, nuova batteria, modello Mondadori | [v06-copertine-editori](2026-11-17-v06-copertine-editori.md) |
| 17 nov 11:00 | **v1.0.0 "Carbonella"**: copertine 600×800, Home 3:4, grigi a contrasto costante, illustrazioni generate | [v100-carbonella](2026-11-17-v100-carbonella.md) |
| 17 nov 11:25–11:28 | **v1.0.1**: il tasto Home risveglia dallo standby (solo finché il Kindle non dorme del tutto) | [v100-carbonella](2026-11-17-v100-carbonella.md#v101--il-tasto-home-risveglia-11251128-utc) |
| 17 nov 11:35–11:56 | **v1.0.2–v1.0.5**: titoli e autori puliti, standby leggero, Feltrinelli nuovo, conferma e gruppo ARCHIVIO, sottolineature | [v100-carbonella](2026-11-17-v100-carbonella.md#v102v105-11351156-utc) |
| 17 nov 11:57 | **v1.0.6**: menu della Home nel nuovo ordine di Figma | [v100-carbonella](2026-11-17-v100-carbonella.md#v106-1157-utc) |
| 17 nov 12:05–12:40 | **v1.1.0 "Faldoni e Obiettivi"**: raccolte come due faldoni, nuova schermata Obiettivi | [v110-faldoni-obiettivi](2026-11-17-v110-faldoni-obiettivi.md) |

## Cosa abbiamo imparato

- **Hardware**: sul K4 i calcoli in virgola mobile sono emulati e costano circa 200 volte di più. Un
  disegno lento si scopre provando sotto qemu con una libreria vera, non nel simulatore.
- **Figma è la fonte di verità**: testi e ordine delle voci vanno presi parola per parola. Simone
  modifica i frame anche mentre lavoro, quindi vanno riletti subito prima di ogni installazione.
- **E-ink**: sfondo bianco puro, niente grigi chiari, linee di almeno 2 px, testo a 18 px.
- **Sistema del Kindle**: BusyBox 1.7.2 è vecchio (niente nohup, niente grep -E). `lipc-wait-event`
  ignora SIGTERM. Con `preventScreenSaver` a 1, powerd ignora il tasto di accensione. powerd del K4 non
  ha una proprietà `powerButton`: le proprietà vere si leggono con `lipc-probe -v com.lab126.powerd`.
- **Disegno veloce sul Kindle**: le forme grandi (dischi, poligoni) vanno riempite riga per riga, non
  campionando ogni punto del rettangolo che le contiene (v1.0: da 4 s a 0,3 s per copertina).

## Salvataggio del 17 nov, 09:40 UTC

Simone: "mi fai un bel giro di salvataggio evoluzione", "sposta in evoluzione dalla pagina figma di
carbonella tutto ciò che è diventato superfluo", "ti ho aggiunto anche 4 reference copertine libri nuove".

- **Pagina Evoluzione in Figma**: quattro passi nuovi in fondo alla colonna "Evoluzione del design".
  Sono tutte copie congelate, con le istanze staccate, quindi non cambiano se cambiano i componenti.
  - Sul Kindle: v0.3.0
  - Sul Kindle: v0.4.1 · menu come in Figma
  - Sul Kindle: v0.5.1 · Obiettivi e Archivia libro
  - 17 nov 09:36 · Stato del design oggi (tutte le schermate attuali)
- **Pulizia della pagina Carbonella**: le cose superate sono state spostate, non cancellate, nella
  sezione *Superati* a destra della pagina Evoluzione. Sono:
  - le sezioni di versione v0.3.0 e v0.4.1;
  - le copertine v1 per editore, sostituite dalle v2;
  - la nota "pagine stimate" con Open Library, idea mai realizzata nel codice;
  - il testo sparso "Conteggio";
  - un doppione di "Lessico famigliare · v2".
- **Nella pagina Carbonella restano** Componenti, Schermate, Copertine e la sezione della versione sul
  Kindle (allora v0.5.1; ora v0.6.1, la v0.5.3 è in *Superati*).
- **4 nuove reference di copertine** (Adelphi 7, Einaudi 8, Feltrinelli 6, Sellerio 7): spostate nelle
  righe "Altre reference per editore" della sezione Copertine. Restano il riferimento per le copertine
  generate.

## Pacchetti e versioni

- Ogni versione ha il suo pacchetto `carbonella-vX.Y.Z.zip` nella cartella *rilasci* del progetto (dalla
  0.1.0 alla 1.1.0). Gli zip di 0.6.1, 0.6.2, 0.6.3 e 1.0.2 non erano mai stati salvati e sono stati
  ricostruiti dai loro commit il 17 nov.
- Ogni versione avrà anche un'etichetta git `vX.Y.Z` sul commit che la porta (preparate il 17 nov, da pubblicare dal Mac).
- Dal 17 nov 12:02 le versioni le fa un solo thread, "Rilasciare e documentare Carbonella", e solo quando
  Simone lo chiede. Gli altri thread (design, front end, back end, debug) gli passano i commit pronti.

## Schermate per versione

Le immagini sono in [`img/`](img/), con la data e la versione nel nome, per esempio
`2026-11-17-v05-obiettivi.png`.

## Ancora da fare

- Avvio automatico di Carbonella all'accensione, con le vie d'uscita sicure. Aspetta il via di Simone.
- Domande aperte sulla v0.5 (Archivia libro, pagina 2 degli Obiettivi, togliere un libro da una
  raccolta): sono in fondo a [v05-obiettivi](2026-11-17-v05-obiettivi.md).
