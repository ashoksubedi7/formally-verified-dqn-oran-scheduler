# Reproduction Guide

## 1. Purpose

This document describes how to reconstruct and validate the research
prototype contained in this repository.

The target reproduction path is:

```text
Py5cheSim model
        |
        v
frozen ONNX
        |
        v
FlexRIC DQN xApp
        |
        v
OAI E2 integration
        |
        v
RFsim gNB + three slices
        |
        v
DQN Slice-SM control
        |
        v
RB-level scheduler validation
```

This is a working reproduction guide.

Some environment-specific steps are still being refined.

---

## 2. Repository

Clone the publication repository:

```bash
git clone \
  https://github.com/ashoksubedi7/formally-verified-dqn-oran-scheduler.git

cd formally-verified-dqn-oran-scheduler
```

Inspect the repository:

```bash
tree -L 3
```

The main directories are:

```text
docs/
formal-verification/
models/
xapp/
oai/
flexric/
configs/
patches/
evidence/
scripts/
cards/
```

---

## 3. External Dependencies

This repository does not vendor complete OAI or FlexRIC source trees.

You need external checkouts of:

```text
OpenAirInterface
FlexRIC
```

The repository instead preserves:

- baseline information,
- selected modified source files,
- patches,
- model artifacts,
- formal-verification evidence,
- and scheduler-validation evidence.

---

## 4. Recorded OAI Baseline

The recorded OAI baseline is:

```text
Repository:
openairinterface5g

Branch:
develop

Short commit:
fb944fb
```

The development tree used during implementation contained local
modifications.

Therefore reproduction should be based on:

```text
recorded upstream baseline
        +
project patch
```

rather than treating the modified development working tree as an upstream
commit.

The OAI patch is:

```text
patches/oai/oai-dqn-integration.patch
```

---

## 5. Recorded FlexRIC Baseline

The recorded FlexRIC baseline commit is:

```text
beabdd07
```

A branch name was not established in the preserved baseline output.

Therefore this document intentionally identifies FlexRIC by commit rather
than claiming a branch name.

The FlexRIC patch is:

```text
patches/flexric/flexric-dqn-xapp.patch
```

---

## 6. Baseline Precision

The currently documented OAI and FlexRIC baseline identifiers are short
Git hashes.

For stronger long-term reproducibility, future repository updates should
record:

```text
git rev-parse HEAD
```

for each dependency and preserve the complete 40-character commit ID.

The current short hashes are sufficient to identify the development
baseline in the present research workspace but are less robust for archival
reproduction.

---

## 7. Model Artifact

The frozen deployment model is:

```text
models/formal_dqn_v2_candidate_16000.onnx
```

Expected SHA-256:

```text
699c2d326b8c86f81d86929fcf28e3a00086687e0391c6a1bd4119b92001103e
```

Verify it with:

```bash
sha256sum models/formal_dqn_v2_candidate_16000.onnx
```

Do not continue with model-level claims if this hash differs.

---

## 8. Formal-Verification Artifacts

Formal-verification evidence is stored under:

```text
evidence/formal-verification/
```

including:

```text
formal_dqn_v2_final_verification.txt
marabou_formal_dqn_v2_results.txt
FORMAL_VERIFICATION_MANIFEST.txt
FORMAL_VERIFICATION_SHA256.txt
```

The source verifier is retained under:

```text
formal-verification/py5chesim/
```

The exact verification procedure is documented in:

```text
docs/FORMAL_VERIFICATION.md
```

---

## 9. Py5cheSim Training Source

The learning implementation associated with the frozen model is retained
under:

```text
formal-verification/py5chesim/
```

Important files include:

```text
formal_vanilla_v2.py
simulation_formal.py
InterSliceSch.py
IntraSliceSch.py
Cell.py
Slice.py
UE.py
```

The training/model design is documented in:

```text
docs/PY5CHESIM_LEARNING_MODEL.md
```

---

## 10. Apply the OAI Integration

