# Two-PLMN home-routed Open5GS deployment

Docker Compose deployment of this branch as two 5G cores, HPLMN 999-70 (the UEs' home) and
VPLMN 001-01, joined by SEPPs over N32 and by a home-routed N9 user plane. It exercises
home-routed roaming and the inter-PLMN N2 handover with N14 context transfer.

Images follow the upstream blueprints: `docker/ubuntu/noble/base` + `docker/build` (all NFs
and the `inter-plmn-hr-n2` test binary run from the build image), plus a UERANSIM image.
Configs start from `configs/examples/5gc-sepp1-999-70.yaml.in` / `5gc-sepp2-001-01.yaml.in`
and `configs/inter-plmn-hr-n2.yaml.in`.

## Quick start (from scratch)

Prerequisites: a Linux Docker host with Docker Engine and the Compose v2 plugin
(`docker compose`), about 15 GB free disk, the TUN device (`/dev/net/tun`, used by the UPFs
and UEs), and internet access for the image builds (Ubuntu packages, meson subprojects,
UERANSIM v3.2.8 from GitHub).

```sh
git clone -b inter-plmn-hr-n2-ho-fixes <this-repo-url> open5gs
# or, from a local checkout: git -C /path/to/checkout archive --prefix=open5gs/ HEAD | tar -x
cd open5gs/docker/inter-plmn-hr
scripts/up.sh        # build images, start both PLMNs, provision, check ue-h and ue-v
scripts/ho-test.sh   # inter-PLMN N2 handover 999-70 -> 001-01 with the test binary
scripts/down.sh      # remove containers, networks, volumes (--rmi: also the images)
```

`scripts/up.sh` builds only the images that are missing (`REBUILD=1` rebuilds all without
the build cache; `NINJA_JOBS` caps the Open5GS build, default 4), runs
`docker compose --profile vplmn up -d`, waits for mongo and the NRF registrations, runs
`scripts/provision.sh` and `scripts/restart-ran.sh`, then `scripts/check-ue.sh` for ue-h and
ue-v. It ends with

```
== images 614 s, bring-up 52 s
== RESULT: ue-h:PASS ue-v:PASS
```

and exits non-zero if a UE fails. `scripts/ho-test.sh` prints `SUCCESS` lines, `All tests
passed.` and `ho-test rc=0`. The scripts work from any directory. Times on a 16-vCPU host
with `NINJA_JOBS=4`: image build about 10 min (once; later runs reuse the images), bring-up to PASS about
1 min, ho-test about 20 s.

## Topology

```
          HPLMN 999-70 (net sbi-h 172.30.1.0/24)              VPLMN 001-01 (net sbi-v 172.30.2.0/24)
 mongodb-h  UDR-h  PCF-h  BSF-h  UDM-h  AUSF-h  NSSF-h            NSSF-v  PCF-v  UDR-v  mongodb-v
                        |                                                |
            NRF-h --- SCP-h --- SEPP-h ====== N32-c/f (ipx) ====== SEPP-v --- SCP-v --- NRF-v
                        |        172.30.3.10   172.30.3.0/24   172.30.3.20     |
                      AMF-h ------------ N14 (SCP/SEPP/N32) ------------ AMF-v
                      SMF-h ------------ N16a HR (SCP/SEPP/N32) -------- SMF-v
                        | N4                                             | N4
                      UPF-h (PSA, ogstun 10.45.0.0/16) <==== N9 ====== UPF-v (ogstun 10.46/16, unused)
                        ^ N3                                             ^ N3
              (net ran 172.30.4.0/24: NGAP to both AMFs, N3 + N9 GTP-U on both UPFs)
              gnb-h 999-70 TAC 1 --> AMF-h                    gnb-v 001-01 TAC 2 --> AMF-v
              ue-h  (UERANSIM)                                ue-v (UERANSIM, roaming)
```

Every SBI server binds to its 3GPP FQDN (`<nf>.5gc.mnc070.mcc999.3gppnetwork.org`,
`<nf>.5gc.mnc001.mcc001.3gppnetwork.org`), which is a Docker network alias on its PLMN's SBI
network. The SCP forwards a request to its SEPP when the target FQDN's PLMN is not its own,
so every cross-PLMN request crosses N32 (TS 29.500 §6.10, TS 23.003 §28.3.2.3); plain IPs
would bypass the SEPPs. Each PLMN has its own NRF with `serving:` set to that PLMN, from
which its NFs learn their serving PLMN. SBI uses http on port 80 (FQDN without port).

N9 rides the `ran` network, not `ipx`. An Open5GS UPF
advertises one GTP-U address per source interface, and at the H-UPF both N3 (pre-HO) and N9
(from the V-UPF) are on the access side, so a separate N9 address would need per-peer
routing in the UPF.

## Networks and addresses

| Container | sbi-h | sbi-v | ipx | ran | Notes |
|---|---|---|---|---|---|
| mongodb-h | .100 | | | | HPLMN subscriber DB (`mongodb://mongodb-h/open5gs`) |
| nrf-h / scp-h / sepp-h | .10 / .200 / .250 | | sepp-h .10 | | N32-c :7777, N32-f :7778 |
| amf-h | .5 | | | .5 | NGAP :38412, GUAMI 999-70/2/1/31, TAI 999-70/1 |
| smf-h | .4 | | | | UE pool 10.45.0.0/16 |
| upf-h | .7 (N4) | | | .7 (N3/N9) | ogstun 10.45.0.1/16 |
| ausf-h udm-h pcf-h nssf-h bsf-h udr-h | .11 .12 .13 .14 .15 .20 | | | | |
| mongodb-v | | .100 | | | V-PCF policy DB |
| nrf-v / scp-v / sepp-v | | .10 / .200 / .250 | sepp-v .20 | | |
| amf-v | | .5 | | .6 | GUAMI 001-01/2/1/30, TAI 001-01/2, `access_control` 999-70 |
| smf-v | | .4 | | | |
| upf-v | | .7 (N4) | | .8 (N3/N9) | ogstun 10.46.0.1/16 (placeholder, HR uses H pool) |
| nssf-v pcf-v udr-v | | .14 .13 .20 | | | |
| gnb-h / gnb-v | | | | .21 / .22 | UERANSIM, gNB IDs 0x1 / 0x2 (32 bit) |
| ue-h / ue-v | | | | .31 / .32 | UERANSIM UEs imsi-999700000000002 / ...003 |
| ho-test | .50 | | | .50 (+ .51) | `inter-plmn-hr-n2` test binary as gNB1/gNB2 + UEs imsi-999700000000020/030 (profile `ho`) |

All subnets are `172.30.{1,2,3,4}.0/24`. Only the optional `monitoring` and `webui` profiles
publish host ports (see Monitoring and WebUI).

## Build

`scripts/up.sh` builds missing images. By hand, from this directory (base first, the build
image starts `FROM` it):

```sh
docker compose --profile images build base             # ubuntu:noble + build deps + mongosh
NINJA_JOBS=4 docker compose --profile images build build   # meson build + install
docker compose --profile images build ueransim         # UERANSIM v3.2.8 from GitHub + rat-information.patch
UERANSIM_SRC=/path/to/ueransim docker compose --profile images build ueransim   # local v3.2.8 tree
```

Image names follow `docker/docker-compose.yml`: `$USER/ubuntu-noble-open5gs-base`,
`$USER/ubuntu-noble-open5gs-build`, plus `$USER/ueransim`. Use ubuntu noble: with debian
bookworm's libcurl 7.88.1 every SCP-forwarded request fails with `Error in the HTTP2 framing
layer`. Rebuild `build` after every source change, then `docker compose --profile vplmn up -d`
recreates the NF containers, and `docker compose --profile vplmn restart gnb-h gnb-v ue-h ue-v`
re-attaches the UERANSIM gNBs/UEs (they do not reconnect to a restarted AMF on their own).

## Up / down

The individual steps that `scripts/up.sh` and `scripts/down.sh` automate:

```sh
docker compose up -d                          # HPLMN + gnb-h + ue-h
docker compose --profile vplmn up -d          # + VPLMN + gnb-v + ue-v
scripts/provision.sh                          # subscribers (needs mongodb-h, and mongodb-v if up)
scripts/restart-ran.sh                        # after NF containers were recreated
docker compose --profile vplmn ps
docker compose logs -f amf-h sepp-h           # NF logs go to stdout
docker compose --profile vplmn stop           # stop, keep containers
docker compose --profile vplmn --profile ho down -v   # remove containers, networks, volumes
```

NFs start in any order: SBI clients retry until the SCP/NRF are up, and the PCF/UDR exit and
are restarted (`restart: unless-stopped`) until mongo accepts connections, so give the core
~20 s after `up` before starting UEs (or restart them); `scripts/up.sh` polls instead.

## Subscribers

Subscribers live only in the HPLMN (`mongodb-h`, read by UDR-h). `scripts/provision.sh` adds
imsi-999700000000001..005, ...020 and ...030 (the `inter-plmn-hr-n2` test UEs) with the fork test UE's K `465b5ce8b199b49faa5f0a2ee238a6bc` and
OPc `e8ed289deba952e4283b54e88e6183ca`, DNN `internet`, SST 1 (`open5gs-dbctl
add_ue_with_apn`; the build image has `open5gs-dbctl` and `mongosh`). It removes each IMSI first,
so it can be re-run.

`scripts/provision.sh FILE` instead runs one `open5gs-dbctl` command per line of FILE in one
container against mongodb-h. Use IMSIs starting with 99970 and DNN `internet` (the only DNN
the SMFs serve). PCF-h takes policy from the UDR, so no `supi_range` needs to match.

## Checks

| Case | Command | Pass |
|---|---|---|
| HPLMN | `scripts/check-ue.sh ue-h` | `Initial Registration is successful`, `PDU Session establishment is successful`, `uesimtun0, 10.45.0.x`, ping 10.45.0.1 0 % loss |
| HR roaming | `scripts/check-ue.sh ue-v` (UE 999-70 on gnb-v 001-01) | same as HPLMN, address from the H-SMF pool 10.45/16; `docker compose logs amf-v \| grep Home-Routed`; `docker compose logs sepp-v \| grep established` (N32-c) |
| N2 HO 999-70 -> 001-01 | `scripts/ho-test.sh` | `SUCCESS` / `All tests passed.` (details below) |
| NR(LEO) barring | `scripts/ntn-barring-demo.sh` | `== RESULT: PASS` (details below) |

### Inter-PLMN N2 handover with the `inter-plmn-hr-n2` test binary

UERANSIM has no N2 handover, so `scripts/ho-test.sh` runs this branch's test binary (service
`ho-test`, profile `ho`, from the build image) against the compose: it is gNB1 (0x4000,
999-70 TAC 1, N2 to amf-h) and gNB2 (0x4001, 001-01 TAC 2, N2 to amf-v) at 172.30.4.50, with
N3 at .50/.51 (`TEST_GNB1_ADDR`/`TEST_GNB2_ADDR`), and UE imsi-999700000000020. Sequence:
NG Setup x2, registration + PDU session on 999-70 (H-SMF/H-UPF), Handover Required to
0x4001 / TAI 001-01-2, target AMF via N14 over the SEPPs, V-SMF/V-UPF insertion with
RetrieveSMContext, Handover Request/Ack, Handover Command, RAN status transfer, Handover
Notify, End Marker from the H-UPF on the old N3, source UE Context Release, ping through
gNB2 -> V-UPF -> N9 -> H-UPF, then (`TEST_HR_MRU=1`, set in the compose) the Mobility
Registration Update at amf-v (H-UDM registration and V-PCF AM policy over N32,
Registration Accept/Complete, Configuration Update) and a second ping that must return a
G-PDU, then release. `configs/ho-test.yaml` gives the binary the two AMFs' NGAP addresses,
TAIs and PLMNs and mongodb-h; all `no_*` flags are set so it starts no NFs itself.

The same binary then runs the idle-mode case with UE imsi-999700000000030: registration on
gNB1, UE Context Release, then a Registration Request of type mobility registration updating
with the amf-h 5G-GUTI on gNB2. amf-v discovers the old AMF by the GUAMI through the SEPPs
and fetches the UE context (Namf_Communication_UEContextTransfer, TS 23.502 4.2.2.2.2 steps
4-5), authenticates without an Identity Request, and accepts. The UE then goes idle and
returns to gNB1 with the amf-v 5G-GUTI: amf-h fetches the context from amf-v the same way
and authenticates with the H-AUSF (the home PLMN comes from the SUPI).

The script restarts amf-v (and re-attaches gnb-v/ue-v) first: Open5GS's UDM does not send
Nudm_UECM_DeregistrationNotify to the old AMF, so a previous run's UE context would stay
in amf-v and the next CreateUEContext would be refused. A handover cancel case is not in
the test binary.

UERANSIM only selects a non-home PLMN that the network sent as an equivalent PLMN, and Open5GS
sends none, so `ueransim/Dockerfile` drops that condition with a one-line `sed` (automatic
network selection may pick any available non-forbidden PLMN, TS 23.122 §4.4.3.1.1).

### NR satellite access barring (`scripts/ntn-barring-demo.sh`)

`ueransim/rat-information.patch` (applied in `ueransim/Dockerfile`; the build fails if it
does not apply) adds an optional gNB config key `ratInformation` (`unlicensed`, `nb-IoT`,
`nR-LEO`, `nR-MEO`, `nR-GEO`, `nR-OTHERSAT`). When set, the gNB's NG Setup Request carries it as
the RAT Information of its Supported TA Item (TS 38.413 9.3.1.125), and the AMF takes the
RAT Type of UEs in that TA from it (TS 23.501 5.4.10). `ueransim/gnb-v-leo.yaml` is gnb-v
with `ratInformation: nR-LEO`; `GNB_V_CONFIG=gnb-v-leo.yaml` selects it. The default is the
terrestrial `gnb-v.yaml`. After updating the tree, rebuild the image with
`docker compose --profile images build ueransim`, because `scripts/up.sh` only builds missing images.

`scripts/ntn-barring-demo.sh` runs against the running deployment:

1. gnb-v is recreated with `gnb-v-leo.yaml`, so TA 001-01/2 is NR(LEO);
2. `open5gs-dbctl rat_restrictions 999700000000003 NR_LEO` bars ue-v from NR(LEO); ue-v is
   restarted and its registration is rejected with 5GMM cause #15 (UERANSIM logs
   `Initial Registration failed [NO_SUITIBLE_CELLS_IN_TA]`) and amf-v logs
   `Registration rejected due to RAT restrictions`;
3. `open5gs-dbctl rat_restrictions 999700000000003` clears it; ue-v registers on the NR(LEO)
   TA, sets up its home-routed session and pings 10.45.0.1 (`scripts/check-ue.sh ue-v`);
4. gnb-v goes back to `gnb-v.yaml` and ue-v registers again.

It prints `PASS`/`FAIL` per step and `== RESULT: PASS` (exit code 0) at the end. A UE with
RAT restrictions that is accepted gets them in the Mobility Restriction List of Initial
Context Setup Request and Handover Request (TS 38.413 9.3.1.85). UERANSIM ignores that list.

## Monitoring and WebUI

Two opt-in compose profiles; both publish their ports on `$BIND_ADDR` (default `127.0.0.1`,
so use an SSH port forward from another machine, or `BIND_ADDR=0.0.0.0`).

```sh
MONITORING=1 WEBUI=1 scripts/up.sh                        # from scratch, with both profiles
docker compose --profile monitoring --profile webui up -d   # add them to a running deployment
```

| Service | Profile | Host port (env, default) | Login |
|---|---|---|---|
| Grafana | `monitoring` | `GRAFANA_PORT` 13000 | anonymous viewer; admin / `$GRAFANA_ADMIN_PASSWORD` (default `open5gs`) |
| Prometheus | `monitoring` | `PROMETHEUS_PORT` 19190 | none |
| WebUI-h (mongodb-h, HPLMN subscribers) | `webui` | `WEBUI_H_PORT` 9999 | admin / 1423 |
| WebUI-v (mongodb-v, the V-PCF's copy) | `webui` | `WEBUI_V_PORT` 9998 | admin / 1423 |

**Metrics.** The AMF, SMF, UPF and PCF of both PLMNs serve Prometheus metrics on `:9090`
(`metrics:` in `configs/*.yaml`, address `0.0.0.0`; config changes need a restart of the NF).
Prometheus (`monitoring/prometheus.yml`, 5 s scrape, 24 h retention) scrapes the eight
endpoints as job `open5gs` with labels `plmn="999-70"|"001-01"` and `nf="amf"|"smf"|"upf"|"pcf"`;
`up{job="open5gs"}` shows which are reachable (the VPLMN targets are down without the
`vplmn` profile).

**Dashboard.** Grafana opens on the provisioned "Open5GS 5GC" dashboard
(`monitoring/dashboards/open5gs-5gc.json`). Every panel repeats on the `plmn` variable, so
999-70 and 001-01 sit side by side; one row per NF:

| Row | Panel | Metrics |
|---|---|---|
| AMF | gNBs, UEs, registered subscribers | `gnb` (NG Setup done), `ran_ue`, `fivegs_amffunction_rm_registeredsubnbr`, `amf_session` |
| AMF | registrations per minute | `fivegs_amffunction_rm_reg{init,mob,period}{req,succ,fail}` |
| AMF | registration success ratio (5 min) | succ / req per registration type |
| AMF | authentication per minute | `fivegs_amffunction_amf_auth{req,fail,reject}` |
| SMF | sessions, UEs, QoS flows, PFCP | `fivegs_smffunction_sm_sessionnbr`, `ues_active`, `fivegs_smffunction_sm_qos_flow_nbr`, `pfcp_sessions_active`, `pfcp_peers_active` |
| SMF | PDU session creation per minute | `fivegs_smffunction_sm_pdusessioncreation{req,succ,fail}` |
| UPF | sessions, QoS flows, PFCP peers | `fivegs_upffunction_upf_sessionnbr`, `fivegs_upffunction_upf_qosflows`, `pfcp_peers_active` |
| PCF | policy associations | `fivegs_pcffunction_pa_policy{am,sm}asso{req,succ}`, `fivegs_pcffunction_pa_sessionnbr` |

In a home-routed session the V-SMF/V-UPF count the roamer's session as well as the H-SMF/H-UPF.

**JSON info API.** The same `:9090` servers answer `/gnb-info`, `/ue-info` (AMF) and
`/pdu-info` (SMF), paged with `?page=N&page_size=M`
(`docs/_docs/tutorial/07-infoAPI-UE-gNB-session-data.md`). From inside the networks, e.g.
from the Prometheus container:

```sh
docker compose exec prometheus wget -qO- http://amf-h:9090/gnb-info   # gnb-h, its TAs, UE count
docker compose exec prometheus wget -qO- http://amf-v:9090/ue-info    # roamers on 001-01
docker compose exec prometheus wget -qO- http://smf-h:9090/pdu-info   # sessions, IPs, QoS flows
```

**AMF PLMN API.** The AMF also serves `namf-oam/v1/plmns` on its SBI (HTTP/2,
`http://amf.5gc.mnc070.mcc999.3gppnetwork.org/namf-oam/v1/plmns` on sbi-h) to list, add and
delete served PLMNs at run time; see `docs/_docs/tutorial/08-AMF-API-addAndDelete-new-PLMN.md`.

**WebUI.** One WebUI per subscriber DB, built from this tree's `webui/` with
`docker/webui/Dockerfile` (image `$USER/open5gs-webui`, rebuilt from the cache on each `up`).
`scripts/up.sh` creates the `admin` account in both DBs with upstream's
`docs/assets/webui/mongo-init.js` (only when no account exists); without `scripts/up.sh`, run
`docker compose exec -T mongodb-h mongosh < ../../docs/assets/webui/mongo-init.js` (same for
mongodb-v). WebUI-h shows the `scripts/provision.sh` subscribers and edits what UDR-h serves.
WebUI-v edits mongodb-v, which only the V-PCF reads (Known issues), so subscribers added there
cannot register.

## Known issues and limits

- The V-PCF reads the roamer's UE-AMBR from its own DB (`ogs_dbi_subscription_data` in
  src/pcf/nudr-handler.c), so `scripts/provision.sh` copies the subscribers into mongodb-v.
  A real VPLMN PCF would not hold home subscription data.
- Open5GS's UDM sends no Nudm_UECM_DeregistrationNotify to an old AMF when the UE registers
  elsewhere (TS 23.502 4.2.2.2.2 step 14), so stale UE contexts stay in the old AMF
  (`scripts/ho-test.sh` restarts amf-v for that reason).
- At handover the target AMF has no AM policy association yet and logs `No pcf_nf_instance`
  (src/amf/nsmf-build.c) when it builds CreateSMContext; the association is created by the
  Mobility Registration Update.
- SCP-v logs `Cannot find client [type:UDM ...]` once when its cached H-UDM profile has
  expired (30 s validity); it rediscovers and the request succeeds.
- N9 GTP-U uses the `ran` network (both UPFs have one GTP-U address).
- UERANSIM only selects a non-home PLMN listed as equivalent; `ueransim/Dockerfile` drops
  that condition (see above). UERANSIM gNBs/UEs do not reconnect to restarted AMFs.
- No handover cancel test; NTN -> TN (back to the HPLMN) handover is not implemented in the
  branch.
