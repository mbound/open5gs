# UERANSIM gNB/UE image. Build context = this directory; the UERANSIM source tree comes in
# as the additional context `ueransim-src`: by default the v3.2.8 tag from GitHub, or a local
# tree via UERANSIM_SRC (docker-compose.yml).
FROM ubuntu:24.04 AS build
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    make g++ cmake libsctp-dev && rm -rf /var/lib/apt/lists/*
COPY --from=ueransim-src . /src
# Roaming: UERANSIM only selects a non-home PLMN that the network listed as equivalent,
# and Open5GS sends no Equivalent PLMNs. In automatic mode TS 23.122 §4.4.3.1.1 lets the
# UE select any available, non-forbidden PLMN, so drop the equivalent-PLMN condition.
RUN sed -i 's/if (m_storage->equivalentPlmnList->contains(plmn))/if (true)/' /src/src/ue/nas/mm/radio.cpp \
    && grep -q 'if (true)' /src/src/ue/nas/mm/radio.cpp
RUN cd /src && rm -rf b cmake-build-* && cmake -DCMAKE_BUILD_TYPE=Release -B b . \
    && cmake --build b -j4 --target nr-gnb nr-ue nr-cli

FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    libsctp1 iproute2 iputils-ping tcpdump && rm -rf /var/lib/apt/lists/*
COPY --from=build /src/b/nr-gnb /src/b/nr-ue /src/b/nr-cli /usr/local/bin/