Start from the recorded OAI baseline.

Conceptually:

```bash
cd ~/openairinterface5g
git checkout develop
git checkout fb944fb
```

Before applying changes, verify:

```bash
git rev-parse --short HEAD
```

Then apply the project patch from the publication repository.

Example:

```bash
git apply \
  ~/formally-verified-dqn-oran-scheduler/patches/oai/oai-dqn-integration.patch
```

If the patch does not apply cleanly, do not force it blindly.

Check:

```bash
git status --short
git diff --check
```

The patch was generated from a locally modified OAI tree, so a mismatch in
upstream source context must be reviewed manually.

---

## 11. OAI Integration Files

Important modified or added OAI components include:

```text
openair2/LAYER2/NR_MAC_gNB/
    gNB_scheduler_dlsch_default_policies.c
    main.c
    nr_mac_gNB.h
    nr_slice_manager.c
    nr_slice_manager.h

openair2/LAYER2/nr_rlc/
    nr_rlc_entity.h
    nr_rlc_entity_am.c
    nr_rlc_oai_api.c

openair2/E2AP/RAN_FUNCTION/
    CUSTOMIZED/ran_func_slice.c
    O-RAN/ran_func_kpm.c
    O-RAN/ran_func_kpm_subs.c
```

Reference copies are also preserved under:

```text
oai/
```

---

## 12. OAI Integration Responsibilities

The OAI changes implement four principal functions.

### RLC queue telemetry

Expose the queue information needed for:

```text
OAI.RlcTxQueuePktsDl
```

### KPM support

Export the custom measurement through E2SM-KPM.

### Slice-SM control

Receive and validate the three-slice weight vector.

### MAC enforcement

Apply the policy in the new-data Phase-3 downlink scheduling path.

---

## 13. Apply the FlexRIC Integration

Start from the recorded FlexRIC commit:

```bash
cd ~/flexric
git checkout beabdd07
```

Verify:

```bash
git rev-parse --short HEAD
```

Apply:

```bash
git apply \
  ~/formally-verified-dqn-oran-scheduler/patches/flexric/flexric-dqn-xapp.patch
```

Then inspect:

```bash
git status --short
git diff --check
```

The main modified xApp files are represented in the publication repository
under:

```text
xapp/
```

---

## 14. ONNX Runtime Dependency

The validated xApp used:

```text
ONNX Runtime 1.23.2
```

The development environment contained a local ONNX Runtime installation for
FlexRIC.

The xApp must be compiled and linked against a compatible ONNX Runtime C
API/library.

Exact installation layout can vary by system.

The runtime version should be confirmed when the xApp starts.

Expected log:

```text
[DQN ONNX] Runtime version = 1.23.2
```

---

## 15. Model Path

The current experimental xApp has used an absolute model path from the
development workspace.

This must be adapted when reproducing in another directory.

Preferred repository model location:

```text
~/formally-verified-dqn-oran-scheduler/models/
formal_dqn_v2_candidate_16000.onnx
```

Until the xApp is changed to accept a configurable model path, either:

1. update the source path intentionally before building, or
2. provide the expected filesystem path through an explicit symlink.

Always verify the resulting SHA-256.

---

## 16. Build Status

The validated development environment successfully built:

```text
nr-softmodem
```

with the OAI integration and built:

```text
nearRT-RIC
xapp_kpm_moni
```

with the FlexRIC/xApp integration.

This repository does not yet claim a completely automated clean-room build.

Exact dependency installation and build automation remain candidates for
future scripts under:

```text
scripts/
```

---

## 17. 5G Core

The validated RFsim experiments used an OAI 5G Core deployment.

Recorded addresses included:

```text
AMF:
192.168.70.138

gNB N3:
192.168.70.129

external data network:
192.168.70.130
```

These addresses are environment-specific.

A reproducer may use different addresses but must update the gNB/5GC
configuration consistently.

---

## 18. gNB RFsim Configuration

The validated gNB configuration is based on:

