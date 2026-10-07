# Inter-PLMN home-routed N2 handover (M3)

## Scope

This branch extends the M2 `inter-amf-n2-ho` baseline with the first
inter-PLMN home-routed (HR) connected-mode handover case:

- source UE is registered in HPLMN 999-70;
- the PDU Session is initially anchored directly at the H-SMF/H-UPF;
- target NG-RAN and target AMF are in VPLMN 001-01;
- N14 / Namf_Communication transfers the UE context to the target AMF;
- the target AMF inserts a V-SMF/V-UPF for the retained home-routed session;
- V-SMF <-> H-SMF signalling uses Nsmf_PDUSession;
- the target V-UPF reaches the retained H-UPF/PSA over N9;
- cross-PLMN SBI is routed through SCP/SEPP and N32.

The H-SMF/H-UPF PSA is not relocated.

## Implemented procedure

### Preparation

1. Source AMF performs the M2 inter-AMF N2 handover split and transfers the
   UE/session context to the target AMF.
2. The transferred `PduSessionContext` carries the existing SM context
   reference plus H-SMF identity/URI information.
3. Target AMF selects the local V-SMF and sends migration-aware
   `CreateSMContext` with:
   - existing `smContextRef`;
   - source/H-SMF identity and PLMN;
   - `hoState=PREPARING`;
   - target ID;
   - HandoverRequired N2 SM information.
4. Target V-SMF retrieves the complete existing SM context from the retained
   H-SMF using `RetrieveSMContext`.
5. The retrieved context carries the retained UE address, QoS state and PSA
   tunnel endpoint.
6. V-SMF creates the target V-UPF leg. Its uplink N9 FAR points to the
   retained H-UPF.
7. V-SMF sends handover preparation to H-SMF with the target V-CN tunnel.
8. H-SMF stages, but does not yet activate, the target V-SMF/V-UPF endpoint.
9. Target V-SMF returns the handover preparation response to the target AMF,
   which continues the NGAP HandoverRequest/HandoverRequestAcknowledge flow.

### Execution

1. Target NG-RAN sends HandoverNotify.
2. Target AMF reports `hoState=COMPLETED` to the target V-SMF.
3. V-SMF sends the mobility commit to H-SMF.
4. H-SMF changes the retained H-UPF path from source N3 to roaming N9 toward
   the target V-UPF.
5. Only after PFCP/N4 success does H-SMF promote the target V-SMF URI/path.
6. H-SMF response allows V-SMF to complete the AMF UpdateSMContext request.
7. M2 `HANDOVER_COMPLETED` processing releases the old source AMF/RAN
   context.

### Cancel

For HandoverCancel before execution:

1. target V-SMF sends a mobility cancel to H-SMF without a target V-CN tunnel;
2. H-SMF discards only the staged target path and leaves the current
   H-SMF/H-UPF anchor/path intact;
3. target V-SMF acknowledges the AMF and deletes the temporary V-UPF session;
4. M2 Namf `ReleaseUEContext` cleanup handles the target AMF/RAN context.

## Test topology

Generated config:

`build/configs/inter-plmn-hr-n2.yaml`

Source template:

`configs/inter-plmn-hr-n2.yaml.in`

Required FQDN mappings:

`configs/inter-plmn-hr-n2-hosts.txt`

The topology starts two instances where required:

- NRF x2
- SCP x2
- SEPP x2
- AMF x2
- SMF x2
- UPF x2
- HPLMN AUSF / UDM / UDR / PCF / NSSF
- VPLMN NSSF

The standalone integration executable is:

`build/tests/transfer/inter-plmn-hr-n2`

It exercises registration and initial PDU Session establishment in 999-70,
then handover to 001-01, RAN Status Transfer, HandoverNotify, source context
release, and a post-handover GTP-U ping through the target V-UPF/N9/H-UPF path.

## Validation status

Source implementation and the integration harness are present, but this branch
has not yet been compiled or executed in the current environment. Runtime
validation is intentionally deferred until a deployment/build host is
available.

The first deployment validation should therefore check:

1. all generated config parses and all indexed NF instances start;
2. NRF discovery and all cross-PLMN requests traverse the expected SCP/SEPP;
3. N32-c/N32-f negotiation succeeds;
4. target V-SMF RetrieveSMContext reaches the retained H-SMF;
5. target V-UPF N9 establishment succeeds;
6. HandoverNotify causes H-UPF N3->N9 path commit only after target
   preparation;
7. post-handover traffic follows gNB -> V-UPF -> H-UPF/PSA;
8. cancel before execution leaves the original H-UPF path active and removes
   the temporary target V-UPF session.

## Current limitations

- one PDU Session;
- first M3 case is HPLMN -> VPLMN;
- no VPLMN -> VPLMN V-SMF replacement yet;
- no VPLMN -> HPLMN V-SMF removal yet;
- no multi-session partial-success handling;
- no runtime/build result yet.
