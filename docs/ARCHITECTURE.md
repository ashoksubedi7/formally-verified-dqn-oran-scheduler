# System Architecture

## 1. Overview

This project integrates a formally verified Deep Q-Network (DQN) with
OpenAirInterface (OAI) and FlexRIC to perform adaptive, slice-aware
downlink resource scheduling in an O-RAN environment.

The architecture connects radio-access-network telemetry, machine-learning
inference, formal assurance, E2 control, and NR MAC scheduling.

The complete control path is:

```text
UE traffic
    |
    v
OAI RLC queues
    |
    v
Per-slice queue measurement
    |
    v
E2SM-KPM
    |
    v
FlexRIC near-RT RIC
    |
    v
DQN xApp
    |
    v
ONNX Runtime inference
    |
    v
DQN action / slice weights
    |
    v
E2 Slice-SM control
    |
    v
OAI Slice Manager
    |
    v
NR MAC Scheduler
    |
    v
Downlink RB allocation
```

The system is divided into four major layers:

1. **Telemetry layer** — obtains per-slice RLC queue state from OAI.
2. **Intelligence layer** — performs DQN inference in the FlexRIC xApp.
3. **Control layer** — transfers selected scheduling weights through E2.
4. **Scheduling layer** — applies the policy in the OAI NR MAC scheduler.

---

## 2. Network Slices

The current implementation uses three network slices.

| Slice | SST | SD |
|---|---:|---:|
| eMBB | 1 | 000001 |
| URLLC | 2 | 000002 |
| mMTC | 3 | 000003 |

The scheduler uses the following internal mapping:

```text
Slice 0 -> eMBB
Slice 1 -> URLLC
Slice 2 -> mMTC
```

The slice identity is derived from S-NSSAI information and used throughout
the telemetry, control, and scheduling pipeline.

---

## 3. End-to-End Data and Control Flow

The project creates a closed control loop between OAI and the near-RT RIC.

```text
                    TELEMETRY PATH

     +-----------------------------------------+
     |              OpenAirInterface           |
     |                                         |
UE ->| RLC queue -> KPM measurement generation |
     +--------------------+--------------------+
                          |
                          | E2SM-KPM
                          v
              +-----------------------+
              | FlexRIC near-RT RIC   |
              +-----------+-----------+
                          |
                          v
              +-----------------------+
              |      DQN xApp         |
              |                       |
              | state construction    |
              | normalization         |
              | ONNX inference        |
              | safety checks         |
              +-----------+-----------+
                          |
                          | selected action
                          v

                    CONTROL PATH

              +-----------------------+
              | Slice weight vector   |
              +-----------+-----------+
                          |
                          | Slice-SM Control
                          v
     +--------------------+--------------------+
     |              OpenAirInterface           |
     |                                         |
     | E2 control handler                      |
     |        |                                |
     |        v                                |
     | Slice Manager                           |
     |        |                                |
     |        v                                |
     | NR MAC Scheduler                        |
     |        |                                |
     |        v                                |
     | Physical RB allocation                  |
     +-----------------------------------------+
```

---

## 4. DQN State

The DQN receives one queue-load value for each slice.

The state vector is:

```text
[eMBB queue, URLLC queue, mMTC queue]
```

In symbolic form:

```text
[L_eMBB, L_URLLC, L_mMTC]
```

The raw queue values are bounded to:

```text
[0, 500]
```

before normalization.

The normalization rule is:

```text
normalized_queue = clamp(raw_queue, 0, 500) / 500
```

Therefore each DQN input lies in:

```text
[0, 1]
```

and the deployed input vector has the form:

```text
[x_eMBB, x_URLLC, x_mMTC]
```

---

## 5. DQN Model

The deployed frozen model is:

```text
formal_dqn_v2_candidate_16000.onnx
```

Its SHA-256 hash is:

```text
699c2d326b8c86f81d86929fcf28e3a00086687e0391c6a1bd4119b92001103e
```

The model receives three floating-point inputs and produces 22 Q-values.

Conceptually:

```text
3-dimensional state
        |
        v
      DQN
        |
        v
22 Q-values
        |
        v
     argmax
        |
        v
selected action
```

Action selection is:

```text
a* = argmax(Q0, Q1, ..., Q21)
```

Each output corresponds to one predefined resource-weight vector.

For example:

```text
Action 7 -> [20, 80, 0]
```

This represents:

```text
eMBB  weight = 20
URLLC weight = 80
mMTC  weight = 0
```

