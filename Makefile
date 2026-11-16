# Carbonella - build
#
#   make            compila build/carbonella per il Kindle 4 (ARM, statico)
#   make sim        compila build/carbonella-sim per il computer (simulatore, salva le schermate)
#   make prova      simulatore con il libro di prova: schermate in build/schermate/
#   make dist       crea dist/carbonella-vX.Y.Z.zip, il pacchetto da copiare sul Kindle
#   make installa   copia il pacchetto sul Kindle via SSH (Host "kindle" in ~/.ssh/config)
#   make avvia      avvia Carbonella sul Kindle via SSH (come dal menu KUAL)
#   make log        mostra il registro dell'ultima esecuzione sul Kindle
#   make clean
#
# Serve Zig come cross-compilatore (su Mac: brew install zig).

VERSION := $(shell sed -n 's/^\#define CARBONELLA_VERSION "\(.*\)"/\1/p' src/carbonella.h)
KINDLE ?= kindle
ZIG ?= zig
TARGET ?= arm-linux-musleabi
CC_HOST ?= cc

SRC := src/app.c src/canvas.c src/display.c src/epub.c src/font.c src/input.c src/layout.c src/library.c src/util.c
HDR := src/carbonella.h
MINIZ := third_party/miniz/miniz.c
DEFS := -D_GNU_SOURCE -DMINIZ_NO_DEFLATE_APIS -DMINIZ_NO_ZLIB_APIS -DMINIZ_NO_TIME
WARN := -Wall -Wextra -Wno-unused-function
# -O2: the Cortex-A8 likes it; -mcpu tells the compiler exactly which CPU the K4 has (i.MX508)
ARM_FLAGS := -target $(TARGET) -mcpu=cortex_a8 -static -O2 -s -std=gnu11 $(DEFS)

SSH_OPTS = -o ControlMaster=auto -o ControlPath=/tmp/k4-%r@%h -o ControlPersist=600

all: build/carbonella

build/carbonella: $(SRC) $(HDR) $(MINIZ)
	@mkdir -p build
	$(ZIG) cc $(ARM_FLAGS) $(WARN) -o $@ $(SRC) $(MINIZ) -lm
	@ls -l $@

build/carbonella-sim: $(SRC) $(HDR) $(MINIZ)
	@mkdir -p build
	$(CC_HOST) -O2 -g -std=gnu11 $(DEFS) $(WARN) -o $@ $(SRC) $(MINIZ) -lm

sim: build/carbonella-sim

# A quick tour: library, open the book, turn pages, menu, bigger text, index, back.
PROVA_TASTI ?= S,N,N,N,N,N,N,P,M,D,S,B,M,S,D,D,S,B,H,D,S
prova: build/carbonella-sim build/libri/prova.epub
	rm -rf build/schermate build/base && mkdir -p build/base
	cp -r pacchetto/carbonella/fonts pacchetto/carbonella/carbonella.conf build/base/
	./build/carbonella-sim --base build/base --libri build/libri --sim build/schermate --keys "$(PROVA_TASTI)"
	python3 strumenti/pgm2png.py build/schermate

build/libri/prova.epub: strumenti/crea-epub-prova.py
	@mkdir -p build/libri
	python3 strumenti/crea-epub-prova.py $@

dist: build/carbonella
	rm -rf build/pacchetto && mkdir -p build/pacchetto dist
	cp -r pacchetto/carbonella pacchetto/extensions build/pacchetto/
	mkdir -p build/pacchetto/carbonella/bin
	cp build/carbonella build/pacchetto/carbonella/bin/carbonella
	cp CHANGELOG.md build/pacchetto/carbonella/
	sed -i.bak 's/@VERSION@/$(VERSION)/' build/pacchetto/extensions/carbonella/config.xml && rm build/pacchetto/extensions/carbonella/config.xml.bak
	chmod +x build/pacchetto/carbonella/bin/carbonella build/pacchetto/carbonella/*.sh build/pacchetto/extensions/carbonella/bin/*.sh
	cd build/pacchetto && rm -f ../../dist/carbonella-v$(VERSION).zip && zip -qr ../../dist/carbonella-v$(VERSION).zip carbonella extensions
	@ls -l dist/carbonella-v$(VERSION).zip

# Copies the package to /mnt/us (the Kindle's USB drive). Preferences and reading data are kept.
installa: dist
	ssh $(SSH_OPTS) $(KINDLE) "mkdir -p /mnt/us/carbonella /mnt/us/extensions"
	scp -O $(SSH_OPTS) dist/carbonella-v$(VERSION).zip $(KINDLE):/tmp/carbonella.zip
	ssh $(SSH_OPTS) $(KINDLE) "cd /mnt/us && unzip -o -q /tmp/carbonella.zip && rm /tmp/carbonella.zip && chmod +x carbonella/bin/carbonella carbonella/*.sh extensions/carbonella/bin/*.sh && echo 'Carbonella $(VERSION) installata'"

avvia:
	ssh $(SSH_OPTS) $(KINDLE) "/mnt/us/extensions/carbonella/bin/avvia.sh"

log:
	ssh $(SSH_OPTS) $(KINDLE) "cat /mnt/us/carbonella/carbonella.log"

clean:
	rm -rf build dist

.PHONY: all sim prova dist installa avvia log clean
