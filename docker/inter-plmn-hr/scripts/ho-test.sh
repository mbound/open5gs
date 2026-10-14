#!/bin/sh
# ho-test.sh: Runs the inter-plmn-hr-n2 test binary (service ho-test) as
# two gNBs + one UE against the running compose: registration + PDU session on 999-70, N2 HO
# to 001-01, end marker, source release, ping via V-UPF/N9, Mobility Registration Update at
# the V-AMF (TEST_HR_MRU), release. Output: ho-test.log here.
cd "$(dirname "$0")/.." || exit 1
# A previous run leaves the test UE registered at the V-AMF, and Open5GS's UDM does not
# send Nudm_UECM_DeregistrationNotify to the old AMF when the UE registers at the H-AMF
# again (TS 23.502 4.2.2.2.2 step 14), so the next CreateUEContext would be refused
# ("Target AMF already has a UE context"). Start from a clean V-AMF.
docker compose --profile vplmn restart amf-v >/dev/null 2>&1; sleep 5
docker compose --profile vplmn restart gnb-v >/dev/null 2>&1; sleep 3
docker compose --profile vplmn restart ue-v >/dev/null 2>&1; sleep 5
timeout 300 docker compose --profile vplmn --profile ho run --rm ho-test > ho-test.log 2>&1
rc=$?
docker ps -aq -f name=open5gs-hr-ho-test-run | xargs -r docker rm -f >/dev/null
sed 's/\x1b\[[0-9;]*m//g' ho-test.log | grep -E 'SUCCESS|FAILED|Line [0-9]+:|All tests passed|Failed'
echo "ho-test rc=$rc"
