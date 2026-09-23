# FlexRIC DQN xApp Integration

## 1. Purpose

This document describes how the frozen DQN scheduling model is integrated
into a FlexRIC xApp and connected to OpenAirInterface through the E2
interface.

The xApp forms the intelligence and control component of the runtime system.

Its responsibilities are:

```text
receive slice telemetry
        |
        v
construct DQN state
        |
        v
check freshness/completeness
        |
        v
normalize state
        |
        v
run ONNX inference
        |
        v
check selected action
        |
        v
send Slice-SM control
```

The current implementation is located at:

```text
xapp/xapp_kpm_moni.c
```

with corresponding build configuration under:

```text
xapp/CMakeLists.txt
```

---

## 2. Integration Context

The xApp runs as part of the FlexRIC near-RT RIC environment.

The runtime control loop is:

```text
OAI gNB
   |
   | E2SM-KPM
   v
FlexRIC near-RT RIC
   |
   v
DQN xApp
   |
   | ONNX Runtime
   v
DQN action
   |
   | Slice-SM control
   v
FlexRIC near-RT RIC
   |
   | E2 control
   v
OAI gNB
```

The xApp does not directly modify the MAC scheduler.

Instead, it sends a slice-weight control policy that is validated and
installed by OAI.

---

## 3. Current Runtime Assumption

The current implementation expects one connected E2 node.

The validated setup is therefore:

```text
one near-RT RIC
        |
        v
one E2-connected OAI gNB
        |
        v
one DQN xApp instance
```

Multi-gNB control has not yet been validated.

---

## 4. Frozen ONNX Model

The deployed model is:

```text
formal_dqn_v2_candidate_16000.onnx
```

Expected SHA-256:

```text
699c2d326b8c86f81d86929fcf28e3a00086687e0391c6a1bd4119b92001103e
```

Before performing inference, the xApp computes the model hash and compares
it against this expected value.

Successful startup includes:

```text
[DQN MODEL] Actual SHA256   = ...
[DQN MODEL] Expected SHA256 = ...
[DQN MODEL] SHA256 VERIFIED
```

This prevents an unintended ONNX model from being silently used as the
formally verified policy.

---

## 5. ONNX Runtime

The current validated runtime uses:

```text
ONNX Runtime 1.23.2
```

The neural-network interface is:

```text
input:
    3 floating-point values

output:
    22 Q-values
```

The model input represents:

```text
[
    eMBB load,
    URLLC load,
    mMTC load
]
```

The output corresponds to the 22 fixed DQN actions.

---

## 6. Model-Path Limitation

The current experimental xApp has used an absolute model path in the
development environment.

Example:

```text
/home/rpaudyal/openairinterface5g/formal-verification/py5chesim/models/
formal_dqn_v2_candidate_16000.onnx
```

This is a reproducibility limitation.

The publication repository stores the model at:

```text
models/formal_dqn_v2_candidate_16000.onnx
```

Future cleanup should make the model path configurable through:

```text
command-line argument
```

or:

```text
configuration file
```

rather than depending on a developer-specific absolute path.

---

## 7. KPM Telemetry Source

The DQN state is constructed from the custom KPM measurement:

```text
OAI.RlcTxQueuePktsDl
```

This measurement represents downlink RLC queue occupancy in packets for the
selected slice context.

The telemetry path is:

```text
OAI RLC
    |
    v
queue measurement
    |
    v
E2SM-KPM
    |
    v
FlexRIC
    |
    v
DQN xApp
```

---

## 8. Slice Definitions

The current system uses three S-NSSAI-defined slices.

| Slice | SST | SD |
|---|---:|---:|
| eMBB | 1 | 000001 |
| URLLC | 2 | 000002 |
| mMTC | 3 | 000003 |

The internal vector order is:

```text
index 0 -> eMBB
index 1 -> URLLC
index 2 -> mMTC
```

Maintaining this ordering is essential because the DQN was trained and
verified using the same state/action ordering.

---

## 9. KPM Subscriptions

The current xApp creates three separate KPM subscriptions.

### eMBB

```text
SST = 1
SD  = 000001
```

### URLLC

```text
SST = 2
SD  = 000002
```

### mMTC

```text
SST = 3
SD  = 000003
```

Each subscription uses:

```text
KPM Report Style 4
```

with an S-NSSAI filter.

The current implementation uses separate subscriptions rather than one
combined multi-action subscription.

---

## 10. State Construction

As KPM indications arrive, the xApp updates the state associated with the
corresponding slice.