These values are scheduler preference weights.

They are **not literal PRB counts**.

---

## 6. DQN Action Space

The DQN uses a fixed 22-action scheduling space.

Each action has the form:

```text
[eMBB weight, URLLC weight, mMTC weight]
```

The action table is:

| Action | eMBB | URLLC | mMTC |
|---:|---:|---:|---:|
| 0 | 0 | 0 | 0 |
| 1 | 0 | 0 | 100 |
| 2 | 0 | 20 | 80 |
| 3 | 0 | 40 | 60 |
| 4 | 0 | 60 | 40 |
| 5 | 0 | 80 | 20 |
| 6 | 0 | 100 | 0 |
| 7 | 20 | 80 | 0 |
| 8 | 40 | 60 | 0 |
| 9 | 60 | 40 | 0 |
| 10 | 80 | 20 | 0 |
| 11 | 100 | 0 | 0 |
| 12 | 80 | 0 | 20 |
| 13 | 60 | 0 | 40 |
| 14 | 40 | 0 | 60 |
| 15 | 20 | 0 | 80 |
| 16 | 60 | 20 | 20 |
| 17 | 20 | 60 | 20 |
| 18 | 20 | 20 | 60 |
| 19 | 40 | 40 | 20 |
| 20 | 40 | 20 | 40 |
| 21 | 20 | 40 | 40 |

The runtime OAI slice manager accepts only valid vectors from this frozen
action space.

---

## 7. KPM Telemetry

Per-slice queue state is transported from OAI to the xApp using E2SM-KPM.

The implementation uses three separate KPM subscriptions.

```text
eMBB
    SST = 1
    SD  = 000001

URLLC
    SST = 2
    SD  = 000002

mMTC
    SST = 3
    SD  = 000003
```

The project introduces the custom queue metric:

```text
OAI.RlcTxQueuePktsDl
```

This metric represents downlink RLC queue occupancy in packets.

The telemetry path is:

```text
RLC queue
    |
    v
OAI KPM measurement
    |
    v
E2SM-KPM indication
    |
    v
FlexRIC
    |
    v
DQN xApp
```

---

## 8. Three-Slice State Construction

The xApp maintains queue information for all three slices.

A DQN decision is not made from an incomplete state.

The xApp waits until:

- eMBB state is available,
- URLLC state is available,
- mMTC state is available,
- all measurements satisfy freshness requirements,
- and a new complete state vector is available.

The resulting state is:

```text
raw_state =
[
    eMBB_queue,
    URLLC_queue,
    mMTC_queue
]
```

which is converted into:

```text
normalized_state =
[
    clamp(eMBB_queue, 0, 500) / 500,
    clamp(URLLC_queue, 0, 500) / 500,
    clamp(mMTC_queue, 0, 500) / 500
]
```

---

## 9. FlexRIC DQN xApp

The xApp performs the intelligence and control portion of the architecture.

Its high-level execution pipeline is:

```text
Receive KPM indication
        |
        v
Identify slice
        |
        v
Update queue state
        |
        v
Check state completeness
        |
        v
Check freshness
        |
        v
Normalize state
        |
        v
Run ONNX Runtime inference
        |
        v
Obtain 22 Q-values
        |
        v
argmax
        |
        v
Selected DQN action
        |
        v
Runtime safety check
        |
        v
Generate Slice-SM control
```

The xApp also verifies the SHA-256 hash of the ONNX model before using it.

This ties runtime inference to the exact model that was formally verified.

---

## 10. Runtime Safety Check

Formal verification proves a property of the frozen DQN over the verified
input domain.

The deployment additionally performs a runtime safety check.

For active URLLC traffic, the expected property is:

```text
URLLC active
    =>
selected URLLC weight >= 20
```

This runtime monitor does not replace the formal proof.

It provides an additional deployment-time assurance layer around the neural
network.

---

## 11. Slice-SM Control

The selected DQN action is translated into a three-slice weight vector.

The control convention is:

```text
Slice ID 0 -> eMBB
Slice ID 1 -> URLLC
Slice ID 2 -> mMTC
```

The weights are transferred using Slice Service Model control.

The NVS capacity field carries the normalized scheduling weight.

Conceptually:

```text
DQN action
    |
    v
[W_eMBB, W_URLLC, W_mMTC]
    |
    v
Slice-SM control message
    |
    v
OAI E2 control handler
```

The weights describe scheduling preference.

