# EPC S10, EN-DC (NSA Option 3), and N26 implementation plan

Status: S10 message codecs, static MME peer transport, PLMN/TAI route lookup, basic GTPv2-C transaction correlation and negative responses implemented and tested; **UE mobility, session transfer and S1 handover state machines remain unimplemented**.
Baseline: [upstream integration RC1](https://github.com/mbound/open5gs/tree/integration/upstream-2026-11-05-rc1), commit `b5c8abdd05a9a2f487b8c61e72cbf67425375fd6`.
Development branch: `feature/s10-gtpv2-control-plane`.

## Scope and architecture

Implement LTE/EPC inter-MME S1-based handover over **S10** first. Keep this separate from the already implemented 5GC **N14** inter-AMF N2 handover. Add the EPS side of 5G NSA Option 3/EN-DC as an EPC and E-UTRAN integration concern, not as a new core-to-RAN N2 interface. Then implement **N26** for AMF–MME 5GS/EPS interworking, reusing the GTPv2-C transport and relevant message codecs but not assuming S10 and N26 have identical mandatory IEs or security context semantics.

Primary references: 3GPP TS 23.401 (EPS mobility); TS 29.274 (GTPv2-C message types/IEs; S10/N26); TS 36.413 (S1AP); TS 36.423 (X2AP, EN-DC applicability); TS 23.502 §4.11 (5GS/EPS interworking); TS 33.401 and TS 33.501 (security and context/key mappings).

### Existing Open5GS entry points

- `lib/gtp/v2/message.h`: now includes generated mobility codecs for types 128–140; transfer of the MM Context as structured EPS security state is still missing.
- `src/mme/s1ap-handler.c`: current S1 handover path resolves a target eNB locally. Extend to select another MME from target TAI and route a Forward Relocation Request over S10.
- `src/mme/mme-sm.c`, `src/mme/mme-gtp-path.c`: EPC state machine and existing S11/Gn dispatch; introduce a dedicated S10 control-plane path, peer configuration, and transaction correlation.
- `src/mme/mme-context.h`: persisted NAS, KASME/KeNB and EPS bearer state must be exported/imported with ownership, lifetime and replay-counter semantics.
- `src/sgwc`, `src/sgwu`, `src/smf`, `src/upf`: reuse existing S11/S5/S8/PFCP forwarding and bearer setup rather than introducing independent packet-anchor state.

## Incremental milestones

### Implemented S10 codec checkpoint (12 November 2026)

- TS 29.274 V19.6.0 codec generation now covers GTPv2-C mobility messages
  128–140: Identification, Context, Forward Relocation, Forward Access
  Context and Relocation Cancel families.
- Generated message structures and descriptors support repeated EPS PDN
  Connections (up to `OGS_MAX_NUM_OF_SESS`) and nested bearer contexts
  (up to `OGS_BEARER_PER_UE`), including additional repeated bearer lists.
- The existing GTPv2 message build/parse dispatch is extended, rather than
  adding a disconnected proprietary S10 parser.
- Unit tests cover Forward Relocation, two PDN connections, three nested
  bearers, MM Context as an opaque IE, Context Response, Relocation Cancel
  Response and full GTPv2 packet-header parsing.
- Full Meson/Ninja release-mode build and all four unit suites passed on
  `rdc-worker-02`; generator output was reproduced from the versioned
  `lib/gtp/v2/support/r19.6.0/29274-j60.docx` source.

**Limitations:** The MM Context IE is currently transported as opaque bytes:
KASME/NAS security state serialization and validation are not implemented.
Neither S10 peer connectivity nor UE context migration or RAN handover is
implemented. Codec round-trips alone do not establish interoperability.

### S10 MME transport groundwork (12 November 2026)

- The MME now accepts an optional `mme.gtpc.client.mme` peer list
  with `address`, `port` (default GTP-C port 2123), and `family`.
  A commented configuration example is in `configs/open5gs/mme.yaml.in`.
- Dedicated `mme_s10_peer_t` objects and receive events separate
  explicit S10 peer endpoints from S11 SGW endpoints.
- Source-address matching limits the S10 receive path to configured
  peers; overlapping S11/S10 peer IP/port addresses are rejected.
- **Initial receive-only stub (now superseded by the 13 November checkpoint):**
  S10 messages were decoded and discarded without responses or mobility
  handling. No per-UE state was allocated.
- GTP-C receive now rejects datagrams shorter than the basic header.

This was peer-transport groundwork. See below for transaction processing
and target-TAI routing, which are now partially implemented.

### S10 transaction, reject-path and target-TAI checkpoint (13 November 2026)

The S10 codec and peer layers have been connected to the existing
`lib/gtp/xact.c` GTPv2-C transaction engine, keeping S10 peer
transactions **isolated from S11 SGW and UE-session state**.

Implemented:

- GTPv2-C transaction stage classification for Identification,
  Context Request/Response/Acknowledge, Forward Relocation,
  Forward Relocation Complete, Forward Access Context and
  Relocation Cancel (message types 128–140). The three-message
  Context Request → Context Response → Context Acknowledge
  exchange now correlates the final acknowledgement against the
  remote-originator transaction rather than a local S11 transaction.
- Configured S10 peer endpoints respond to GTP Echo Requests.
  Unsupported mobility requests receive an explicit **Service Not
  Supported** cause in the corresponding GTPv2 response/acknowledgement
  message. Responses use the sender F-TEID where available;
  **the incoming header TEID is never mistaken for the remote TEID**.
  Unsolicited responses are ignored.
- Duplicate requests are handled by the transaction engine's
  cached-response replay and holding timers. Rejected Context
  Transfers clean up on Context Acknowledge.
- Static target-TAI selection via `mme.gtpc.client.mme[].tai`, matching
  exact MCC, **MNC length**, and LTE TAC. Unknown target TAI is not
  assigned an arbitrary peer. Duplicate target TAIs and overlapping
  S10/S11 or S10/S10 remote endpoints are rejected.
- Loopback UDP unit tests verify actual Echo Response, Forward
  Relocation rejection, sender F-TEID/response TEID, cause,
  sequence numbers, Context Response replay and Context
  Acknowledge cleanup. Additional unit tests exercise transaction
  stages and exact TAI peer lookup. Full Meson/Ninja build and
  all four unit executables pass on `rdc-worker-02`.

**Still not implemented:** any S10 request that can transfer a UE or
execute a handover; EPS security context export/import, local S10 TEID
allocation and lifecycle, remote-MME selection wired into
S1AP Handover Required, bearer/PDN reconstruction and S-GW switching,
rollback/cancel handling, and live two-MME/two-eNB validation.
Static source IP/port allowlisting is **not** cryptographic peer
authentication. Production or inter-operator S10 requires secure
inter-node transport (e.g. IPsec according to operator deployment)
and proper trust boundaries.

### M1-A: Codec and transport foundation

- Add TS 29.274 mobility message types 128–140, including Context Request/Response/Acknowledge, Forward Relocation Request/Response/Complete, Forward Access Context Notification/Acknowledge, and Relocation Cancel.
- Define typed IE/message structures and codecs with bounds checking, unknown-IE tolerance where permitted, and complete failure-cause handling.
- Add S10 peer configuration and selection (PLMN/TAI → peer MME), local UDP GTPv2-C endpoint, transaction correlation/retransmission, timer policy, and per-peer TEID allocation.
- Include tests for malformed/truncated IEs, duplicate transactions, unknown TEIDs, timeouts, forward-relocation and context message round trips.

**Gate:** transport + codec tests pass; no UE state/production S1 behavior changed beyond dispatch and configuration.

### M1-B: Inter-MME S1 handover in a single PLMN

1. Source MME receives S1AP Handover Required for a target eNB served by a different MME; resolves target TAI/peer and serializes UE EPS security, session and bearer context.
2. Source sends S10 Forward Relocation Request; target MME allocates UE context, prepares target bearer forwarding and issues S1AP Handover Request.
3. Target returns S10 Forward Relocation Response; source issues Handover Command, maintains source state until completion/cancel.
4. Target receives Handover Notify, sends Forward Relocation Complete Notification, coordinates S11 Modify Bearer with the appropriate S-GW and completes cleanup/acknowledgement.
5. Support direct and indirect data forwarding, unchanged S-GW and S-GW relocation cases in separate iterations; preserve the UE's PDN connection and IP where procedure permits.
6. Implement pre-handover cancellation, timeout and transaction replay safety, including resource reclamation on source and target.

**Gate:** two-MMEs + two-eNB lab test: same PDN IP and user plane before/after HO, no duplicate/default bearer, context release, cancel, reverse HO, SGW relocation, retransmits and End Marker.

### M2: NSA Option 3/EN-DC interoperability

- Verify EPC/eNB S1-MME signaling and subscription/feature negotiation with an EN-DC capable E-UTRAN master node; validate bearer handling, secondary-node procedures and RAN-facing integration.
- Reuse the existing LTE EPC attach and bearer path, adding only demonstrated missing core control-plane behavior.
- Treat EPC↔5GC system transition as separate **N26** work; EN-DC by itself does not add an AMF.

**Gate:** test EPC anchor continuity across master-node handover with secondary NR connectivity and ordinary LTE-only fallback.

### M3: N26 AMF↔MME interworking

- Build the AMF-side GTPv2-C control-plane peer and map NAS/security contexts per TS 33.401/33.501 rather than copying keys or counters blindly.
- Support EPS→5GS and 5GS→EPS connected-mode mobility; handle SGW/PGW-C–SMF mapping, bearer/QFI mappings and network-triggered update paths.
- Scope initial interworking to supported IP PDU/PDN types and session continuity assumptions; reject unsupported cases explicitly.
- Add registration/TAU and idle mobility cases after connected-mode handover.

**Gate:** end-to-end EPS→5GS→EPS UE session preservation, correct NAS re-keying and counters, supported QoS mapping, cancellation/rollback, and no leaks.

### M4: Inter-PLMN scenarios

Only after local-PLMN S10/N26 success, implement/test remote-PLMN peer discovery, security/trust domains, inter-PLMN bearer anchor/roaming constraints and handover restrictions. Do not conflate EPC home-routed roaming with the already validated 5GC V-SMF/H-SMF architecture.

## Compatibility and promotion

- The canonical `inter-plmn-hr-n2-ho` branch stays untouched until 5GC HR/N14 regressions pass against the upstream-integrated code.
- Use targeted feature branches and keep 5GC, EPC and mixed-mode regression suites in CI.
- **5GC regression gate:** On 5 November the worker ran the two 5GC
  mobility tests; both aborted before handover due to unresolved
  `*.5gc.mnc070.mcc999.3gppnetwork.org` service hostnames, not a proven
  mobility defect. The repository includes `configs/inter-plmn-hr-n2-hosts.txt`.
  Fix lab host resolution and rerun before promoting RC1.
- The spec corpus and expertise/reference materials are in `mbound/3gpp-oran-standards-tools`; use its `3gpp-specs` MCP lookup to check exact IE optionality, message tables and release-specific variants before finalizing codecs.

## Standards and source links

- ETSI/3GPP TS 29.274: https://www.etsi.org/deliver/etsi_ts/129200_129299/129274/
- ETSI/3GPP TS 23.401: https://www.etsi.org/deliver/etsi_ts/123400_123499/123401/
- ETSI/3GPP TS 23.502: https://www.etsi.org/deliver/etsi_ts/123500_123599/123502/
- 3GPP reference tools: https://github.com/mbound/3gpp-oran-standards-tools
