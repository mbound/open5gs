#!/bin/sh
# restart-ran.sh: after the NF containers were recreated (new build image), re-attach the
# UERANSIM gNBs to the AMFs and re-register the UEs (UERANSIM does not reconnect by itself).
cd "$(dirname "$0")/.." || exit 1
since=$(date -u +%Y-%m-%dT%H:%M:%SZ)
docker compose --profile vplmn restart gnb-h gnb-v >/dev/null 2>&1
# A UE started before its gNB has completed NG Setup finds no cell ("PLMN selection
# failure, no cells in coverage") and does not retry soon; right after an image rebuild
# the AMFs answer NG Setup later than the fixed delay. Wait for both NG Setups (60 s max).
for gnb in gnb-h gnb-v; do
  n=0
  until docker compose logs --no-color --since "$since" "$gnb" 2>/dev/null |
      grep -q 'NG Setup procedure is successful'; do
    n=$((n + 1)); [ $n -lt 30 ] || { echo "no NG Setup at $gnb"; break; }
    sleep 2
  done
done
docker compose --profile vplmn restart ue-h ue-v >/dev/null 2>&1
sleep 8
