# EPC S10, EN-DC (NSA Option 3), and N26 implementation plan

Status: implementation plan and protocol scaffolding; **not yet runtime validated**.
Baseline: [upstream integration RC1](https://github.com/mbound/open5gs/tree/integration/upstream-2026-11-05-rc1), commit `b5c8abdd05a9a2f487b8c61e72cbf67425375fd6`.
Development branch: `feature/s10-gtpv2-control-plane`.

## Scope and architecture

Implement LTE/EPC inter-MME S1-based handover over **S10** first. Keep this separate from the already implemented 5GC **N14** inter-AMF N2 handover. Add the EPS side of 5G NSA Option 3/EN-DC as an EPC and E-UTRAN integration concern, not as a new core-to-RAN N2 interface. Then implement **N26** for AMF–MME 5GS/EPS interworking, reusing the GTPv2-C transport and relevant message codecs but not assuming S10 and N26 have identical mandatory IEs or security context semantics.

Primary references: 3GPP TS 23.401 (EPS mobility); TS 29.274 (GTPv2-C message types/IEs; S10/N26); TS 36.413 (S1AP); TS 36.423 (X2AP, EN-DC applicability); TS 23.502 §4.11 (5GS/EPS interworking); TS 33.401 and TS 33.501 (security and context/key mappings).

### Existing Open5GS entry points

- `lib/gtp/v2/message.h`: already has GTPv2-C header, IE vocabulary, F-TEID and MM Context foundations; missing S10/N26 mobility message type declarations and complete message codecs.
- `src/mme/s1ap-handler.c`: current S1 handover path resolves a target eNB locally. Extend to select another MME from target TAI and route a Forward Relocation Request over S10.
- `src/mme/mme-sm.c`, `src/mme/mme-gtp-path.c`: EPC state machine and existing S11/Gn dispatch; introduce a dedicated S10 control-plane path, peer configuration, and transaction correlation.
- `src/mme/mme-context.h`: persisted NAS, KASME/KeNB and EPS bearer state must be exported/imported with ownership, lifetime and replay-counter semantics.
- `src/sgwc`, `src/sgwu`, `src/smf`, `src/upf`: reuse existing S11/S5/S8/PFCP forwarding and bearer setup rather than introducing independent packet-anchor state.

## Incremental milestones

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
- **Current blocker:** `rdc-worker-02` ceased responding after launching MongoDB-backed 5GC mobility tests. MongoDB 9.0.4 was started, but no subsequent test result could be retrieved. A missing result is neither a pass nor a fail. Do not promote RC1 without verified runtime results.
- The spec corpus and expertise/reference materials are in `mbound/3gpp-oran-standards-tools`; use its `3gpp-specs` MCP lookup to check exact IE optionality, message tables and release-specific variants before finalizing codecs.

## Standards and source links

- ETSI/3GPP TS 29.274: https://www.etsi.org/deliver/etsi_ts/129200_129299/129274/
- ETSI/3GPP TS 23.401: https://www.etsi.org/deliver/etsi_ts/123400_123499/123401/
- ETSI/3GPP TS 23.502: https://www.etsi.org/deliver/etsi_ts/123500_123599/123502/
- 3GPP reference tools: https://github.com/mbound/3gpp-oran-standards-tools
