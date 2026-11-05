# Upstream integration checkpoint — 2026-11-05

## Provenance

- Verified 5G SA HR baseline: `1963d1126f65cf3908e0e6dc7b5d39a6ac0489a7`
- Open5GS upstream `main`: `e85748ef3d798fa0b4435c80438e81c41bbe5e13`
- Integration branch: `integration/upstream-2026-11-05`
- Worker: `rdc-worker-02` (`/workspace/open5gs-upstream-integration`)
- The canonical `inter-plmn-hr-n2-ho` branch must remain unchanged pending
  successful integration regression tests.

## Source merge and fixes

Four textual conflicts were resolved in:
- `src/amf/gmm-handler.c`: retain inter-AMF post-handover UP preservation
  while respecting upstream authenticated-registration release handling.
- `src/amf/namf-handler.c`: retain RAN-class N1N2 transfer without a
  session ID; enforce upstream PDU session ID validation for SM cases;
  preserve the distinction between UE-reported and AMF-transferred ngKSI.
- `src/amf/ngap-handler.c`: retain inter-AMF discovery/preparation and
  upstream handover attempt metrics.
- `src/amf/sbi-path.h`: assign distinct action IDs for
  authenticated registration (35) and V-SMF-only release (36).

New upstream unit-test source interposition had to be extended to rename
the fork's handover functions in `tests/unit/amf-namf-test.c` and
`tests/unit/amf-registration-test.c`. The former also initializes the
`rat_restrictions` list, which production `amf_ue_add()` initializes.

## Validation on rdc-worker-02

- Meson configure: PASS (Open5GS 2.8.0; release build).
- Full Ninja build: PASS (all targets; `ninja -C build -j3`).
- `meson test -C build --no-rebuild --suite unit`: PASS, 4/4 test
  executables (core, crypt, SCTP, unit).
- `meson test -C build --no-rebuild inter-amf-n2 inter-plmn-hr-n2`:
  NOT VALIDATED. Both aborted during `test_5gc_init()`, before any
  handover assertion, because `ogs_dbi_init(ogs_app()->db_uri)` failed.
  There is no accessible MongoDB service on this worker. Treat these as
  environment failures, not passing or failing mobility test results.

## Promotion gate

Do not advance `inter-plmn-hr-n2-ho` until:
1. A functional MongoDB test instance is available.
2. Inter-AMF N2 and inter-PLMN HR N2 regression tests pass, including
   reverse HO, cancellation, idle mobility, post-HO GTP-U and End Marker.
3. EPC attach/S1/X2 and relevant 5GC registration and transfer suites pass.
4. The integration changes are reviewed for behavioural regressions.
