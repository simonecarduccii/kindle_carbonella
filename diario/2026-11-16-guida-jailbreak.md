# 2026-11-16 · Guida al jailbreak del Kindle 4

**Durata:** circa 5 minuti di lavoro (00:25–00:30 UTC), ricerca compresa.
**Risultato:** [`docs/jailbreak.md`](../docs/jailbreak.md), proposta nella PR #1.

## Punto di partenza

- Dispositivo: Kindle 4 senza touch, firmware `Kindle 4.1.4 (3543630001)` (dallo screenshot di Simone;
  all'inizio pensavamo fosse un Paperwhite 1, la foto ha mostrato che è un K4).
- Dubbio iniziale: il vecchio jailbreak del K4 era stato bloccato in alcune versioni 4.1.x?
  Serviva un downgrade o il cavo seriale?

## Cosa ho fatto

1. Ricerca sulle fonti più recenti (MobileRead Wiki, KindleModding, guide del 2025 e 2026).
2. Confronto dei metodi e scelta di quello consigliato.
3. Scrittura della guida passo passo, in italiano, per chi non è esperto.
4. Aggiunta del collegamento alla guida nel README.

## Cosa ho scoperto

- Sul 4.1.4 funziona ancora il jailbreak "diagnostico" (`data.tar.gz` + `ENABLE_DIAGS` +
  `diagnostic_logs/`). Non servono né downgrade né seriale. La wiki MobileRead lo dà valido da 4.0.0
  a 4.1.4, e le prove del 2025 e 2026 (makerblock, Longplay Tech, kxrz) lo confermano.
- Il pacchetto storico `kindle-k4-jailbreak-1.8.N` usa certificati scaduti ad aprile 2025 (errore
  *"not authorized as a test Kindle"* in KUAL). Scelto quindi **K4-Jailbreak v1.0.0** di KindleModding
  (giugno 2026), che include certificati nuovi.
- KUAL per il K4: solo `KUAL-KDK-1.0.azw2`.
- USBNetwork: rete via USB, Kindle `192.168.15.244`, computer `192.168.15.201`, attivazione con `;un`.

## Cose ancora aperte

- Non sono riuscito a leggere i thread MobileRead t=88004 e t=225030 (bloccati per i client automatici).
  Quindi mancano il nome esatto del file `Update_*.bin` di USBNetwork per il K4 e le regole sulla
  password SSH: vanno verificati nel `README_FIRST.txt` del pacchetto, quando lo scarichiamo.
- Il jailbreak non è ancora stato eseguito sul Kindle. Quando lo facciamo, apriamo una nuova voce di
  diario con foto delle schermate (menu diagnostico, libro "You are Jailbroken", KUAL).

## Fonti

- <https://kindlemodding.org/jailbreaking/Legacy/K4-Jailbreak/>
- <https://github.com/KindleModding/K4-Jailbreak>
- <https://wiki.mobileread.com/wiki/Kindle4NTHacking>
- <https://github.com/kxrz/kindle4-revival>
- <https://longplaytech.com/posts/jailbreak-kindle-4th-gen/>
- <https://makerblock.com/2025/06/jailbreaking-a-kindle-4-non-touch-2012-take-2/>
