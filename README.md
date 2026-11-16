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

## Roadmap

- [ ] Jailbreak e accesso via USBNetwork/SSH (guida: [docs/jailbreak.md](docs/jailbreak.md))
- [ ] Toolchain di cross-compilazione ARM
- [ ] "Hello world" sul framebuffer con FBInk
- [ ] Lettura dei tasti da `/dev/input`
- [ ] Visualizzazione e impaginazione di un file TXT
- [ ] Supporto EPUB (unzip + parsing XHTML essenziale)
- [ ] Libreria: elenco dei libri e ultima pagina letta

## Struttura

```
kindle_carbonella/
├── README.md
├── docs/
│   └── jailbreak.md   # guida al jailbreak del K4 (FW 4.1.4)
└── src/
    └── main.c   # punto di ingresso del lettore (segnaposto)
```

## Stato

Progetto appena avviato: per ora c'è solo questo scaffold.
