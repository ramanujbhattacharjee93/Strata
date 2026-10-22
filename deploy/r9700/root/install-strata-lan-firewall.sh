#!/bin/sh
# Run as: sudo sh ~/.local/state/strata-lan-setup/install-strata-lan-firewall.sh
set -eu
D=/home/rambhatt/.local/state/strata-lan-setup
install -d -m 755 /etc/strata-lan
install -m 644 "$D/strata-lan.nft" /etc/strata-lan/strata-lan.nft
nft -c -f /etc/strata-lan/strata-lan.nft
install -m 644 "$D/strata-lan-firewall.service" /etc/systemd/system/strata-lan-firewall.service
systemd-analyze verify /etc/systemd/system/strata-lan-firewall.service
systemctl daemon-reload
systemctl enable --now strata-lan-firewall.service
nft list table inet strata_lan
ls -l /run/strata-lan-firewall.ok
echo "OK: firewall loaded and enabled at boot"