They are not transmitted as fixed literal PRB counts.

---

## 12. OAI Slice Control Handler

The OAI Slice-SM control path validates incoming DQN policies.

The control handler checks conditions including:

- exactly three expected slices,
- valid slice IDs,
- no duplicate slice IDs,
- expected scheduler configuration,
- weight values within the permitted range,
- and membership in the predefined DQN action space.

Invalid policy vectors are rejected.

Valid vectors are passed to the OAI slice manager.

---

## 13. OAI Slice Manager

The project introduces:

```text
nr_slice_manager.c
nr_slice_manager.h
```

The slice manager provides a central representation of the currently
installed DQN scheduling policy.

Its responsibilities include:

- storing the active slice weights,
- validating policy updates,
- exposing a consistent scheduler-visible snapshot,
- mapping traffic to the three known slices,
- and rejecting vectors outside the frozen DQN action space.

Conceptually:

```text
Slice-SM Control
      |
      v
nr_slice_manager_update()
      |
      v
Validated policy
      |
      v
Atomic scheduler snapshot
      |
      v
NR MAC scheduler
```

---

## 14. NR MAC Scheduler Integration

The DQN does not replace the entire OAI MAC scheduler.

The existing scheduler contains multiple scheduling phases.

The major downlink phases are conceptually:

```text
Phase 1
HARQ retransmissions

Phase 2
No-data control scheduling
- timing advance
- beam-related control
- MAC control elements

Phase 3
New-data proportional-fair scheduling
```

The DQN policy controls **Phase 3 new-data scheduling**.

HARQ retransmissions and required control operations remain outside the
DQN-controlled Phase-3 budget.

This preserves important existing OAI scheduler behavior.

---

## 15. Resource Demand Calculation

Before allocating resources, the scheduler determines how much new-data
resource each scheduling candidate can use.

Candidate demand contributes to per-slice aggregate demand:

```text
Demand_eMBB
Demand_URLLC
Demand_mMTC
```

The DQN weights are then applied against the available Phase-3 resource
ceiling.

The implementation does not assume that the entire nominal 106-PRB carrier
is available to every scheduling candidate at every instant.

Actual allocation still depends on:

- BWP limits,
- time-domain allocation,
- symbol masks,
- existing VRB occupancy,
- minimum RB requirements,
- maximum RB requirements,
- and physical scheduling feasibility.

---

## 16. Protected Scheduling Budgets

For a DQN policy such as:

```text
[20, 80, 0]
```

the scheduler initially constructs weighted target budgets.

Conceptually:

```text
Available Phase-3 resource
        |
        +---- 20% target -> eMBB
        |
        +---- 80% target -> URLLC
        |
        +----- 0% target -> mMTC
```

Integer allocation uses deterministic rounding.

The resulting initial values are tracked as:

```text
target[]
```

The target is then capped according to actual slice demand.

This produces:

```text
protected[]
```

The protected budget represents the scheduling opportunity reserved for
that slice before redistribution.

---

## 17. Work-Conserving Redistribution

The scheduler does not implement rigid slicing.

Suppose the policy is:

```text
[20, 80, 0]
```

but URLLC cannot use its full protected budget.

The unused portion is not intentionally left idle.

Instead:

```text
URLLC unused protected budget
            |
            v
       Common pool
            |
            v
Redistributed to backlogged slices
```

After redistribution, the scheduler records:

```text
effective[]
```

The effective budget may therefore be larger than the original protected
budget for another backlogged slice.

This behavior makes the scheduler work-conserving.

---

## 18. Zero-Weight Slice Borrowing

A DQN weight of zero does not necessarily mean permanent starvation.

For example:

```text
[20, 80, 0]
```

assigns mMTC a protected weight of zero.

However, after protected allocation and redistribution, residual physical
capacity may still exist.

A backlogged zero-weight slice may borrow that residual capacity.

Therefore:

```text
weight = 0
```

means:

```text
no protected DQN scheduling share
```

not:

```text
never schedule this slice
```

This distinction is important for interpreting runtime results.

---

## 19. Final Opportunistic Borrowing

After the weighted first allocation pass, the scheduler performs an
opportunistic residual-allocation stage.

This allows remaining physically schedulable capacity to be used rather
than left idle.

The final behavior is therefore:

```text
DQN target
    |
    v
Demand-capped protected budget
    |
    v
Common-pool redistribution
    |
    v
First-pass scheduling
    |
    v
Residual borrowing
```

