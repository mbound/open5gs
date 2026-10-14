#!/bin/sh
# up.sh: from-scratch bring-up. Builds the images that are missing (base, build, ueransim),
# starts both PLMNs (profile vplmn), waits for mongo and the NRF registrations, provisions
# the subscribers, re-attaches the UERANSIM gNBs/UEs and checks ue-h (HPLMN) and ue-v
# (home-routed roaming). Exit code 0 only if both UEs pass.
#   REBUILD=1 scripts/up.sh    rebuild all images without the Docker build cache
#   NINJA_JOBS=N               build parallelism of the Open5GS build (default 4)
#   MONITORING=1               also start Prometheus + Grafana (profile monitoring)
#   WEBUI=1                    also start the WebUIs of both PLMNs (profile webui)
cd "$(dirname "$0")/.." || exit 1
set -e
t0=$(date +%s)
img=${USER:-open5gs}/${DIST:-ubuntu}-${TAG:-noble}-open5gs
# build SERVICE IMAGE [FORCE]: build if the image is missing, REBUILD is set or FORCE
build() {
  if [ -n "$REBUILD$3" ] || ! docker image inspect "$2" >/dev/null 2>&1; then
    echo "== building $2"
    docker compose --profile images build ${REBUILD:+--no-cache} "$1"
    built=1
  fi
}
built=
build base $img-base
build build $img-build $built   # a new base image needs a new build image
build ueransim ${USER:-open5gs}/ueransim
t1=$(date +%s)

profiles="--profile vplmn"
[ "${MONITORING:-0}" = 1 ] && profiles="$profiles --profile monitoring"
[ "${WEBUI:-0}" = 1 ] && profiles="$profiles --profile webui"
echo "== starting containers ($profiles)"
docker compose $profiles up -d
# wait_for SECONDS WHAT CMD...: poll CMD every 2 s
wait_for() {
  n=$1 what=$2; shift 2
  until "$@" >/dev/null 2>&1; do
    n=$((n - 2)); [ "$n" -gt 0 ] || { echo "timeout waiting for $what"; exit 1; }
    sleep 2
  done
}
registered() { [ "$(docker compose logs nrf-$1 2>/dev/null | grep -c 'NF registered \[Heartbeat')" -ge "$2" ]; }
for db in mongodb-h mongodb-v; do
  wait_for 120 "$db" docker compose exec -T $db mongosh --quiet --eval 'db.runCommand({ping: 1})'
  # WebUI login admin/1423 (upstream docs/assets/webui/mongo-init.js; only if no account exists)
  docker compose exec -T $db mongosh --quiet < ../../docs/assets/webui/mongo-init.js >/dev/null
done
# HPLMN: scp sepp amf smf ausf udm pcf nssf bsf udr; VPLMN: scp sepp amf smf pcf nssf udr
wait_for 180 "NF registrations at nrf-h" registered h 10
wait_for 180 "NF registrations at nrf-v" registered v 7
echo "== core ready after $(( $(date +%s) - t1 )) s"

scripts/provision.sh
scripts/restart-ran.sh
rc=0 summary=
for ue in ue-h ue-v; do
  echo "== $ue"
  n=0
  until scripts/check-ue.sh $ue; do
    n=$((n + 1)); [ $n -lt 4 ] || break
    sleep 5
  done
  if [ $n -lt 4 ]; then summary="$summary $ue:PASS"; else summary="$summary $ue:FAIL"; rc=1; fi
done
echo "== images $((t1 - t0)) s, bring-up $(( $(date +%s) - t1 )) s"
echo "== RESULT:$summary"
exit $rc
