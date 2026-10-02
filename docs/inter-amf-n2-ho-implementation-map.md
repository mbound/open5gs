# Inter-AMF N2 Handover (N14/Namf_Communication) — Implementation Map

Branch: `inter-amf-n2-ho`

Baseline: `5877b43196fca0b185b266f6616979c86c962a4c`

## Scope

Initial implementation target (M1):

- 5G SA only.
- Two Open5GS AMFs.
- Same PLMN first, direct SBI between AMFs.
- Connected-mode N2 handover with AMF relocation.
- One PDU session / one S-NSSAI initially.
- Reuse the existing SMF/PSA where topology permits.
- Add the inter-PLMN SEPP/N32 path only after the direct inter-AMF procedure works.

The normative procedure to map is TS 23.502 §4.9.1.3.2 (N2-based handover with AMF relocation), with Namf_Communication operation semantics from TS 29.518.

## Current Open5GS baseline findings

### Existing same-AMF handover path

`src/amf/ngap-handler.c::ngap_handle_handover_required()` currently parses:

- source RAN/AMF UE NGAP IDs;
- HandoverType and Cause;
- TargetID / target RAN node;
- PDU Session Resource List;
- Source-to-Target Transparent Container.

It resolves the target gNB locally with `amf_gnb_find_by_gnb_id()`. If the target gNB is not attached to the same AMF, the procedure terminates with an error. This is the source-side split point for inter-AMF handover.

For the same-AMF case it:

1. creates/associates a target `ran_ue_t`;
2. stores the handover type/cause/container;
3. sends `Nsmf_PDUSession_UpdateSMContext` with `HANDOVER_REQUIRED`;
4. derives the next-hop security state;
5. later handles `HandoverRequestAcknowledge` and the remaining same-AMF procedure.

This code should be preserved as the local-target fast path.

### Namf_Communication CreateUEContext server seam already exists

Current Open5GS is further along than initially expected.

`src/amf/amf-sm.c` recognizes the TS 29.518 Individual UE Context resource and routes:

`PUT /namf-comm/v1/ue-contexts/{ueContextId}`

to:

`amf_namf_comm_handle_create_ue_context_request()`.

`src/amf/namf-handler.c` already implements an initial CreateUEContext handler. It validates the presence of:

- `ueContext`;
- `targetId`;
- `sourceToTargetData`;
- a non-empty `pduSessionList`;
- `n2NotifyUri`.

The handler then deliberately returns HTTP 501 because target-AMF state reconstruction and asynchronous completion after HandoverRequestAcknowledge are not implemented yet.

This is useful: M1 does not need a new Namf server route. The missing work starts after parsing/validation.

### Existing AMF-to-AMF registration context transfer

`src/amf/context.h` already contains context-transfer states for:

- old/new-AMF UE context transfer;
- old/new-AMF registration status update.

`src/amf/gmm-handler.c` detects a serving-AMF change from the 5G-GUTI during registration and invokes the existing Namf UEContextTransfer flow.

This provides reusable patterns for AMF discovery, SBI transaction ownership, AMF UE context serialization, and old/new-AMF lifecycle handling.

## M1 missing pieces

### Source AMF

1. In `ngap_handle_handover_required()`, distinguish:
   - target gNB local to this AMF → existing same-AMF path;
   - target gNB not local → inter-AMF path.

2. Preserve the full target identity, including target PLMN/TAI, rather than reducing the decision to only a locally-known gNB ID.

3. Select/discover the target AMF.

4. Build and send `Namf_Communication_CreateUEContext`, including:
   - UE/MM context required by TS 29.518;
   - security context;
   - TargetID;
   - Source-to-Target Transparent Container;
   - PDU-session list and N2 SM information;
   - N2 notification URI.

5. Hold source handover state while awaiting target-AMF completion.

6. On successful CreateUEContext response, feed the returned target-to-source information into the existing source-gNB HandoverCommand path.

### Target AMF

1. Extend `amf_namf_comm_handle_create_ue_context_request()` beyond validation.

2. Reconstruct a target `amf_ue_t` and required session context from the transferred UE context.

3. Resolve the target gNB from TargetID.

4. For each transferred PDU session, invoke the required SMF update for handover preparation.

5. Build/send NGAP HandoverRequest to the target gNB.

6. Persist an asynchronous CreateUEContext transaction context so the original SBI response can be completed only after NGAP HandoverRequestAcknowledge.

7. On HandoverRequestAcknowledge:
   - capture Target-to-Source Transparent Container;
   - capture admitted/failed PDU-session information;
   - finish the outstanding CreateUEContext response.

8. Add failure and cleanup paths after the success path is stable.

## Key implementation constraint

The target-AMF CreateUEContext handler must not return success when it merely accepts the request. The successful CreateUEContext result depends on target-RAN handover preparation. The existing HTTP 501 behavior is therefore preferable to a premature 2xx response until the asynchronous state machine is implemented.

## Planned increments

- **M1-A**: implementation map and exact 3GPP field/procedure mapping.
- **M1-B**: source-AMF inter-AMF split + CreateUEContext request builder/client.
- **M1-C**: target-AMF context reconstruction + target NGAP HandoverRequest.
- **M1-D**: correlate HandoverRequestAcknowledge to the pending Namf transaction and complete CreateUEContext.
- **M1-E**: HandoverNotify, SMF update, source release, cancel/failure cleanup.
- **M2**: separate PLMN IDs, still direct AMF-to-AMF SBI for isolation.
- **M3**: route the same procedure through SCP/SEPP/N32.
- **M4+**: standards-complete target AMF selection, inter-PLMN S-NSSAI mapping, and additional relocation variants.

## Immediate next code-reading targets

- `src/amf/ngap-handler.c`: exact same-AMF handover state and completion hooks.
- `src/amf/ngap-build.c` / `ngap-path.c`: reusable HandoverRequest/HandoverCommand builders.
- `src/amf/namf-build.c`: existing AMF-to-AMF request builders and serialization patterns.
- `src/amf/sbi-path.c`: AMF discovery and SBI transaction ownership.
- `src/amf/context.[ch]`: minimal state additions for a pending inter-AMF handover.
- free5GC PR #165: reference for asynchronous CreateUEContext ↔ HandoverRequestAcknowledge correlation; procedure logic only, not a mechanical port.

