#!/bin/sh
# provision.sh [FILE]: write subscribers with open5gs-dbctl into mongodb-h (HPLMN UDR) and,
# when the VPLMN is up, into mongodb-v (the V-PCF reads roamers' UE-AMBR from its own DB).
#   no FILE: imsi-999700000000001..005, ...020 and ...030 (the inter-plmn-hr-n2 test UEs of the
#            handover and idle mobility tests; the V-PCF needs them in mongodb-v), all with
#            the test UE's K/OPc
#   FILE:    one open5gs-dbctl command per line, e.g. `open5gs-dbctl add_ue_with_apn <imsi> <k> <opc> internet`
# Re-running is safe: each IMSI is removed before it is added.
cd "$(dirname "$0")/.." || exit 1
K=465b5ce8b199b49faa5f0a2ee238a6bc OPC=e8ed289deba952e4283b54e88e6183ca
if [ -n "$1" ]; then
  cmds=$(awk '/^open5gs-dbctl add/ { print "open5gs-dbctl remove " $3 } { print }' "$1")
else
  cmds=$(for i in 01 02 03 04 05 20 30; do
    echo "open5gs-dbctl remove 9997000000000$i"
    echo "open5gs-dbctl add_ue_with_apn 9997000000000$i $K $OPC internet"
  done)
fi
for db in mongodb-h mongodb-v; do
  [ -n "$(docker ps -q -f name=^open5gs-hr-$db-1$)" ] || continue
  echo "$cmds" | docker compose run --rm -T -e DB_URI=mongodb://$db/open5gs dbctl sh -s >/dev/null
  echo "$db subscribers: $(docker exec open5gs-hr-$db-1 mongosh --quiet open5gs --eval 'db.subscribers.countDocuments()')"
done
