#!/bin/sh
# ntn-barring-demo.sh: NR(LEO) RAT restriction on the roaming UE, end to end.
#   1. gnb-v re-attaches with its TA (001-01 TAC 2) advertised as NR(LEO) (gnb-v-leo.yaml)
#   2. ue-v's subscriber (imsi-999700000000003) gets rat_restrictions [NR_LEO]
#      -> Registration Reject, 5GMM cause #15 "No suitable cells in tracking area"
#   3. the restriction is cleared -> ue-v registers (home-routed session, ping 10.45.0.1)
#   4. defaults restored: terrestrial gnb-v, no restriction, ue-v registered again
# Needs the running deployment (scripts/up.sh). Exit code 0 only if every step passes.
cd "$(dirname "$0")/.." || exit 1
IMSI=999700000000003
rc=0
step() { echo "== $*"; }
result() { if [ "$1" = 0 ]; then echo "   PASS: $2"; else echo "   FAIL: $2"; rc=1; fi; }
dbctl() { docker compose run --rm -T dbctl open5gs-dbctl "$@" >/dev/null; }
# gnb CONFIG: recreate gnb-v with ueransim/CONFIG (new NG Setup to amf-v)
gnb() {
  GNB_V_CONFIG=$1 docker compose --profile vplmn up -d --force-recreate gnb-v >/dev/null 2>&1
  sleep 3
}
# ue_restart: restart ue-v and wait for its registration attempt; logs since then in $log
ue_restart() {
  since=$(date -u +%Y-%m-%dT%H:%M:%SZ)
  docker compose --profile vplmn restart ue-v >/dev/null 2>&1
  sleep 8
  log=$(docker compose logs --no-color --since "$since" ue-v | sed 's/\x1b\[[0-9;]*m//g')
}

step "gnb-v: TA 001-01/2 as NR(LEO)"
gnb gnb-v-leo.yaml
docker compose logs --no-color --since 20s gnb-v | sed 's/\x1b\[[0-9;]*m//g' | grep 'NG Setup' | tail -1

step "ue-v subscriber: rat_restrictions NR_LEO"
dbctl rat_restrictions $IMSI NR_LEO
ue_restart
echo "$log" | grep -E 'Registration failed|Registration is successful' | tail -2
echo "$log" | grep -q 'Registration failed \[NO_SUITIBLE_CELLS_IN_TA\]'   # UERANSIM's spelling of #15
result $? "ue-v rejected with #15"
docker compose logs --no-color --since "$since" amf-v | sed 's/\x1b\[[0-9;]*m//g' | grep -i 'RAT restriction' | tail -1

step "ue-v subscriber: no RAT restriction (still on the NR(LEO) TA)"
dbctl rat_restrictions $IMSI
ue_restart
scripts/check-ue.sh ue-v
result $? "ue-v registered on NR(LEO), HR session, ping"

step "defaults: terrestrial gnb-v"
gnb gnb-v.yaml
ue_restart
scripts/check-ue.sh ue-v >/dev/null
result $? "ue-v registered on the terrestrial TA"

echo "== RESULT: $([ $rc = 0 ] && echo PASS || echo FAIL)"
exit $rc
