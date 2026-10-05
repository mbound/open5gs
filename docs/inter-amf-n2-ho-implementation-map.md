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
- Both AMFs may share one SMF/UPF only for this procedure-isolation harness.
- gNB IDs must be unique across the two AMFs.

The production/lab target for **inter-PLMN TN-NTN handover is home-routed
roaming**, not the M1 shared-SMF/UPF topology. The home H-SMF/H-UPF remains the
PDU Session anchor, the visited side uses V-SMF/V-UPF and N9 toward the home
anchor, and inter-PLMN SBA signalling traverses SEPPs over N32.

The normative procedure set is therefore:
- TS 23.502 §4.9.1.3 for N2 handover/AMF relocation;
- TS 23.502 §4.23 for inter-PLMN/HR handover impacts and intermediate SMF/UPF
  insertion/change/removal;
- TS 29.518 for Namf_Communication;
- TS 23.501 home-routed roaming architecture for V-SMF/V-UPF, H-SMF/H-UPF,
  N9 and N32/SEPP.

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

## Current blockers found by code survey

The success path is substantially implemented, but five items block the
intended TN-NTN tests:

1. **TAI-aware target-AMF selection** — the source supplies target TAI/PLMN in
   discovery, but the NRF selection path does not reliably distinguish AMFs
   by served TAI. Add NRF TAI matching or a deterministic lab override.

2. **Handover-complete notification to the source AMF** — the branch already
   contains `amf_namf_callback_build_n2_info_notify()` with
   `HANDOVER_COMPLETED`, but nothing invokes it from the target
   `HandoverNotify` completion path. Without it, the old source-side context
   is not released.

3. **Cancel/failure N14 rollback** — Handover Cancel and target preparation
   failure still need inter-AMF state rollback and peer notification instead
   of terminating locally with Error Indication.

4. **NTN location IEs** — ordinary NR-CGI/TAI are handled, but NR NTN TAI
   Information and Mapped Cell ID are not yet retained/propagated for TN-NTN
   mobility testing.

5. **Build/integration validation** — no CI run and no reproducible two-AMF
   configuration/test exist yet.

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

- **M1**: same-PLMN/direct AMF-to-AMF success path, one PDU session.
- **M2-A**: make target-AMF selection deterministic and TAI-aware.
- **M2-B**: invoke Namf callback `HANDOVER_COMPLETED`, release source
  AMF/RAN state, and complete execution-phase lifecycle.
- **M2-C**: implement inter-AMF Handover Cancel / HandoverFailure rollback.
- **M2-D**: preserve NTN-specific location IEs required by the TN-NTN test.
- **M2-E**: add two-AMF/two-gNB configuration and integration test; run CI.
- **M3**: inter-PLMN **home-routed** topology with H-SMF/H-UPF anchor,
  V-SMF/V-UPF, N9, and vSEPP↔hSEPP over N32. Keep N14 as the AMF-relocation
  control-plane procedure within this architecture.
- **M4+**: multi-session partial success, forwarding variants, I-SMF/I-UPF
  relocation variants, full inter-PLMN S-NSSAI mapping, policy relocation and
  additional roaming cases.

## Immediate next code-reading targets

- `src/amf/ngap-handler.c`: exact same-AMF handover state and completion hooks.
- `src/amf/ngap-build.c` / `ngap-path.c`: reusable HandoverRequest/HandoverCommand builders.
- `src/amf/namf-build.c`: existing AMF-to-AMF request builders and serialization patterns.
- `src/amf/sbi-path.c`: AMF discovery and SBI transaction ownership.
- `src/amf/context.[ch]`: minimal state additions for a pending inter-AMF handover.
- free5GC PR #165: reference for asynchronous CreateUEContext ↔ HandoverRequestAcknowledge correlation; procedure logic only, not a mechanical port.

