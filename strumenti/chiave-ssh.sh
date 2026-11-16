#!/bin/sh
# Prepara una chiave SSH per entrare nel Kindle senza password.
#
# Uso (dal Mac, con USBNetwork attivo):   sh strumenti/chiave-ssh.sh
#
# Cosa fa:
#   1. crea la chiave ~/.ssh/kindle (solo la prima volta);
#   2. copia la parte pubblica nel Kindle, in /mnt/us/usbnet/etc/authorized_keys
#      (ti chiede la password di root UN'ULTIMA volta);
#   3. aggiunge a ~/.ssh/config un blocco per il Kindle, così ssh, scp e make run usano la chiave;
#   4. prova a collegarsi senza password.
# Sul Kindle tocca solo il file authorized_keys di USBNetwork, nel disco dei libri.

set -eu

KINDLE_IP="${KINDLE_IP:-192.168.15.244}"
KEY="$HOME/.ssh/kindle"
CONFIG="$HOME/.ssh/config"

mkdir -p "$HOME/.ssh"
chmod 700 "$HOME/.ssh"

echo "1/4  Chiave"
if [ -f "$KEY" ]; then
    echo "     esiste già: $KEY"
else
    ssh-keygen -t ed25519 -N "" -C "carbonella-kindle" -f "$KEY"
fi

echo "2/4  Copio la chiave pubblica sul Kindle (inserisci la password di root)"
ssh -o PreferredAuthentications=password,keyboard-interactive -o PubkeyAuthentication=no \
    "root@$KINDLE_IP" \
    "mkdir -p /mnt/us/usbnet/etc && touch /mnt/us/usbnet/etc/authorized_keys && \
     grep -qF '$(cat "$KEY.pub")' /mnt/us/usbnet/etc/authorized_keys || \
     echo '$(cat "$KEY.pub")' >> /mnt/us/usbnet/etc/authorized_keys"

echo "3/4  Configuro ~/.ssh/config"
touch "$CONFIG"
chmod 600 "$CONFIG"
if grep -q "# carbonella-kindle" "$CONFIG"; then
    echo "     blocco già presente"
else
    cat >> "$CONFIG" <<CFG

# carbonella-kindle (aggiunto da strumenti/chiave-ssh.sh)
Host kindle $KINDLE_IP
    HostName $KINDLE_IP
    User root
    IdentityFile $KEY
    IdentitiesOnly yes
    ControlMaster auto
    ControlPath /tmp/k4-%r@%h
    ControlPersist 600
CFG
    echo "     aggiunto il blocco 'kindle'"
fi

echo "4/4  Provo il collegamento senza password"
if ssh -o BatchMode=yes -o ControlMaster=no -o ControlPath=none "root@$KINDLE_IP" "echo '     OK: entrato con la chiave'"; then
    echo "Fatto. Da ora puoi scrivere anche solo:  ssh kindle"
else
    echo "La chiave non è stata accettata. Incolla questo output nel thread di Carbonella."
    exit 1
fi
