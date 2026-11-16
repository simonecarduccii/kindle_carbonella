# 2026-11-16 · USBNetwork: la rete via USB non parte

**Stato:** in corso.

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