```text
gnb.sa.band78.fr1.106PRB.usrpb210.conf
```

A copy is preserved under:

```text
configs/oai/
```

The validated radio configuration is approximately:

| Parameter | Value |
|---|---|
| Band | n78 |
| Bandwidth | 40 MHz |
| Numerology | 1 |
| SCS | 30 kHz |
| Nominal PRBs | 106 |
| Duplex | TDD |

The scheduling implementation should not interpret DQN weights as fixed
fractions of these literal 106 PRBs.

---

## 19. Validated RFsim gNB Command

From:

```text
~/openairinterface5g/cmake_targets/ran_build/build
```

the validated gNB launch command was:

```bash
sudo ./nr-softmodem \
  -O ../../../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band78.fr1.106PRB.usrpb210.conf \
  --rfsim \
  --gNBs.[0].amf_ip_address.[0].ipv4 192.168.70.138 \
  --gNBs.[0].min_rxtxtime 6
```

The AMF address must be changed if the reproducer uses a different 5GC
network.

---

## 20. Slice Configuration

The validated three-slice setup is:

```text
eMBB
SST = 1
SD  = 000001
```

```text
URLLC
SST = 2
SD  = 000002
```

```text
mMTC
SST = 3
SD  = 000003
```

The same ordering must remain consistent across:

```text
OAI NSSAI configuration
KPM subscriptions
DQN state
DQN action table
Slice-SM control
OAI slice manager
```

---

## 21. FlexRIC Startup Order

The validated startup sequence is:

```text
1. near-RT RIC
2. E2-connected gNB
3. DQN xApp
```

Do not start the xApp first in the current experimental setup.

The xApp expects a connected E2 node before starting the complete
telemetry/control flow.

---

## 22. Start the near-RT RIC

The validated binary location is:

```bash
cd ~/flexric/build/examples/ric
./nearRT-RIC
```

Confirm that the RIC starts and waits for an E2 node.

The E2 transport commonly uses:

```text
port 36421
```

in the validated setup.

---

## 23. Start the OAI gNB

After the RIC is running, start the E2-enabled gNB using the RFsim command
documented above.

Confirm that the gNB establishes the E2 connection with the RIC.

Do not continue to the xApp until the RIC shows a connected E2 node.

---

## 24. Start the DQN xApp

After the E2-connected gNB is present:

```bash
cd ~/flexric/build/examples/xApp/c/monitor
./xapp_kpm_moni
```

The xApp E42 interface uses:

```text
port 36422
```

in the validated configuration.

---

## 25. Expected xApp Startup Evidence

A successful startup should include evidence similar to:

```text
Connected E2 nodes = 1
```

followed by:

```text
[DQN MODEL] SHA256 VERIFIED
```

and:

```text
[DQN ONNX] Runtime version = 1.23.2
```

The xApp should then create three KPM subscriptions.

---

## 26. Expected KPM Subscriptions

Expected subscription setup includes:

```text
eMBB:
SST=1 SD=000001

URLLC:
SST=2 SD=000002

mMTC:
SST=3 SD=000003
```

and:

```text
Report Style 4
```

for each subscription.

---

## 27. Expected Queue Telemetry

As traffic is generated, the xApp should receive:

```text
OAI.RlcTxQueuePktsDl
```

for the slices.

Representative state logs have the form:

```text
[DQN STATE]
[DQN VECTOR]
[DQN FRESHNESS]
[DQN READY]
```

Inference should occur only after the three-slice state is complete and
fresh.

---

## 28. Expected Model Inference

A saturated-state example is:

```text
[DQN INFERENCE]
input=[1.000000,1.000000,1.000000]
```

with the validated candidate producing:

```text
action=7
weights=[20,80,0]
```

The runtime safety check should report:

```text
URLLC_active=YES
w_URLLC=80
result=PASS
```

---

## 29. Expected Slice Control

If the selected action differs from the currently installed action, the
xApp should send Slice-SM control.

