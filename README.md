# Carbonella

> *Kindle* vuol dire "accendere un fuoco". La carbonella è quello che lo tiene acceso.

Carbonella è un sistema di lettura minimale e nativo per il **Kindle 4** (2011, senza touch, firmware 4.1.4).
L'obiettivo è sostituire l'interfaccia di Amazon con un lettore essenziale, scritto in C, che disegna
direttamente sul framebuffer e-ink e si controlla con il pad a 5 direzioni e i tasti laterali.

## Obiettivi

1. **Jailbreak del Kindle 4** per avere accesso root e poter avviare programmi propri.
2. **Lettore nativo in C** che:
   - disegna sul framebuffer e-ink (ad esempio con [FBInk](https://github.com/NiLuJe/FBInk));
   - legge i tasti tramite l'input di Linux (`/dev/input/event*`);
   - apre file **TXT** ed **EPUB**;
   - impagina il testo e permette di sfogliare le pagine.

## Hardware di riferimento

| Voce        | Valore                                   |
|-------------|------------------------------------------|
| Modello     | Kindle 4 (2011), non touch               |
| Firmware    | 4.1.4                                    |
| Sistema     | Linux su ARM                             |
| Schermo     | e-ink 600×800                            |
| Input       | pad a 5 direzioni, tasti pagina, Home, Menu, Indietro, Tastiera |

## Installare e provare

- **Per chi lo riceve** (o per gli amici): [docs/installazione.md](docs/installazione.md). In breve:
  jailbreak + KUAL, poi si copiano due cartelle sul Kindle via USB.
- **Per sviluppare** (dal Mac, con `ssh kindle` già configurato e Zig installato):

```sh
make            # compila build/carbonella per il Kindle 4
make prova      # simulatore sul computer: schermate PNG in build/schermate/
make dist       # crea dist/carbonella-vX.Y.Z.zip
make installa   # copia il pacchetto sul Kindle via SSH
make avvia      # avvia Carbonella sul Kindle (come da KUAL)
make log        # registro dell'ultima esecuzione
```

Le novità di ogni versione sono in [CHANGELOG.md](CHANGELOG.md).

## Roadmap

- [x] Jailbreak e accesso via USBNetwork/SSH (guida: [docs/jailbreak.md](docs/jailbreak.md))
- [x] Backup delle partizioni ([strumenti/backup-k4.sh](strumenti/backup-k4.sh))
- [x] Toolchain di cross-compilazione ARM (Zig, `arm-linux-musleabi`, statico)
- [x] "Hello world" sul framebuffer ([esperimenti/01-ciao](esperimenti/01-ciao))
- [x] **v0.1**: font veri (Literata con stb_truetype), tasti da `/dev/input`, TXT ed EPUB,
      impaginazione con sillabazione italiana, libreria con ultima pagina letta, pacchetto KUAL
- [ ] v0.1 provata sul Kindle vero (tasti, lampeggio, tasto di accensione)
- [ ] Home a 3 colonne con copertine generate (design Figma)
- [ ] Numeri di pagina dell'edizione cartacea ([proposta](https://github.com/simonecarduccii/kindle_carbonella/pull/3))
- [ ] Immagini negli EPUB, note a piè di pagina
- [ ] Obiettivi di lettura (dai dati di `letture.tsv`), Archivio, Raccolte
- [ ] PDF (MuPDF)

## Struttura

```
kindle_carbonella/
├── Makefile           # compilazione, simulatore, pacchetto, installazione
├── CHANGELOG.md       # novità di ogni versione
├── src/               # il lettore, in C (vedi src/carbonella.h per la mappa dei moduli)
├── third_party/       # miniz (zip) e stb_truetype (font): un file ciascuno, pubblico dominio / MIT
├── pacchetto/         # cosa finisce sul Kindle: script di avvio, font, impostazioni, menu KUAL
├── diario/            # diario di sviluppo: un file per attività, con data
├── docs/
│   ├── installazione.md  # installare Carbonella (anche per gli amici)
│   └── jailbreak.md      # guida al jailbreak del K4 (FW 4.1.4)
├── esperimenti/
│   └── 01-ciao/       # primo programma nativo: "Ciao" sullo schermo e-ink
└── strumenti/
    ├── backup-k4.sh         # backup delle partizioni via SSH
    ├── chiave-ssh.sh        # accesso SSH senza password
    ├── installa-usb.sh      # installa il pacchetto su un Kindle collegato via USB
    ├── crea-epub-prova.py   # EPUB di prova per il simulatore
    └── pgm2png.py           # converte le schermate del simulatore
```

## Stato

**v0.1.0** pronta per la prima prova sul Kindle (vedi [diario](diario/2026-11-16-v01-prima-brace.md)).