This design separates **priority protection** from **resource wastage**.

---

## 20. Meaning of DQN Weights

DQN weights should be interpreted as protected scheduling preference.

They should not be interpreted as guaranteed final throughput percentages.

For example:

```text
[20, 80, 0]
```

does **not** imply:

```text
eMBB  = exactly 20% final throughput
URLLC = exactly 80% final throughput
mMTC  = exactly 0% final throughput
```

Actual throughput also depends on:

- queue demand,
- channel quality,
- MCS,
- retransmissions,
- transport-block size,
- time-domain scheduling,
- symbol allocation,
- UE conditions,
- and residual resource borrowing.

The more accurate interpretation is:

```text
DQN weights
    =
protected Phase-3 scheduling opportunity
```

---

## 21. RB-Level Instrumentation

The MAC scheduler includes runtime instrumentation for validating the
actual DQN-controlled resource behavior.

The instrumentation reports:

```text
weight
demand
target
protected
effective
pass1
protected_used
redistributed_used
borrow
unknown_borrow
phase3_committed
```

The fields have the following meanings.

### `weight`

Current DQN slice-weight vector.

### `demand`

Requested Phase-3 RB demand for each slice.

### `target`

Initial weighted target based on the DQN policy.

### `protected`

Target after being capped by actual demand.

### `effective`

Budget after unused protected resources are redistributed.

### `pass1`

RBs successfully committed during the initial allocation pass.

### `protected_used`

Successful allocation within the original protected budget.

### `redistributed_used`

Successful first-pass allocation obtained from redistributed common-pool
capacity.

### `borrow`

Residual RB allocation obtained in the final opportunistic borrowing pass.

### `unknown_borrow`

Residual allocation to candidates not classified into the three expected
slices.

### `phase3_committed`

Total successfully committed Phase-3 RB count during the reporting window.

---

## 22. Runtime Validation Example

During simultaneous saturated traffic from all three slices, the DQN
observed approximately:

```text
state = [1.0, 1.0, 1.0]
```

and selected:

```text
Action 7
weights = [20, 80, 0]
```

One representative instrumentation window reported:

```text
weight=[20,80,0]

demand=[
    106000,
    106000,
    106000
]

target=[
    21000,
    85000,
    0
]

protected=[
    21000,
    85000,
    0
]

effective=[
    21000,
    85000,
    0
]

pass1=[
    16086,
    62661,
    0
]

protected_used=[
    16086,
    62661,
    0
]

redistributed_used=[
    0,
    0,
    0
]

borrow=[
    0,
    0,
    23927
]

phase3_committed=102674
```

This demonstrates two different behaviors:

1. eMBB and URLLC receive the DQN-protected scheduling opportunity.
2. mMTC can still use residual capacity through borrowing.

---

## 23. Observed Protected Ratio

Across saturated validation windows, successful protected allocation between
eMBB and URLLC was approximately:

```text
eMBB  = 20.4%
URLLC = 79.6%
```

This closely follows the DQN-selected protected ratio:

```text
20 : 80
```

The result should be interpreted as validation of protected RB scheduling
behavior.

It should not be interpreted as proof that final application throughput
must exactly follow the same percentage.

---

## 24. Common-Pool Redistribution Example

The runtime instrumentation also captured a case where URLLC demand was
lower than its full weighted target.

A representative window showed:

```text
target=[
    21000,
    85000,
    0
]

protected=[
    21000,
    50452,
    0
]

effective=[
    55548,
    50452,
    0
]

redistributed_used=[
    26091,
    0,
    0
]
```

This shows that unused URLLC protected capacity was returned to the common
pool and made available to eMBB.

This directly validates the intended work-conserving redistribution
mechanism.

---

## 25. Zero-Weight mMTC Validation

During saturated three-slice testing with:

```text
weights = [20, 80, 0]
```

mMTC had no protected target.

Nevertheless, mMTC received approximately:

```text
23,000 - 24,000 RB
```

per 1000 DQN-controlled Phase-3 scheduler invocations in representative
validation windows.

These RBs were recorded through the borrowing path.

This demonstrates that the scheduler avoids unnecessary starvation of a
zero-weight slice when residual capacity exists.

---

## 26. Experimental Platform

The current validated implementation uses:

- OpenAirInterface
- FlexRIC
- near-RT RIC
- E2 interface
- OAI 5G Core
- RFsim
- three simulated UEs
- three S-NSSAI slices
- ONNX Runtime
- formally verified DQN policy

