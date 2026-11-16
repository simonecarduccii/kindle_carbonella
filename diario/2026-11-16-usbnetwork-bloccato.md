# 2026-11-16 · USBNetwork: la rete via USB non parte

**Stato:** USBNetwork attivo (10:02 UTC). Prossimo passo: IP sul computer, ping e SSH.

## Problema

Simone è bloccato al passo "Attivare la rete via USB" della guida (`docs/jailbreak.md`, §6).

## Ipotesi

1. **Comando sbagliato per il K4.** La guida diceva `;un`, ma quella scorciatoia è dei Kindle Touch e
   successivi. Sul K4 la sequenza è `;debugOn`, `~usbNetwork`, `;debugOff`. Guida corretta.
2. **USBNetwork non installato.** Se dopo "Aggiorna il tuo Kindle" sul disco non c'è la cartella
   `usbnet/`, il pacchetto non è entrato.
3. **Interfaccia sul computer.** Dopo aver attivato USBNetwork bisogna scollegare e ricollegare il cavo.
   Su Windows serve il driver RNDIS; su Mac e Linux di solito no.

## Domande aperte a Simone

- Mac, Windows o Linux?
- Dopo il comando, il Kindle sparisce come disco USB?
- C'è la cartella `usbnet/`?
- Il computer vede una nuova interfaccia di rete? Cosa risponde `ping 192.168.15.244`?

## Soluzione (09:55–10:02 UTC, circa 7 minuti)

- La causa era la guida: `;un` non funziona sul K4. La sequenza giusta è `;debugOn`, `~usbNetwork`, `;debugOff`.
- Secondo intoppo: sul K4 la ricerca "non fa confermare", perché non c'è un tasto Invio. Si conferma
  scendendo con il pad a 5 direzioni sulla voce di ricerca e premendo il tasto centrale. Aggiunto alla guida.
- Risultato: dopo il comando il Kindle è sparito come disco USB, segno che USBNetwork è partito.
