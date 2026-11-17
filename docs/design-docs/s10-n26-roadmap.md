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

### M1-B: LTE inter-MME S1 handover MVP, including inter-PLMN

**Priority P0 — this is the current implementation target.** Inter-PLMN
operation and session continuity are not a later optional milestone.
The normative procedure is 3GPP TS 23.401 §5.5.1.2.2, with S10
GTPv2-C IEs defined in TS 29.274 §§7.3.1–7.3.4, EPC security in
TS 33.401, and S1AP procedures in TS 36.413.

#### MVP deployment and acceptance scope

The first end-to-end environment comprises two MMEs and two eNBs in
**different PLMNs**, distinct S-GWs (SGW relocation mandatory in this
initial profile), and a **common home P-GW** reachable by both S-GWs via
the appropriate S5/S8 arrangements. Start with a UE that is allowed to
roam, one IPv4 PDN connection, one active default EPS bearer, and a
conventional S1-based handover. Do not accept an apparent 'handover
success' that actually re-establishes the PDN connection and reallocates
the IP. This is a scoped initial profile, not an assertion that all
inter-PLMN configurations share the same HPLMN.

**Acceptance condition**: a continuous bidirectional IP flow survives
PLMN-A/eNB-A/MME-A/SGW-A → PLMN-B/eNB-B/MME-B/SGW-B, preserving the
same P-GW anchor, UE PAA/IP address and PDN session, then survives
handover in the reverse direction. Packet loss during the interruption
must be measured; the required result is *session continuity*, not a
claim of strictly zero lost packets.

#### Mandatory source-MME procedure

1. In `src/mme/s1ap-handler.c`, parse the target eNB Global ID and
   selected target TAI from the **S1AP Handover Required** message.
   Retain the existing local-eNB/intra-MME path unchanged when
   appropriate. For remote TAI, select `mme_s10_peer_find_by_tai()`,
   enforce inter-PLMN roaming authorization, and reject unknown or
   ambiguous targets. Do not assume target eNB belongs to the source
   MME's locally attached eNB list.
2. Create a **per-UE S10 source relocation object** with an immutable
   source UE reference, target TAI/PLMN/eNB ID, S10 peer, transaction ID,
   source/target S10 control TEIDs, target SGW identity, bearer
   admission, and a rollback/cleanup timer. Its lifecycle must not
   depend on a dangling raw pointer after UE removal.
3. Serialize a proper **MM Context (EPS Security Context)** from the
   live security state (KASME/eKSI, NAS UL/DL COUNT, capabilities,
   selected algorithms, NH and NCC as specified), following TS 29.274
   §8.38 / TS 33.401. Do not memcpy a C struct into a vendor-defined
   opaque MM Context IE, and do not use `mme_ue_memento_t` as the
   on-wire format: that memento explicitly does not preserve NCC.
   Protect keys as sensitive data and never log them.
4. Serialize the linked **PDN Connection** including APN, PDN Type/PAA,
   P-GW S5/S8-C F-TEID and address, EBI and bearer QoS, S-GW bearer
   parameters, AMBR and the remaining mandatory/conditional IEs per
   TS 29.274. The initially supported profile is one IPv4 PDN and one
   default EBI; reject additional unsupported sessions/bearers
   explicitly rather than silently dropping them.
5. Build and send **Forward Relocation Request** (133) with sender S10
   control-plane F-TEID, subscriber/session context, selected PLMN,
   target ID, Source-to-Target RAN transparent container and cause.
   Start GTPv2-C transaction/timer handling using the existing shared
   engine. Do not issue a source S1AP Handover Command until a
   successful, admitted target response has been received.
6. Consume **Forward Relocation Response** (134), checking the cause,
   matching source transaction and TEIDs, admitted EBI(s), target
   transparent container and forwarding information. On rejection or
   timeout, send S1AP Handover Preparation Failure and keep the
   source S1-U/PDN session intact.
7. On a successful target response, issue **S1AP Handover Command** to
   the source eNB. Coordinate forwarding/status transfer; retain
   source-side context until the matching Forward Relocation Complete
   exchange and cleanup conditions have been satisfied.
8. Support source S1AP Handover Cancel with S10 Relocation Cancel,
   including idempotent rollback; delayed/duplicate S10 responses
   must not resurrect a canceled UE context.

#### Mandatory target-MME procedure

1. Authorize the source peer and verify the target PLMN/TAI before
   accepting Forward Relocation Request; parse and validate the complete
   MM Context and PDN Connection. Reject malformed or unsupported
   security/session state and release all provisional allocations.
2. Allocate a target **MME UE context** with its own MME S11/S10 TEIDs,
   its own eNB UE and SGW UE references, and correctly imported NAS/EPS
   security state. Ensure transfer of NH/NCC and NAS counters, and
   security key lifetime handling, before issuing radio procedures.