The tested radio configuration uses:

| Parameter | Value |
|---|---|
| NR band | n78 |
| Bandwidth | 40 MHz |
| Subcarrier spacing | 30 kHz |
| Numerology | 1 |
| Nominal NR PRBs | 106 |
| Duplex | TDD |

The scheduler logic does not hard-code the DQN weights as literal portions
of these 106 PRBs.

---

## 27. OAI Baseline

The OAI integration is based on:

```text
Repository: openairinterface5g
Branch: develop
Commit: fb944fb
```

Project-specific changes are preserved under:

```text
oai/
```

and as a patch under:

```text
patches/oai/oai-dqn-integration.patch
```

---

## 28. FlexRIC Baseline

The FlexRIC integration is based on commit:

```text
beabdd07
```

The project-specific xApp source is stored under:

```text
xapp/
```

and the corresponding patch is stored under:

```text
patches/flexric/flexric-dqn-xapp.patch
```

---

## 29. Formal Assurance Layer

The frozen DQN has been formally checked with Marabou.

The verified safety property is:

```text
URLLC active
    =>
selected URLLC weight >= 20
```

for the normalized domain:

```text
x_eMBB  in [0, 1]
x_URLLC in [0.002, 1]
x_mMTC  in [0, 1]
```

The formal proof applies to the frozen neural-network decision policy.

Detailed information is available in:

```text
docs/FORMAL_VERIFICATION.md
```

---

## 30. Formal Verification Boundary

Formal verification does not imply that the complete end-to-end system has
been formally proven.

The assurance structure is:

```text
Formal verification
        |
        +-- Frozen DQN decision property

Runtime validation
        |
        +-- model hash
        +-- KPM state completeness
        +-- measurement freshness
        +-- action-table validation
        +-- Slice-SM installation
        +-- scheduler behavior
        +-- RB instrumentation

Experimental validation
        |
        +-- RFsim traffic
        +-- congestion
        +-- throughput behavior
        +-- work-conserving allocation
```

These assurance layers complement one another but establish different
properties.

---

## 31. Current Validation Boundary

The following stages have been completed:

- DQN training
- frozen-model creation
- ONNX conversion
- model hash validation
- formal safety verification
- native ONNX inference in the xApp
- three-slice KPM telemetry
- Slice-SM control
- OAI policy installation
- work-conserving MAC scheduling
- RFsim integration
- three-slice congestion testing
- RB-level scheduler validation

The next experimental stage is:

```text
USRP / real-RF validation
```

---

## 32. Planned Hardware Validation

The transition from RFsim to USRP should preserve the control architecture:

```text
KPM telemetry
    ->
FlexRIC
    ->
DQN xApp
    ->
ONNX inference
    ->
Slice-SM
    ->
OAI Slice Manager
    ->
work-conserving MAC scheduler
```

The main change will occur below the scheduling/control layer:

```text
RFsim backend
      |
      v
USRP RF backend
```

The DQN model, telemetry semantics, action space, scheduler policy, formal
verification result, and RB instrumentation should remain unchanged unless a
hardware-specific portability problem is discovered.

---

## 33. Repository Architecture

The repository is organized as:

```text
.
├── README.md
├── docs/
│   ├── ARCHITECTURE.md
│   ├── BASELINES.md
│   └── FORMAL_VERIFICATION.md
├── formal-verification/
├── models/
├── xapp/
├── oai/
├── flexric/
├── configs/
├── scripts/
├── patches/
│   ├── oai/
│   └── flexric/
└── evidence/
    ├── formal-verification/
    ├── rb-validation/
    └── hashes/
```

Each directory separates implementation source, reproducibility information,
and experimental evidence.

---

## 34. Architecture Summary

The overall design can be summarized as:

```text
Observe
  |
  v
Per-slice RLC queue telemetry
  |
  v
Construct bounded DQN state
  |
  v
Infer formally verified policy
  |
  v
Validate action
  |
  v
Send O-RAN control
  |
  v
Install slice weights
  |
  v
Create protected scheduling opportunities
  |
  v
Redistribute unused capacity
  |
  v
Borrow remaining residual resources
  |
  v
Measure actual RB behavior
```

The core architectural principle is to combine:

```text
learning-based adaptation
        +
formal policy assurance
        +
runtime validation
        +
work-conserving deterministic enforcement
```

within an O-RAN-compatible control and scheduling pipeline.