Conceptually:

```text
eMBB KPM indication
        |
        +--> state[0]

URLLC KPM indication
        |
        +--> state[1]

mMTC KPM indication
        |
        +--> state[2]
```

The raw state is:

```text
[
    queue_eMBB,
    queue_URLLC,
    queue_mMTC
]
```

---

## 11. State Clamping

Each raw queue measurement is clamped to:

```text
[0, 500]
```

Example:

```text
raw = 2275
```

becomes:

```text
clamped = 500
```

This keeps the deployed model input within the numerical range used by the
training and formal-verification pipeline.

---

## 12. State Normalization

After clamping:

```text
normalized_value =
    clamped_value / 500.0
```

Therefore:

```text
0     -> 0.000
250   -> 0.500
500   -> 1.000
```

The resulting input vector is:

```text
[
    x_eMBB,
    x_URLLC,
    x_mMTC
]
```

---

## 13. Complete-State Requirement

The xApp does not perform DQN inference after receiving only one slice
measurement.

It tracks whether all three slice measurements have been observed.

Conceptually:

```text
seen = [
    seen_eMBB,
    seen_URLLC,
    seen_mMTC
]
```

Inference is permitted only after a complete three-slice state is
available.

This prevents inference from using an incomplete vector.

---

## 14. Freshness Requirement

The xApp also checks the age of each slice measurement.

The validated implementation uses a freshness threshold of approximately:

```text
2000 ms
```

The readiness decision therefore depends on both:

```text
all slices observed
```

and:

```text
all required measurements sufficiently recent
```

This protects the DQN from making a new decision using stale telemetry.

---

## 15. New-State Requirement

A new inference decision is associated with a newly completed state vector.

This avoids repeatedly treating an unchanged partial measurement set as a
new network observation.

The readiness logic can therefore be summarized as:

```text
complete state
    +
fresh state
    +
new complete vector
        |
        v
DQN inference
```

---

## 16. DQN Inference

When the state is ready, the normalized vector is passed to ONNX Runtime.

Example:

```text
[DQN INFERENCE]
input=[1.000000,1.000000,1.000000]
```

The network produces 22 Q-values.

The selected action is:

```text
argmax(Q0 ... Q21)
```

A validated saturated-state example produced:

```text
action = 7
weights = [20,80,0]
```

---

## 17. Action Table

Each DQN action corresponds to:

```text
[eMBB weight, URLLC weight, mMTC weight]
```

The xApp uses the same frozen 22-action table used during training and
formal verification.

The xApp does not generate arbitrary weight combinations.

This is important because the formal-verification result applies to the
specific frozen model and action interpretation.

---

## 18. Runtime Safety Check

After selecting an action, the xApp performs a deployment-time safety
check.

For active URLLC traffic:

```text
URLLC active
    =>
URLLC weight >= 20
```

Example:

```text
[DQN SAFETY]
URLLC_active=YES
w_URLLC=80
result=PASS
```

The runtime check complements rather than replaces the Marabou proof.

---

## 19. Control-on-Change Behavior

The xApp does not resend Slice-SM control every time the same action is
predicted.

If the newly selected action equals the currently installed action:

```text
action unchanged
```

the xApp logs that control is not resent.

Conceptually:

```text
new action
    |
    +-- same as current --> no E2 control
    |
    +-- different ------> send Slice-SM control
```

This reduces unnecessary repeated control signaling.

---

## 20. Slice-SM Control Mapping

When a new action must be installed, the three weights are mapped to the
three controlled slices.

The project convention is:

```text
Slice ID 0 -> eMBB
Slice ID 1 -> URLLC
Slice ID 2 -> mMTC
```

The selected action:

```text
[W_eMBB, W_URLLC, W_mMTC]
```

is encoded through the Slice Service Model and sent through the near-RT RIC
to OAI.

---

## 21. Meaning of Slice-SM Weight

The value transported through the Slice-SM NVS capacity representation is
used by this project as a normalized scheduling weight.

Conceptually:

```text
weight 20  -> 0.20
weight 80  -> 0.80
weight 100 -> 1.00
```

These values are not literal physical PRB numbers.

The OAI scheduler later converts the installed relative weights into
runtime scheduling budgets based on available Phase-3 resources.

---

## 22. OAI Control Validation

The receiving OAI control path validates the xApp policy before accepting
it.

Checks include:

- exactly three expected slices,
- expected slice IDs,
- no duplicate IDs,
- valid NVS representation,
- allowed weight range,
- available MAC instance,
- and membership in the fixed DQN action table.

