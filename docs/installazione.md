# Installare Carbonella su un Kindle 4

Guida per chi riceve un Kindle con Carbonella o vuole installarla sul proprio. Serve un **Kindle 4**
(2011, senza touch, con il pad a 5 direzioni) e un computer con un cavo micro-USB.

Tempo: circa 30 minuti la prima volta (quasi tutto è il jailbreak), 2 minuti per gli aggiornamenti.

## In breve

1. **Jailbreak + KUAL** (una volta sola): [docs/jailbreak.md](jailbreak.md), sezioni 1–5.
   USBNetwork (sezione 6) non serve per usare Carbonella, solo per svilupparla.
2. **Scarica il pacchetto** `carbonella-vX.Y.Z.zip` dalla pagina
   [Releases](https://github.com/simonecarduccii/kindle_carbonella/releases) (oppure crealo con `make dist`).
3. **Copia sul Kindle**: collega il Kindle al computer, apri lo zip e trascina le due cartelle
   `carbonella` ed `extensions` nella **radice** del disco "Kindle" (accanto a `documents`).
   Se il computer chiede di unire la cartella `extensions` esistente, rispondi **Unisci** (non Sostituisci).
   In alternativa, da Terminale: `sh strumenti/installa-usb.sh carbonella-vX.Y.Z.zip`.
4. **Copia i libri** (`.epub` o `.txt`, senza DRM) nella cartella `documents`.
5. **Espelli** il Kindle e scollega il cavo.
6. Sul Kindle apri **KUAL** (è nella libreria come un libro) → **Carbonella** → **Apri Carbonella**.
   Lo schermo lampeggia, l'interfaccia Amazon si ferma e dopo qualche secondo compare la libreria.

## Come si usa

| Tasto | In libreria | Mentre leggi |
|-------|-------------|--------------|
| Tasti pagina ai lati | pagina dell'elenco | pagina avanti / indietro |
| Pad ◀ ▶ | pagina dell'elenco | pagina indietro / avanti |
| Pad ▲ ▼ | sposta la selezione | pagina indietro / avanti |
| Centro del pad | apri il libro | menu |
| Menu | aggiorna l'elenco, torna al Kindle | menu |
| Indietro | — | torna alla libreria |
| Home | torna al Kindle (chiede conferma) | torna alla libreria |

Il **menu di lettura** ha: Indice, Testo più grande, Testo più piccolo, Pulisci lo schermo,
Torna alla libreria, Torna al Kindle.

## Tornare al Kindle originale

- Dal menu: **Torna al Kindle**, oppure **Home** in libreria. L'interfaccia Amazon riparte in circa 30 secondi.
- Da sola: dopo **20 minuti** senza toccare tasti Carbonella si chiude e torna al Kindle.
- **Se si blocca tutto**: tieni premuto il tasto di accensione per **20 secondi**. Il Kindle si riavvia
  sempre nell'interfaccia Amazon: Carbonella non parte mai da sola all'accensione.

## Aggiornare

Ripeti i passi 2, 3 e 5 con il nuovo zip. I file vengono sostituiti, ma **non** si perdono le posizioni
di lettura né le preferenze (sono in `carbonella/dati/`, che lo zip non contiene).

## Disinstallare

Collega il Kindle e cancella le cartelle `carbonella` ed `extensions/carbonella`.

## Personalizzare

Le impostazioni predefinite sono in `carbonella/carbonella.conf` (con le spiegazioni).
Per cambiarle in modo permanente scrivi le stesse righe in `carbonella/dati/preferenze.conf`.
Esempi: `lampeggio_ogni = 10`, `margine = 30`, `inattivita_minuti = 30`, `apri_ultimo = 1`.

## Se qualcosa non va

- **Il registro**: `carbonella/carbonella.log` (e `carbonella.log.prima` per l'avvio precedente).
  Mandalo a Simone: dice cosa è successo.
- **Un tasto non fa niente**: KUAL → Carbonella → **Prova dei tasti** e annota il codice che compare.
- **Resta l'ombra della pagina precedente**: in `dati/preferenze.conf` scrivi `lampeggio = inverti`.
- **Un libro non si apre**: probabilmente ha il DRM o il file è rovinato. Prova a convertirlo in EPUB
  con [Calibre](https://calibre-ebook.com).