Expected progression:

```text
DQN inference
    |
    v
new action
    |
    v
[DQN E2 CONTROL]
    |
    v
Slice-SM request
    |
    v
OAI control handler
    |
    v
policy installed
```

If the action is unchanged, the xApp should avoid resending control.

---

## 30. gNB Restart Behavior

Restarting the gNB resets its runtime policy state.

Therefore after a gNB restart:

```text
restart RIC/gNB as required
        |
        v
wait for E2 connection
        |
        v
rerun xApp
```

to reinstall the DQN-selected policy.

---

## 31. RFsim UE Environment

The validated RFsim environment used three simulated UEs corresponding to
the three slices.

Example RFsim underlay addressing from the development environment was:

```text
UE1 / eMBB:
10.201.1.1 -> gNB 10.201.1.100

UE2 / URLLC:
10.202.1.2 -> gNB 10.202.1.100

UE3 / mMTC:
10.203.1.3 -> gNB 10.203.1.100
```

Application/TUN addresses are dynamically assigned and should not be
hard-coded in the reproduction procedure.

---

## 32. Traffic Generation

To validate scheduler behavior, generate concurrent downlink traffic toward
the three UEs.

The exact TUN addresses must be discovered at runtime.

For congestion validation, the objective is to create sufficient backlog so
that:

```text
eMBB
URLLC
mMTC
```

are simultaneously requesting resources.

The queue telemetry should reflect the resulting load.

---

## 33. DQN Saturation Check

Under sufficiently heavy three-slice traffic, state values may clamp to:

```text
[500,500,500]
```

and normalize to:

```text
[1.000,1.000,1.000]
```

For the preserved model, this state has produced:

```text
action 7
[20,80,0]
```

This is a useful reference point for checking inference consistency.

---

## 34. Scheduler Evidence

RB-level evidence is stored at:

```text
evidence/rb-validation/dqn_rb_validation_evidence.log
```

Inspect it with:

```bash
grep '\[DQN RB VALID\]' \
  evidence/rb-validation/dqn_rb_validation_evidence.log
```

The detailed interpretation is documented in:

```text
docs/RB_VALIDATION.md
```

---

## 35. Validated Scheduler Snapshot

The scheduler source used for the preserved validation is stored at:

```text
evidence/rb-validation/
gNB_scheduler_dlsch_default_policies.c.dqn_rb_validated
```

This allows the RB-validation log to be associated with the scheduler
implementation used in that experiment.

---

## 36. Scheduler Hash Evidence

Hash information is stored under:

```text
evidence/hashes/
```

including:

```text
dqn_rb_validated_sha256.txt
```

Use:

```bash
cat evidence/hashes/dqn_rb_validated_sha256.txt
```

to inspect the recorded artifact hashes.

---

## 37. Representative RB Validation Result

A representative saturated window used:

```text
weight=[20,80,0]
```

with:

```text
target=[21000,85000,0]
```

and successful protected allocation approximately preserving the intended:

```text
20 : 80
```

relative eMBB/URLLC priority.

Residual resources were also observed being borrowed by mMTC.

The interpretation must remain:

```text
protected scheduling priority
```

not:

```text
guaranteed final throughput percentage
```

---

## 38. Formal Verification Check

The formal result should be reviewed independently from the scheduler
experiment.

The verified property is:

```text
URLLC active
    =>
selected URLLC weight >= 20
```

for the bounded normalized domain described in:

```text
docs/FORMAL_VERIFICATION.md
```

The proof applies to the frozen DQN, not to the entire OAI/FlexRIC system.

---

## 39. Reproduction Success Criteria

A useful reproduction should establish all of the following:

```text
1. Correct ONNX SHA
2. OAI gNB builds
3. FlexRIC RIC/xApp build
4. E2 connection established
5. Three KPM subscriptions created
6. Queue measurements received
7. Complete/fresh state generated
8. ONNX inference succeeds
9. Runtime safety check passes
10. Slice-SM control accepted
11. OAI policy installed
12. DQN scheduler path executes
13. RB-validation logs are produced
```

