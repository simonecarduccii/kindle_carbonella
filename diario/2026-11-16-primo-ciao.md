# 2026-11-16 · Il primo "Ciao" di Carbonella

**Stato:** 🟡 programma compilato e verificato al computer, da provare sul Kindle.

## Obiettivo

Il primo codice nostro che gira sul Kindle: un programma C che disegna "Ciao" sullo schermo e-ink.
Serve a verificare tutta la catena: scrivo sul Mac → compilo per ARM → copio via `scp` → avvio via SSH.

## Cosa abbiamo fatto (10:08–10:25 UTC, circa 17 minuti)

- Codice in `esperimenti/01-ciao/` (`ciao.c`, `Makefile`, `README.md` con tutti i comandi).
- **Disegno:** il programma apre `/dev/fb0`, lo mappa in memoria, lo riempie di bianco e disegna le
  lettere con un mini font 5×7 scritto a mano, ingrandito 20 volte ("Ciao") e 6 volte ("Carbonella").
  Funziona con 4, 8, 16 e 32 bit per pixel, perché non sappiamo ancora con certezza il formato del K4.
- **Aggiornamento dello schermo:** l'e-ink non si ridisegna da solo. Usiamo l'ioctl
  `FBIO_EINK_UPDATE_DISPLAY` del vecchio driver "einkfb" (Kindle 2/3/4), con un piano B su
  `/proc/eink_fb/update_display`. Il programma stampa quale ha funzionato.
- **Toolchain:** [Zig](https://ziglang.org) come cross-compilatore (`zig cc -target arm-linux-musleabi
  -mcpu=cortex_a8 -static`). Il K4 ha un processore i.MX508 (Cortex-A8) e librerie del 2011: un
  binario statico con `musl` evita ogni problema di versioni. Su Mac basta `brew install zig`.
- **Verifica fatta qui:** compila senza warning; il risultato è un `ELF 32-bit ARM, statically linked`
  di circa 31 KB. Il codice di disegno, compilato per il computer, produce questa anteprima:

![anteprima del Ciao](img/2026-11-16-ciao-anteprima.png)

- **Non ancora verificato:** che il refresh e-ink funzioni sul K4 reale e che i colori non siano
  invertiti (c'è l'opzione `-i`). Lo scopriamo alla prima prova.

## Passi per Simone

Seguire `esperimenti/01-ciao/README.md`. In breve, dal Mac:

```sh
brew install zig
cd esperimenti/01-ciao
make
ssh root@192.168.15.244 "mkdir -p /mnt/us/carbonella"
scp -O build/ciao root@192.168.15.244:/mnt/us/carbonella/ciao
ssh root@192.168.15.244 "chmod +x /mnt/us/carbonella/ciao; /mnt/us/carbonella/ciao"
```

## Risultato

_Da compilare: la riga `framebuffer: ...` stampata dal programma, quale refresh ha funzionato,
una foto dello schermo del Kindle (in `img/2026-11-16-ciao-kindle.jpg`)._

## Prossimi passi

- Usare FBInk per scrivere testo con font veri.
- Leggere i tasti da `/dev/input/event*`.
- Fermare l'interfaccia di Amazon mentre Carbonella è in esecuzione, così non ridisegna sopra.
