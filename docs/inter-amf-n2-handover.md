# Inter-AMF N2 handover over N14

## Scope

This branch implements connected-mode 5G SA N2 handover with AMF relocation,
with inter-PLMN operation as the eventual target.

The first milestone (M1) deliberately keeps the source and target AMFs in the
same PLMN and connects them directly over the service-based interface.  This
isolates the AMF-relocation procedure before adding SEPP/N32 routing.

Baseline: `mbound/open5gs` commit
`5877b43196fca0b185b266f6616979c86c962a4c`.

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

## M2: inter-PLMN routing

After direct two-AMF M1 works, add the PLMN boundary:

```text
S-AMF -> S-SCP/SEPP == N32 == T-SEPP/SCP -> T-AMF
```

Open5GS already has SEPP/N32 infrastructure and the SBI discovery layer already
supports target/requester PLMN lists.  M2 should therefore preserve the same
Namf_Communication transaction and change routing/discovery rather than the
handover state machine.

M2 validation includes:

- target PLMN from NGAP TargetID/TAI;
- target AMF selection/discovery in the target PLMN;
- requester/target PLMN discovery attributes;
- S-NSSAI mapping and target support checks;
- SEPP/N32 traversal;
- roaming session architecture constraints.

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
