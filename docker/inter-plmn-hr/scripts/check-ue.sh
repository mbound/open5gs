#!/bin/sh
# check-ue.sh [ue-h|ue-v]: Prints the UE's registration/PDU session
# log lines and pings the H-UPF ogstun gateway 10.45.0.1 through uesimtun0.
cd "$(dirname "$0")/.." || exit 1
ue=${1:-ue-h}
docker compose logs --no-color "$ue" | sed 's/\x1b\[[0-9;]*m//g' |
  grep -E 'Registration is successful|PDU Session establishment is successful|TUN interface|reject|failure' | grep -v '\[rrc\]' | tail -5
docker compose exec -T "$ue" ping -c 3 -W 2 -I uesimtun0 10.45.0.1