3. Select a target S-GW in the visited PLMN and prepare S11/S5/S8
   bearer transfer while **retaining the home P-GW address/TEID and
   original PAA**. An ordinary new-session Create Session flow that
   chooses a new P-GW or assigns a new UE IP is not acceptable.
   Verify how `src/sgwc`, `src/smf`, and PGW/UPF handling perform
   inter-SGW relocation and S8 path changes; add missing support there
   rather than hiding it in the MME.
4. Send **S1AP Handover Request** to the target eNB. Import admitted
   E-RAB forwarding endpoints from Handover Request Acknowledge;
   send S10 Forward Relocation Response with admitted EBI(s), target
   transparent container, target S10 F-TEID and outcome/cause.
5. Only on **S1AP Handover Notify** perform the required S11 Modify
   Bearer/path switch with the target S-GW; only then complete S10
   Forward Relocation Complete Notification/Acknowledge and release
   source control/user-plane resources in the correct order.
6. On target eNB rejection, SGW/PGW failure, cancellation, duplicate
   transaction or timer expiry, unwind the provisional target context
   without disrupting the source session. Ensure repeated notification
   does not trigger a second SGW relocation or duplicate EBI.

#### Concrete implementation touchpoints

| Module | Required change | Status |
| --- | --- | --- |
| `src/mme/mme-s10-handler.[ch]` | Replace rejection-only flow with source/target Forward Relocation, Complete and Cancel handlers | Not implemented |
| `src/mme/mme-s10-build.[ch]` | Encode real MM Context, PDN Connection and transaction-specific IEs | Not created |
| `src/mme/mme-context.[ch]` | Add bounded per-UE S10 relocation context and TEID ownership/lookup | Not implemented |
| `src/mme/s1ap-handler.c` | Dispatch remote target TAI to S10; handle source/target S1AP outcomes | Not implemented |
| `src/mme/s1ap-build.c` | Adapt Handover Request/Command builders for a target MME UE context and imported eNB information | Not implemented |
| `src/mme/mme-s11-[build,handler].c` | Use transferred S-GW/P-GW/EBI context for target-path activation | Not implemented |
| `src/sgwc`, `src/smf`, `src/sgwu`, `src/upf` | Support required inter-SGW relocation and home-routed P-GW continuity | Not validated / likely code work |
| `tests/handover` and `tests/transfer` | Two-MME/eNB integration tests with two PLMNs and a home P-GW | Not created |

The previously implemented S10 codec, configured peer routing, Echo,
negative response and basic transaction matching remain the *foundation*,
not evidence that Forward Relocation succeeds.

#### Tests required before MVP is declared finished

- Positive: A→B handover, B→A reverse handover, same PAA/IP and P-GW
  anchor, same EBI, completed user-plane path switch and cleanup.
- Validation: transferred MM Context matches TS 29.274, security/NAS
  counters and NH/NCC are preserved, replay protection works.
- Negative: unauthorized source/target PLMN, wrong target TAI/eNB,
  malformed MM Context, missing PDN/bearer context, unknown S10 TEID,
  unsupported APN/PDN type, duplicate or out-of-order relocation
  messages, source cancel, target eNB reject, target S-GW failure,
  response timeout, source MME disconnect, late target Handover Notify.
- Regression: original intra-MME S1 and X2 handover, EPC attach,
  5G SA inter-AMF/N2 home-routed handover, and all existing GTPv2 tests.
- Measurements: ping/TCP continuity, interruption time and packet
  loss, retained PAA, PGW and per-bearer TEID traces; source/target
  MME/SGW context counts must return to baseline after cleanup.

**Exit gate:** all positive and mandatory negative cases run against
two real Open5GS MMEs and user-plane components. A successful
compile or GTP codec test is insufficient.

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

### M4: Inter-PLMN N26 and advanced roaming extensions

**Inter-PLMN LTE/S10 handover is part of M1-B, not deferred here.** M4 covers additional N26-specific inter-PLMN interworking, advanced EPC roaming cases (multiple PDNs/dedicated bearers, local breakout, alternative P-GW anchors), dynamic peer discovery and broader operator interoperability. Do not conflate EPC home-routed roaming with the existing 5GC V-SMF/H-SMF architecture.

## Execution blocker — 17 November 2026

`rdc-worker-02`, the designated Open5GS implementation/test worker, was
offline when M1-B execution was resumed (last seen ~49 hours earlier).
Accordingly no new M1-B C implementation, compilation, lab test or MVP
completion is asserted in this checkpoint. The GitHub branch remains at
`b167847ee0acb1236ecd7cda14a06b6845303f40` until build-validated
changes can be produced. Do not treat planning/architecture review as an
implemented handover.

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
