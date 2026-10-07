# Inter-AMF N2 Handover — implementation map

Branch: `inter-amf-n2-ho`

Baseline: `5877b43196fca0b185b266f6616979c86c962a4c`

## Controlled M1/M2 scope

- 5G SA only.
- Two Open5GS AMFs in one PLMN.
- Direct AMF-to-AMF SBI.
- One PDU session / one S-NSSAI.
- Shared SMF/UPF for procedure isolation.
- Distinct AMF-served TAIs and unique gNB IDs.
- Direct forwarding path.

This is not the final inter-PLMN architecture. M3 is HR roaming with
H-SMF/H-UPF home anchoring, V-SMF/V-UPF, N9 and SEPP/N32.

## Implemented procedure map

| Procedure element | Open5GS implementation |
|---|---|
| Source HandoverRequired | `src/amf/ngap-handler.c` detects a non-local target and retains TargetID/TAI/container |
| Target-AMF discovery | NRF discovery option includes TAI/PLMN; `lib/sbi/context.c` matches AMF `taiList` / `taiRangeList` |
| CreateUEContext client | `amf_namf_comm_build_create_ue_context()` + handover SBI transaction |
| CreateUEContext server | `amf_namf_comm_handle_create_ue_context_request()` reconstructs target state |
| Security transfer | KAMF + NH/NCC + NAS algorithms/counters transferred; target derives KNAS keys |
| Target SMF preparation | Existing Nsmf UpdateSMContext path reused for inter-AMF handover |
| Target HandoverRequest | Existing NGAP builder/path reused from target AMF |
| HandoverRequestAck correlation | Pending CreateUEContext stream retained and completed after target-RAN preparation |
| CreateUEContext 201 | Returns target-to-source container, session transfers and target UE-context Location |
| Source HandoverCommand | Existing NGAP HandoverCommand path consumes returned transfer data |
| RAN Status Transfer | S-AMF relays via Namf N1N2MessageTransfer class RAN; T-AMF emits DownlinkRANStatusTransfer |
| HandoverNotify | Target AMF updates SMF with completed state and target NR location |
| Source completion | T-AMF sends N2InfoNotify(HANDOVER_COMPLETED); S-AMF releases old NG/AMF context |
| HandoverCancel | S-AMF sends ReleaseUEContext; T-AMF rolls back SMF/RAN state; source receives cancel ack |
| Target failure | Target preparation failure completes CreateUEContext with failure and releases target state |
| NTN location | NRNTNTAIInformation parsed/stored/copied and propagated to SMF; NR-CGI cell ID retains mapped-cell semantics |

## Inter-AMF RAN Status Transfer

The execution phase must not use the same-AMF shortcut.

Implemented sequence:

```text
S-gNB                   S-AMF                  T-AMF                 T-gNB
  | Uplink RAN Status     |                      |                     |
  |---------------------->|                      |                     |
  |                       | N1N2MessageTransfer  |                     |
  |                       | class=RAN            |                     |
  |                       | RAN_STATUS_TRANS...  |                     |
  |                       |--------------------->|                     |
  |                       |                      | Downlink RAN Status |
  |                       |                      |-------------------->|
```

The NGAP RANStatusTransfer transparent container is APER-encoded as a multipart
binary part and decoded by the target AMF before reuse in NGAP.

## NTN location map

For every handled `UserLocationInformationNR`:

1. Decode ordinary NR-CGI/TAI.
2. Inspect `iE-Extensions`.
3. If `NRNTNTAIInformation` is present, retain:
   - serving PLMN;
   - TAC list in NR NTN;
   - UE-location-derived TAC when present.
4. Copy NTN state from RAN UE to AMF UE when associated.
5. Include `NtnTaiInfo` in SMF NR location objects.

`NR-CGI.nRCellIdentity` is the mapped-cell value in the NTN case; there is
no additional Mapped Cell ID field in this NGAP location structure.

## Test map

### Unit tests

- `tests/unit/sbi-message-test.c`
  - bare CreateUEContext parsing;
  - RAN-class N1N2MessageTransfer parsing.
- `tests/unit/nrf-discovery-test.c`
  - exact AMF TAI match;
  - TAI mismatch rejection;
  - TAI-range matching;
  - OR matching across multiple `amfInfo` blocks.

### Integration test

Configuration: `configs/inter-amf-n2.yaml.in`

Test: `tests/transfer/inter-amf-n2-handover-test.c`

Launcher: `tests/transfer/abts-inter-amf-n2-main.c`

Meson target: `inter-amf-n2`

The test starts two AMFs using the existing indexed test launcher:

- AMF-1 / TAC 1 / source gNB;
- AMF-2 / TAC 2 / target gNB;
- one shared SMF/UPF.

It exercises registration, one PDU session, inter-AMF preparation, RAN status
relay, HandoverNotify, source release, target N3 user-plane ping and target
context cleanup.

## Source-layout cleanup

The temporary `ngap-handler-body.inc` and `nsmf-build-body.inc` macro
interposition used during remote editing has been removed. The corresponding
logic is directly in normal Open5GS C source files.

## Validation gate

As of this branch state there is **no successful build or runtime result**.

- GitHub Actions: zero branch/PR workflow runs.
- Connected remote build host: offline.
- Local sandbox: cannot clone/download the repository.

The code is therefore at the **implemented + test-harness-ready** stage, not
the validated/merge-ready stage.

Required validation when an execution environment is available:

1. Meson configure/build.
2. Unit suites covering SBI + NRF changes.
3. Focused `inter-amf-n2` integration test.
4. Existing `transfer` and `handover` 5GC regression suites.
5. Address compiler warnings/errors, sanitizer findings and runtime failures.
6. Only then mark M2 complete.

## M3 target

After M2 passes, implement the actual TN-NTN inter-PLMN HR topology:

```text
HPLMN                                           VPLMN
H-SMF/H-UPF(PSA) <----------- N9 ----------- V-UPF/V-SMF
       |                                           |
     H-AMF <------ N14/Namf via N32/SEPP ------> V-AMF
       |                                           |
 source RAN                                  target TN/NTN RAN
```

M3 must preserve the home PSA while introducing the visited SMF/UPF path and
routing inter-PLMN SBI through the SEPPs.
