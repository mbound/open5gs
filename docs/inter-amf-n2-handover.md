# Inter-AMF N2 handover over N14

## Scope

This branch implements connected-mode 5G SA N2 handover with AMF relocation,
with inter-PLMN operation as the eventual target.

The first milestone (M1) deliberately keeps the source and target AMFs in the
same PLMN and connects them directly over the service-based interface. This is
only a procedure-isolation harness for AMF relocation. It is **not** the target
TN-NTN inter-PLMN architecture.

The target inter-PLMN architecture is **home-routed (HR) roaming**. The PDU
Session remains anchored by the H-SMF/H-UPF (PSA) in the HPLMN. When the UE is
served by the other PLMN, the visited side uses a V-SMF/V-UPF and reaches the
home anchor over N9. Inter-PLMN SBA signalling crosses the PLMN boundary via
vSEPP/hSEPP over N32. N14/Namf_Communication supplies the AMF-relocation
control-plane continuity; it does not replace the HR session anchor or the N9
user-plane path.

Baseline: `mbound/open5gs` commit
`5877b43196fca0b185b266f6616979c86c962a4c`.

## M1 implementation status

The branch now contains the first end-to-end **single-PDU-session** implementation of the direct, same-PLMN inter-AMF preparation and execution path:

- source AMF detects a non-local target gNB and discovers a target AMF by target TAI/PLMN;
- source AMF sends `Namf_Communication_CreateUEContext` with UE/MM/session context and multipart N2 information;
- KAMF, NH/NCC, NAS algorithm selection and NAS counters are transferred; the target AMF restores KNASint/KNASenc locally;
- target AMF reconstructs the UE and PDU-session context, resolves the target gNB and invokes the existing SMF handover-preparation path;
- target AMF sends NGAP `HandoverRequest` and accepts `HandoverRequestAcknowledge` without requiring a local source RAN UE;
- target AMF performs the SMF `HANDOVER_REQ_ACK` update and completes the pending CreateUEContext request with HTTP 201 only after target-RAN preparation succeeds;
- source AMF consumes `UeContextCreatedData` and drives its existing `HandoverCommand` path;
- target-AMF `HandoverNotify` processing updates the SMF with `hoState=COMPLETED` without requiring a local source RAN UE;
- the existing same-AMF N2 handover path remains the local-target fast path.

M1 is deliberately constrained to one PDU session and direct AMF-to-AMF SBI. Failure/cancel coverage is not yet complete, and inter-PLMN SEPP/N32 routing remains M2. The branch includes SBI parsing coverage for CreateUEContext; repository CI has not yet executed on this fork, so build/integration validation is still required before treating the branch as merge-ready.

## Blocking gaps for TN-NTN inter-PLMN testing

The current branch is not yet sufficient for the TN-NTN two-PLMN test. The
following items are blockers rather than optional cleanup:

1. **Target AMF selection by target TAI.** The source side currently builds NRF
   discovery options from the selected target TAI/PLMN, but the current NRF
   matching path does not reliably distinguish two AMFs in one PLMN by TAI.
   M1 therefore needs either NRF TAI matching support or an explicit/static
   target-AMF override for the controlled lab.

2. **Inter-AMF completion notification.** The branch contains
   `amf_namf_callback_build_n2_info_notify()` with
   `HANDOVER_COMPLETED`, but the target-AMF HandoverNotify path does not
   invoke it. Consequently the source AMF is not informed that execution
   completed and cannot perform the old source-side UE/RAN cleanup required
   by the inter-AMF procedure.

3. **Cancel/failure rollback.** Inter-AMF Handover Cancel and target
   HandoverFailure paths are not yet mapped to the corresponding
   Namf_Communication cleanup/notification behaviour. Target AMF UE/session
   state and source handover state therefore need explicit rollback.

4. **NTN location information.** The current NGAP location handling consumes
   the ordinary NR-CGI/TAI path but does not yet preserve the NTN-specific
   location extensions needed by the TN-NTN tests, including NR NTN TAI
   Information and Mapped Cell ID where present.

