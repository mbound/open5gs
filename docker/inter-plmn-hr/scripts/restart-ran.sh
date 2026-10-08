#!/bin/sh
# restart-ran.sh: after the NF containers were recreated (new build image), re-attach the
# UERANSIM gNBs to the AMFs and re-register the UEs (UERANSIM does not reconnect by itself).
cd "$(dirname "$0")/.." || exit 1
docker compose --profile vplmn restart gnb-h gnb-v >/dev/null 2>&1
sleep 3
docker compose --profile vplmn restart ue-h ue-v >/dev/null 2>&1
sleep 8
