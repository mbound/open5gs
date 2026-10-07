# Inter-AMF N2 handover over N14

## Scope

This branch implements connected-mode 5G SA N2 handover with AMF relocation
using `Namf_Communication`. The controlled M1/M2 validation topology is
same-PLMN and direct AMF-to-AMF SBI with one PDU session and a shared SMF/UPF.

That topology is only a procedure-isolation harness. The target TN-NTN
inter-PLMN architecture is **home-routed (HR) roaming**:

- H-SMF/H-UPF (PSA) remain the PDU Session anchor in the HPLMN;
- the visited network uses V-SMF/V-UPF;
- V-UPF reaches the home anchor over N9;
- inter-PLMN SBA signalling crosses vSEPP/hSEPP over N32;
- N14/Namf_Communication provides AMF-relocation continuity.

Baseline: `5877b43196fca0b185b266f6616979c86c962a4c`.

## Current M1/M2 implementation

The branch now contains the controlled one-session inter-AMF handover path:

1. Source gNB sends `HandoverRequired` with a Global gNB target ID and
   selected target TAI.
2. Source AMF distinguishes local-target from remote-target handover.
3. NRF AMF discovery filters registered AMFs by advertised TAI/TAI ranges.
4. Source AMF sends `Namf_Communication_CreateUEContext`.
5. Target AMF reconstructs UE, MM/security and PDU-session context.
6. KAMF, NH/NCC, NAS algorithm selection/counters are transferred and the
   target derives KNASint/KNASenc locally.
7. Target AMF performs SMF handover preparation and sends NGAP
   `HandoverRequest`.
8. `HandoverRequestAcknowledge` completes the pending CreateUEContext with
   HTTP 201; source AMF then sends `HandoverCommand`.
9. Source `UplinkRANStatusTransfer` is relayed to the target AMF using
   `Namf_Communication_N1N2MessageTransfer` with
   `n2InformationClass=RAN` and
   `ngapIeType=RAN_STATUS_TRANS_CONTAINER`; the target AMF emits
   `DownlinkRANStatusTransfer` to the target gNB.
10. Target `HandoverNotify` updates the SMF and sends
    `N2InfoNotify(HANDOVER_COMPLETED)` to the source AMF.
11. Source AMF releases the old source NG/AMF UE context.
12. Source `HandoverCancel` uses `Namf_Communication_ReleaseUEContext`;
    target failure/cancel paths roll back target SMF/RAN state before
    acknowledging the source.

The existing same-AMF N2 handover remains the local-target fast path.

## NTN location handling

Release-19 `UserLocationInformationNR.iE-Extensions` is parsed for
`NRNTNTAIInformation`, including:

- serving PLMN;
- TAC list in NR NTN;
- optional UE-location-derived TAC.

The information is retained in RAN/AMF UE state, copied during target UE
creation, and included in SMF NR location updates including HandoverNotify.

There is no separate NGAP "Mapped Cell ID" field in this location structure.
For NTN, the `NR-CGI.nRCellIdentity` value carries the mapped-cell semantics.

The temporary macro/body-include implementation used while editing through the
GitHub connector has been removed. NTN handling is folded directly into
`ngap-handler.c` and `nsmf-build.c`.

## Controlled validation topology

`configs/inter-amf-n2.yaml.in` defines:

- AMF-1: PLMN 999/70, TAC 1;
- AMF-2: PLMN 999/70, TAC 2;
- shared SMF and UPF;
- NRF/SCP discovery infrastructure.

gNB IDs are unique across the two AMFs.

The test launcher `tests/transfer/abts-inter-amf-n2-main.c` uses the standard
Open5GS multi-NF test launcher. Repeated AMF sections are counted into
`amf_count`, so both AMF instances are started using the existing `-k`
instance selection mechanism.

`tests/transfer/inter-amf-n2-handover-test.c` exercises:

- gNB-1 attached to AMF-1/TAC-1;
- gNB-2 attached to AMF-2/TAC-2;
- initial registration and one PDU session on AMF-1;
- target-AMF selection by TAC-2;
- CreateUEContext and target handover preparation;
- HandoverRequest/HandoverRequestAcknowledge;
- HandoverCommand;
- inter-AMF RAN Status Transfer relay;
- HandoverNotify and HANDOVER_COMPLETED source cleanup;
- retained PDU-session user plane through target gNB N3;
- clean target context release.

The M1 test advertises direct forwarding; indirect forwarding remains outside
the controlled baseline.

Additional unit coverage exists for:

- CreateUEContext SBI parsing;
- AMF discovery by exact TAI and TAI range, including multiple amfInfo blocks;
- RAN-class N1N2MessageTransfer parsing.

## Validation status

**The branch has not yet been compiled or executed.**

This is an environment limitation, not a passing result:

- GitHub Actions has produced zero workflow runs for the fork/branch even
  though the inherited workflow is configured for push/PR;
- the connected Desktop Commander build host is offline;
- the local sandbox cannot clone/download the GitHub repository.

Therefore M2 is **implemented in source and has a reproducible test harness,
but is not yet validated**. The first available execution environment should
run a normal Meson build followed by the focused `inter-amf-n2` test and the
existing unit/5GC suites.

Do not treat the branch as merge-ready until those runs pass.

## Standards baseline

Primary references:

- 3GPP TS 23.502 §4.9.1.3.2 — N2 handover preparation with AMF relocation;
- 3GPP TS 23.502 §4.9.1.3.3 — execution phase, including inter-AMF RAN Status
  Transfer and handover completion;
- 3GPP TS 23.502 §4.23 — inter-PLMN/HR handover impacts;
- 3GPP TS 29.518 — Namf_Communication Individual UE Context,
  CreateUEContext, N1N2MessageTransfer and ReleaseUEContext;
- 3GPP TS 29.502 — Nsmf_PDUSession handover updates;
- 3GPP TS 38.413 — NGAP handover procedures;
- 3GPP TS 33.501 — transferred 5G security context and NH/NCC.

## Delivery milestones

- **M1 — source/target AMF preparation path:** implemented in source.
- **M2 — complete same-PLMN inter-AMF lifecycle, NTN location handling and
  reproducible two-AMF integration harness:** implemented in source;
  compile/runtime validation pending.
- **M3 — actual inter-PLMN HR topology:** not implemented. Requires H-SMF/H-UPF
  home anchoring, V-SMF/V-UPF, N9, vSEPP↔hSEPP N32 and inter-PLMN
  PLMN/S-NSSAI/session-management handling.
- **M4 — expanded cases:** multi-session partial success, additional forwarding
  and I-SMF/I-UPF variants, policy relocation and broader roaming scenarios.

## Non-goals of the M1/M2 validation harness

- DAPS handover;
- indirect data forwarding;
- V-SMF/H-SMF relocation;
- I-UPF/PSA relocation;
- PCF relocation;
- EPS/N26 mobility;
- multi-session partial-success coverage.

Those belong to M3/M4 once the controlled inter-AMF path is validated.