5. **Validation.** The branch has not been built or executed in CI and does
   not yet contain a reproducible two-AMF/two-gNB topology test. A successful
   Meson build plus a two-AMF integration test is required before M1 can be
   considered complete.

For the controlled M1 harness both AMFs may share one SMF/UPF and gNB IDs must
be unique across the two AMFs. That topology is deliberately non-roaming and
exists only to isolate the N14/NGAP state machine before HR roaming is enabled.

## Standards baseline

The implementation is mapped against these procedures and APIs:

- 3GPP TS 23.502, clause 4.9.1.3.2: inter-NG-RAN-node N2 handover preparation
  with AMF change.
- 3GPP TS 29.518, clause 5.2.2.2.3 and the Individual UE Context resource:
  `Namf_Communication_CreateUEContext`, using
  `PUT /namf-comm/v1/ue-contexts/{ueContextId}`.
- 3GPP TS 29.502, N2 handover preparation via
  `Nsmf_PDUSession_UpdateSMContext`.
- 3GPP TS 38.413, N2 Handover Preparation procedures
  (`HandoverRequired`, `HandoverRequest`,
  `HandoverRequestAcknowledge`, `HandoverCommand`).
- 3GPP TS 33.501 for transfer/use of the 5G security context and NH/NCC during
  N2 handover.

Open5GS contains generated TS 29.518 OpenAPI models through Release 19.  The
initial implementation should use the existing generated types instead of
introducing a private wire format.

## Target preparation flow

The M1 preparation phase is:

```text
S-gNB              S-AMF                 T-AMF              T-gNB
  |                  |                     |                  |
  | HandoverRequired |                     |                  |
  |----------------->|                     |                  |
  |                  | CreateUEContext PUT |                  |
  |                  |-------------------->|                  |
  |                  |                     | UpdateSMContext  |
  |                  |                     |-----> SMF        |
  |                  |                     |<----- SMF        |
  |                  |                     | HandoverRequest  |
  |                  |                     |----------------->|
  |                  |                     | HandoverReqAck   |
  |                  |                     |<-----------------|
  |                  |                     | UpdateSMContext  |
  |                  |                     |-----> SMF        |
  |                  |                     |<----- SMF        |
  |                  | CreateUEContext 201 |                  |
  |                  |<--------------------|                  |
  | HandoverCommand  |                     |                  |
  |<-----------------|                     |                  |
```

Execution/completion and cancellation are subsequent increments.  The
preparation implementation must not introduce a synchronous sleep/poll loop
inside an SBI request handler; the existing Open5GS event/FSM model should be
used to retain the inbound SBI stream until target-RAN preparation completes.

## Existing Open5GS handover path

### Source Handover Required

`src/amf/ngap-handler.c::ngap_handle_handover_required()` already:

1. validates AMF/RAN UE identifiers, handover type, cause and TargetID;
2. accepts `TargetID.targetRANNodeID` with a Global gNB ID;
3. extracts the target gNB ID;
4. requires `amf_gnb_find_by_gnb_id(target_gnb_id)` to return a locally
   connected target gNB;
5. creates and associates a target `ran_ue_t`;
6. stores the source-to-target transparent container;
7. sends each active PDU session to the SMF using
   `AMF_UPDATE_SM_CONTEXT_HANDOVER_REQUIRED`, with
   `hoState=PREPARING` and the NGAP TargetID;
8. advances the NH/NCC security chain after validating the complete PDU-session
   list.

The current failure point for AMF relocation is therefore explicit: a target
gNB that is not attached to the source AMF is treated as an error rather than
as the trigger for target-AMF selection.

### Target allocation and acknowledgement

For same-AMF handover, Open5GS already has:

- `ngap_send_handover_request()`;
- `ngap_handle_handover_request_ack()`;
- SMF handover-prepared updates;
- `ngap_send_handover_command()`;
- handover notify, cancel and context-release handling.

These procedures should be reused.  The inter-AMF path should change ownership
of the target RAN context, not duplicate the NGAP procedure.

