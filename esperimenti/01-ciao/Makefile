# Carbonella - esperimento 01: "Ciao"
#
#   make         compila build/ciao per il Kindle 4 (ARM)
#   make run     copia il programma sul Kindle e lo avvia via SSH
#   make clean   cancella build/
#
# Serve Zig (https://ziglang.org) come cross-compilatore: su Mac  brew install zig

KINDLE ?= root@192.168.15.244
REMOTE_DIR ?= /mnt/us/carbonella

ZIG ?= zig
# musl + static: nessuna dipendenza dalle librerie (vecchie) del Kindle.
TARGET ?= arm-linux-musleabi
CFLAGS ?= -Os -Wall -Wextra -std=c99 -D_DEFAULT_SOURCE

# Una sola connessione SSH riusata per 10 minuti: la password si chiede una volta sola.
SSH_OPTS = -o ControlMaster=auto -o ControlPath=/tmp/k4-%r@%h -o ControlPersist=600

build/ciao: ciao.c
	mkdir -p build
	$(ZIG) cc -target $(TARGET) -mcpu=cortex_a8 -static $(CFLAGS) -o $@ ciao.c

run: build/ciao
	ssh $(SSH_OPTS) $(KINDLE) "mkdir -p $(REMOTE_DIR)"
	scp -O $(SSH_OPTS) build/ciao $(KINDLE):$(REMOTE_DIR)/ciao
	ssh $(SSH_OPTS) $(KINDLE) "chmod +x $(REMOTE_DIR)/ciao; $(REMOTE_DIR)/ciao"

clean:
	rm -rf build

.PHONY: run clean