A reproduction should not be considered complete merely because the model
loads successfully.

---

## 40. Common Troubleshooting Notes

### xApp starts before gNB

Use:

```text
RIC -> gNB -> xApp
```

rather than starting the xApp first.

### Model SHA mismatch

Stop and verify that the exact ONNX artifact is being used.

### No DQN inference

Check:

```text
three KPM subscriptions
queue metric
state seen flags
freshness
```

### No control message

The predicted action may be unchanged.

### Policy disappears

A gNB restart resets the runtime policy; rerun the xApp.

### Log output missing

When capturing gNB output through a pipe, standard output buffering may
delay some direct `printf` output.

Prefer normal OAI logging and validated evidence counters when possible.

---

## 41. Debugging Caution

Attaching a live debugger to the timing-sensitive RFsim gNB can disrupt UE
operation and invalidate the experiment.

For runtime validation, prefer:

```text
instrumentation
structured logs
preserved evidence
```

over pausing the live gNB process.

---

## 42. Hardware / USRP Reproduction

USRP testing is the next stage and is not yet documented as a completed
reproduction path.

The intended transition is:

```text
RFsim
    |
    v
USRP RF backend
```

while preserving:

```text
DQN model
KPM telemetry
FlexRIC xApp
Slice-SM control
OAI slice manager
MAC scheduler
RB instrumentation
```

Before defining an exact hardware command, record:

- USRP model,
- UHD version,
- clock source,
- time source,
- RF frequency,
- gains,
- UE/SIM configuration,
- and authorized RF test environment.

---

## 43. Current Reproduction Limitations

The repository currently has several known reproducibility limitations:

- OAI/FlexRIC full commit hashes should still be recorded,
- the xApp model path should become configurable,
- complete clean-build automation is not yet provided,
- exact training random seeds are not yet preserved,
- the full Py5cheSim transition dataset is not preserved,
- USRP reproduction has not yet been completed,
- and some environment/network addresses are lab-specific.

These limitations should remain visible rather than being hidden from the
reproduction record.

---

## 44. Recommended Reproduction Order

The safest order for a new researcher is:

```text
1. Clone publication repository
2. Verify model SHA
3. Review BASELINES.md
4. Obtain exact OAI baseline
5. Apply OAI patch
6. Build OAI
7. Obtain exact FlexRIC baseline
8. Apply FlexRIC patch
9. Install/link ONNX Runtime
10. Build FlexRIC + xApp
11. Start 5GC
12. Start near-RT RIC
13. Start RFsim gNB
14. Start three RFsim UEs
15. Confirm E2 connection
16. Start DQN xApp
17. Generate traffic
18. Confirm DQN inference
19. Confirm Slice-SM policy installation
20. Collect RB-validation evidence
```

---

## 45. Related Documentation

System design:

```text
docs/ARCHITECTURE.md
```

Learning model:

```text
docs/PY5CHESIM_LEARNING_MODEL.md
```

Formal verification:

```text
docs/FORMAL_VERIFICATION.md
```

xApp integration:

```text
docs/XAPP_INTEGRATION.md
```

RB validation:

```text
docs/RB_VALIDATION.md
```

Model and data documentation:

```text
cards/MODEL_CARD.md
cards/DATA_CARD.md
```

---

## 46. Reproduction Summary

The project is reproduced as a chain rather than as a standalone neural
network:

```text
frozen verified DQN
        |
        v
FlexRIC xApp
        |
        v
KPM telemetry
        |
        v
DQN inference
        |
        v
Slice-SM control
        |
        v
OAI slice manager
        |
        v
work-conserving MAC scheduler
        |
        v
RB-level evidence
```

The primary reproducibility principle is:

```text
baseline source
    +
versioned project changes
    +
cryptographically identified model
    +
preserved runtime evidence
```

rather than relying on an undocumented modified development machine.