## Existing N14 / Namf_Communication support

Open5GS already implements AMF-to-AMF context transfer used during mobility
registration:

- `amf_namf_comm_build_ue_context_transfer()`;
- `amf_namf_comm_handle_ue_context_transfer_request()`;
- `amf_namf_comm_handle_ue_context_transfer_response()`;
- `amf_namf_comm_build_registration_status_update()`;
- corresponding old/new-AMF state in `amf_ue_t`;
- NRF/SBI discovery of `namf-comm` AMF services.

This is useful infrastructure, but it is not the connected-mode handover
procedure.

## CreateUEContext gap

The generated SBI/OpenAPI layer already contains:

- `OpenAPI_ue_context_create_data_t`;
- `OpenAPI_ue_context_created_data_t`;
- `OpenAPI_create_ue_context_request_t`;
- `OpenAPI_create_ue_context_201_response_t`;
- the TS 29.518 `/ue-contexts/{ueContextId}` PUT schema.

However, current generic SBI handling does not expose corresponding fields in
`ogs_sbi_message_t`, and the `namf-comm` parser currently dispatches only
subresources such as:

- `/ue-contexts/{id}/n1-n2-messages`;
- `/ue-contexts/{id}/transfer`;
- `/ue-contexts/{id}/transfer-update`.

The bare `/ue-contexts/{id}` PUT needed by
`Namf_Communication_CreateUEContext` therefore needs explicit build, parse
and AMF-server dispatch support.

## Implementation plan

### M1-A: generic SBI model plumbing

Add to `ogs_sbi_message_t`:

- `UeContextCreateData`;
- `UeContextCreatedData`.

Teach `lib/sbi/message.c` to:

- serialize the two models;
- parse `PUT /ue-contexts/{id}` requests as `UeContextCreateData`;
- parse a 201 response as `UeContextCreatedData`;
- free both model instances safely.

Multipart NGAP binary parts remain represented by the existing
`ogs_sbi_message_t.part[]` mechanism.

### M1-B: target-AMF CreateUEContext server seam

Add `amf_namf_comm_handle_create_ue_context_request()` and route a bare
`PUT /ue-contexts/{id}` to it.

The first version must perform strict input validation and return a standards-
shaped error for unsupported/incomplete requests.  It must not silently create
a partial UE context.

### M1-C: source-AMF builder and target-AMF discovery

When `HandoverRequired` names a non-local target:

1. retain the selected TargetID/TAI and source-to-target transparent container;
2. select/discover a target AMF serving the target TAI;
3. build `UeContextCreateData` containing the UE/MM/security and session
   context required by TS 29.518;
4. send `Namf_Communication_CreateUEContext`.

For M1, configured/direct AMF selection may be used to decouple relocation
logic from inter-PLMN NRF/SEPP routing, but the request must use the normal SBI
transaction framework.

### M1-D: reconstruct target UE/session state

The T-AMF must reconstruct, at minimum:

- SUPI and relevant UE identity;
- NAS/MM security context needed for handover;
- AMBR and supported/requested slice context required by the existing target
  HandoverRequest builder;
- PDU-session contexts including PDU session ID, S-NSSAI and SM context
  reference/SMF routing information;
- target TAI/gNB information;
- source-to-target transparent NGAP container and NGAP cause.

The SMF remains authoritative for session/user-plane state.

### M1-E: target SMF preparation

For every accepted PDU session, T-AMF invokes
`Nsmf_PDUSession_UpdateSMContext` for N2 handover preparation and constructs
the target `PDUSessionResourceSetupListHOReq`.

The existing Open5GS same-AMF SMF/NGAP builders should be reused where their
inputs are equivalent.

### M1-F: asynchronous CreateUEContext completion

The inbound CreateUEContext SBI transaction remains pending while T-AMF sends
`HandoverRequest` to T-gNB.

`HandoverRequestAcknowledge` must correlate back to that pending
CreateUEContext operation, run the existing handover-prepared SMF updates, and
then send the 201 response containing:

