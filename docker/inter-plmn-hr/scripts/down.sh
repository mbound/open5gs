#!/bin/sh
# down.sh [--rmi]: remove all containers, networks and volumes of both PLMNs and the ho-test.
#   --rmi   also remove the images the compose uses (Open5GS, UERANSIM, mongo)
cd "$(dirname "$0")/.." || exit 1
[ "$1" = --rmi ] && set -- --rmi all
docker compose --profile vplmn --profile ho --profile tools --profile images down -v --remove-orphans "$@"