An arbitrary vector outside the DQN action space is rejected.

---

## 23. Policy Persistence

A successful Slice-SM control changes the policy stored by the OAI slice
manager.

The installed policy persists in the running gNB after the xApp exits.

However:

```text
gNB restart
```

reinitializes the runtime state.

Therefore the xApp should be rerun after restarting the gNB if the DQN
policy needs to be installed again.

---

## 24. Subscription Cleanup

The current experimental xApp is not implemented as a permanent production
daemon.

At the end of the test run, it removes its KPM subscriptions and exits.

The installed OAI scheduling policy can remain active until the gNB is
restarted or another policy is installed.

A persistent production xApp would require a longer-running lifecycle and
additional failure/reconnection handling.

---

## 25. Required Startup Order

The validated startup order is:

```text
1. near-RT RIC
2. E2-connected OAI gNB
3. DQN xApp
```

This order ensures that an E2 node is already registered before the xApp
starts its telemetry/control workflow.

Starting the xApp before an E2 node is available has caused failures in the
current experimental FlexRIC path.

---

## 26. Runtime Binaries

The validated development environment uses:

```text
near-RT RIC:
~/flexric/build/examples/ric/nearRT-RIC
```

and:

```text
DQN xApp:
~/flexric/build/examples/xApp/c/monitor/xapp_kpm_moni
```

The publication repository stores the source separately under:

```text
xapp/
```

rather than distributing the build directory.

---

## 27. Runtime Ports

The validated FlexRIC setup uses:

```text
E2 interface:
36421
```

and:

```text
xApp E42 interface:
36422
```

The exact addresses and ports should be confirmed from the active
FlexRIC configuration during reproduction.

---

## 28. Representative Runtime Sequence

A successful execution can be summarized as:

```text
xApp starts
    |
    v
E42 setup
    |
    v
one E2 node detected
    |
    v
ONNX SHA verified
    |
    v
three Style-4 KPM subscriptions
    |
    v
queue measurements received
    |
    v
three-slice vector complete/fresh
    |
    v
ONNX inference
    |
    v
runtime safety check
    |
    v
new action?
    |
    +-- no --> retain installed policy
    |
    +-- yes
           |
           v
      Slice-SM control
           |
           v
      OAI policy installation
```

---

## 29. Current Limitations

The current xApp has several research-prototype limitations:

- one validated E2 node,
- fixed three-slice design,
- fixed 22-action table,
- developer-specific model-path assumptions,
- bounded experimental runtime,
- no validated multi-gNB coordination,
- no persistent fault-recovery loop,
- no validated automatic reconnection after RIC/gNB failure,
- and no USRP-specific changes yet tested.

These limitations should be considered when evaluating the implementation.

---

## 30. Relationship to Formal Verification

Formal verification applies to:

```text
frozen ONNX model
        +
bounded normalized input
        +
fixed action table
        +
URLLC minimum-weight property
```

The xApp integration itself has not been formally verified.

Instead, runtime checks provide assurance for:

```text
model identity
state completeness
state freshness
action interpretation
URLLC runtime safety
control-on-change
```

See:

```text
docs/FORMAL_VERIFICATION.md
```

---

## 31. Relationship to Scheduler Validation

The xApp determines the weight vector.

The OAI MAC scheduler determines how that weight vector is enforced in
actual RB scheduling.

Therefore:

```text
xApp validation
    !=
scheduler validation
```

The RB-level enforcement evidence is documented in:

```text
docs/RB_VALIDATION.md
```

---

## 32. Source and Patch Artifacts

The publication repository contains:

```text
xapp/xapp_kpm_moni.c
xapp/CMakeLists.txt
```

and the FlexRIC patch:

```text
patches/flexric/flexric-dqn-xapp.patch
```

These artifacts document the changes made relative to the recorded FlexRIC
baseline.

---

## 33. Summary

The xApp converts O-RAN telemetry into a formally verified DQN scheduling
decision and transports that decision back to OAI.

The runtime architecture is:

```text
KPM queue telemetry
        |
        v
three-slice state
        |
        v
freshness/completeness guard
        |
        v
normalization
        |
        v
verified ONNX model
        |
        v
22-action argmax
        |
        v
runtime safety check
        |
        v
control-on-change
        |
        v
Slice-SM
        |
        v
OAI slice manager
```

This design keeps neural-network inference in the near-RT RIC while leaving
actual RB allocation under deterministic OAI MAC scheduler control.