- `UeContextCreatedData`;
- target-to-source transparent NGAP information;
- per-session success/failure information.

This correlation should use Open5GS pool IDs/event state, not a blocking
timeout.

### M1-G: source HandoverCommand

The S-AMF consumes the CreateUEContext response and maps the returned
target-to-source data and per-session transfer information into its existing
`HandoverCommand` path.

### M1-H: failure/cancel/cleanup

Add explicit rollback for:

- T-AMF discovery failure;
- CreateUEContext 4xx/5xx;
- SMF handover-preparation failure;
- target HandoverFailure;
- source HandoverCancel;
- transaction timeout;
- target context release.

No target AMF-UE or RAN-UE object may survive a failed preparation unless a
subsequent standards procedure explicitly requires it.

## Inter-PLMN target architecture: home-routed roaming

The TN-NTN inter-PLMN baseline is **not** "the M1 topology with different PLMN
IDs". It is the 5GS home-routed roaming architecture.

```text
                 control plane / inter-PLMN SBI
        HPLMN                                      VPLMN
   +-------------+        N32 / SEPP          +-------------+
   | H-SEPP      |============================| V-SEPP      |
   +-------------+                            +-------------+
          |                                         |
       H-AMF / S-AMF  <--- N14/Namf ------------> V-AMF / T-AMF
          |                                         |
        H-SMF <--------------- N16 -------------- V-SMF
          |                                         |
        H-UPF (PSA) <----------- N9 ------------ V-UPF
          |                                         |
          DN                                  target TN/NTN RAN
```

For the baseline test:

- the PDU Session anchor remains the H-SMF/H-UPF (PSA);
- a visited V-SMF/V-UPF is inserted/used as required by the HR roaming
  procedure;
- the visited user plane reaches the home PSA over N9;
- inter-PLMN service-based signalling traverses the two SEPPs over N32;
- N14/Namf_Communication carries the AMF relocation/context-transfer part of
  the N2 handover, while SMF/V-SMF and N9 updates preserve PDU-session/user-plane
  continuity.

TS 23.502 explicitly redirects inter-PLMN N2 handover in an HR roaming
scenario to the clause 4.23 procedures. Those procedures preserve the anchor
SMF/PSA while allowing the intermediate/visited SMF and UPF path to be
inserted, changed, or removed.

### Delivery milestones

- **M1** — same PLMN, direct S-AMF↔T-AMF, one PDU session, shared SMF/UPF;
  complete and test the N14/NGAP state machine.
- **M2** — close M1 blockers: deterministic/TAI-aware T-AMF selection,
  HANDOVER_COMPLETED callback/source cleanup, cancel/failure rollback, NTN
  location IEs, and a two-AMF/two-gNB integration test.
- **M3** — actual inter-PLMN HR testbed: H-SMF/H-UPF anchor, V-SMF/V-UPF,
  N9, vSEPP↔hSEPP N32, inter-PLMN slice/PLMN mapping and target selection.
- **M4** — expand beyond the controlled one-session baseline: multi-session
  partial success, forwarding variants, I-SMF/I-UPF relocation variants,
  policy/PCF relocation where required, and broader roaming cases.

## Initial non-goals

Until M1 is stable:

- DAPS handover;
- indirect data forwarding;
- I-UPF/PSA relocation;
- V-SMF/H-SMF relocation;
- PCF relocation;
- multiple access types;
- EPS/N26 mobility;
- emergency/unauthenticated SUPI edge cases;
- full NSSF reselection logic;
- multi-session partial-success optimisation beyond standards-required
  correctness.

## Reference implementation

free5GC PR #165 is useful as a procedure reference because it adds an
experimental inter-AMF N2 handover path around CreateUEContext.  It should not
be copied mechanically: its implementation uses Go channels and a blocking
timeout in the target-AMF CreateUEContext handler, while Open5GS already has an
event-driven SBI/FSM architecture better suited to retaining asynchronous
handover state.

The useful parts to compare are the transferred context fields, target SMF
preparation, target-RAN HandoverRequest construction, acknowledgement mapping
and error paths.
