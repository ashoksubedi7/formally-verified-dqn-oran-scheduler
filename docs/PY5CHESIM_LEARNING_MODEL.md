# Py5cheSim DQN Learning Model

## 1. Purpose

This document describes the Deep Q-Network (DQN) learning model used in
the Py5cheSim-based training environment for the formally verified O-RAN
resource-scheduling project.

The learning model is responsible for learning a mapping from a
three-slice network state to one of 22 predefined resource-weight actions.

The main training implementation is:

```text
formal-verification/py5chesim/formal_vanilla_v2.py
```

The associated simulation configuration retained by this repository is:

```text
formal-verification/py5chesim/simulation_formal.py
```

The resulting frozen model was later exported to ONNX, formally verified,
and integrated into the FlexRIC xApp.

---

## 2. Role of Py5cheSim

Py5cheSim provides the learning and simulation environment used before
deployment into OpenAirInterface.

The overall development flow is:

```text
Py5cheSim simulation
        |
        v
DQN training
        |
        v
candidate trained models
        |
        v
selected frozen model
        |
        v
ONNX export
        |
        v
formal verification
        |
        v
FlexRIC xApp deployment
        |
        v
OAI MAC scheduling
```

Py5cheSim therefore belongs to the **offline learning stage** of the
project.

It is separate from the runtime OAI/FlexRIC execution environment.

---

## 3. Network Slices

The formal-v2 learning model operates on three slices:

```text
eMBB
URLLC
mMTC
```

The state and action ordering used throughout the DQN is:

```text
index 0 -> eMBB
index 1 -> URLLC
index 2 -> mMTC
```

This ordering is important because the same three-element convention is
later preserved in the deployed ONNX/xApp system.

---

## 4. DQN State

The learning model maintains a three-element state:

```text
[eMBB state, URLLC state, mMTC state]
```

Internally the source maintains variables such as:

```text
current_state
new_state
```

with the state initialized as:

```text
[0, 0, 0]
```

The training code derives each new slice-state component from the
corresponding Py5cheSim scheduler state using:

```text
min(
    slice.schedulerDL.updSumPcks() // div,
    500
)
```

Therefore every raw DQN state component is capped at:

```text
500
```

The exact simulator meaning of `updSumPcks()` is defined by the Py5cheSim
scheduler implementation and should be treated as the authoritative
training-state source.

---

## 5. State Normalization

Before a state is passed to the neural network, the formal-v2 code
normalizes it by:

```text
normalized_state =
    raw_state / 500.0
```

The implementation is equivalent to:

```python
np.array(state, dtype=np.float32) / 500.0
```

Therefore the expected normalized state range is:

```text
0.0 <= x_i <= 1.0
```

for each slice.

The normalized vector is:

```text
[
    x_eMBB,
    x_URLLC,
    x_mMTC
]
```

This normalization is also used when replay-memory states are passed to
the DQN during training.

---

## 6. Relationship Between Training and Deployment State

The frozen model expects the same three-dimensional normalized input
structure at deployment:

```text
[eMBB, URLLC, mMTC]
```

The OAI/FlexRIC deployment therefore reproduces the normalization:

```text
clamp(value, 0, 500) / 500
```

before invoking the ONNX model.

This preserves the numerical input range expected by the trained DQN.

The training simulator and OAI deployment obtain their state information
from different environments, so their measurement semantics should not be
treated as identical merely because they share the same normalized
three-element representation.

---

## 7. Action Space

The DQN has:

```text
22 actions
```

Each action represents one predefined resource-weight vector:

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

The action-space size in the DQN implementation is therefore:

```text
ACTION_SPACE_SIZE = 22
```

---

## 8. Action Selection

During inference, the DQN calculates one Q-value for every action.

The selected action is:

```text
action = argmax(Q-values)
```

Conceptually:

```text
normalized state
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
       |
       v
slice weight vector
```

For example:

```text
action 7
    ->
[20, 80, 0]
```

means that the DQN selected a scheduling policy with relative weights for
eMBB, URLLC, and mMTC.

---

## 9. Training Mode and Inference Mode

The formal-v2 implementation supports both learning behavior and
model-only inference behavior.

During learning:

```text
state
    ->
action
    ->
simulated resource allocation
    ->
reward
    ->
new state
    ->
replay memory
    ->
DQN update
```

When learning is disabled, the trained model is used only to predict
Q-values and choose the action with the largest value.

This separation allows the same scheduler logic to be used for both:

```text
training
```

and:

```text
evaluation / inference
```

---

## 10. Experience Replay

The DQN uses experience replay.

Each stored transition contains the standard components:

```text
(
    current_state,
    action,
    reward,
    new_state,
    done
)
```

Transitions are appended to:

```text
replay_memory
```

The replay-memory capacity in the formal-v2 implementation is:

```text
REPLAY_MEMORY_SIZE = 10000
```

Training does not begin until the replay buffer contains at least:

```text
MIN_REPLAY_MEMORY_SIZE = 500
```

transitions.

---

## 11. Replay-State Normalization

States stored in replay memory are converted to floating-point arrays and
normalized before being passed to the network.

For the current state:

```text
current_state / 500.0
```

For the next state:

```text
new_state / 500.0
```

This is important because training and inference must operate on the same
numerical input representation.

Therefore:

```text
training input normalization
        =
inference input normalization
```

with respect to the `/500` scaling.

---

## 12. Main Network and Target Network

The implementation follows the DQN pattern of maintaining:

```text
main network
```

and:

```text
target network
```

The main network is updated during learning.

The target network is used when estimating future-state Q-values.

Initially:

```text
target_network weights
    =
main_network weights
```

The target-network update interval in the formal-v2 source is:

```text
UPDATE_TARGET_EVERY = 200
```

At the configured update point, the target network receives the current
main-network weights.

---

## 13. DQN Training Update

For a sampled replay transition:

```text
(
    state,
    action,
    reward,
    next_state,
    terminal
)
```

the training loop obtains:

```text
current Q-values
```

from the main model and:

```text
future Q-values
```

from the target model.

The Q-value corresponding to the selected action is then updated using the
transition reward and future-state estimate according to the DQN training
logic implemented in `formal_vanilla_v2.py`.

The updated Q-vector becomes the training target for the current state.

The repository retains the source code rather than attempting to redefine
that update mathematically in documentation, so the implementation remains
the authoritative definition of the training rule.

---

## 14. Reward Design

The formal-v2 reward function is designed to consider more than raw
resource allocation.

The source includes terms related to:

- per-slice service satisfaction,
- radio-resource utilization,
- overall resource efficiency,
- URLLC packet delivery,
- and a URLLC safety penalty.

The code computes service-satisfaction quantities for the slices and
resource-utilization quantities used to form the reward.

It also computes average efficiency from the three slice RB-utilization
values.

---

## 15. URLLC Delivery Component

The training code explicitly tracks URLLC packet delivery behavior.

It computes a URLLC packet-loss ratio over the current decision interval:

```text
PLR_URLLC
```

and bounds that value to:

```text
[0, 1]
```

URLLC delivery is then defined as:

```text
delivery_URLLC = 1 - PLR_URLLC
```

The reward includes a positive URLLC delivery component.

The retained source shows the coefficient:

```text
+ 0.10 * delivery_URLLC
```

This encourages the learned policy to consider URLLC delivery performance.

---

## 16. URLLC Safety Penalty

The formal-v2 training code introduces an explicit safety penalty.

The condition is:

```text
current_state[1] >= 1
and
URLLC weight < 20
```

Because:

```text
state[1] = URLLC
```

this means:

```text
URLLC active
and
selected action gives URLLC less than 20 weight
```

The code sets:

```text
safety_penalty = 1
```

when the condition occurs.

Otherwise:

```text
safety_penalty = 0
```

The reward then includes:

```text
- 2.0 * safety_penalty
```

This strongly discourages actions that assign insufficient scheduling
weight to active URLLC traffic.

---

## 17. Safety Penalty vs. Formal Verification

The safety penalty is part of **learning**.

It encourages the DQN to learn:

```text
URLLC active
    ->
URLLC weight >= 20
```

However, reward shaping alone does not prove that the final network always
satisfies this behavior.

For that reason, the final frozen DQN was analyzed separately using
Marabou.

The assurance process is:

```text
training safety penalty
        |
        v
encourage safe learned behavior
        |
        v
freeze candidate model
        |
        v
formal verification
        |
        v
prove property over bounded domain
```

The formal verification is documented separately in:

```text
docs/FORMAL_VERIFICATION.md
```

---

## 18. Training Duration

The formal-v2 scheduler is configured with:

```text
EPISODES = 20000
```

This defines the configured learning horizon for the training run.

The deployed candidate model is named:

```text
formal_dqn_v2_candidate_16000
```

The filename identifies the selected candidate artifact retained from the
training process.

The filename alone should not be interpreted as proof of training quality;
the selected model is subsequently identified by its exact cryptographic
hash and formal-verification result.

---

## 19. Model Selection and Frozen Artifact

The final deployment artifact is:

```text
formal_dqn_v2_candidate_16000.onnx
```

Its SHA-256 hash is:

```text
699c2d326b8c86f81d86929fcf28e3a00086687e0391c6a1bd4119b92001103e
```

Once this model was selected for verification and deployment, its identity
was frozen using the hash.

The control path therefore becomes:

```text
trained candidates
        |
        v
selected candidate
        |
        v
freeze model identity
        |
        v
ONNX SHA-256
        |
        v
formal verification
        |
        v
runtime SHA-256 verification
```

---

## 20. Model Output

The DQN output layer contains one value per action.

Therefore:

```text
output size = 22
```

The outputs are Q-values rather than probabilities.

Conceptually:

```text
Q(s, a0)
Q(s, a1)
...
Q(s, a21)
```

The action with the maximum Q-value is selected.

No softmax probability interpretation is used for the deployed action
selection.

---

## 21. Resource Allocation in Py5cheSim

After selecting a weight vector, the Py5cheSim learning scheduler converts
the selected weights into simulated resource allocations.

The formal-v2 source contains calculations of the form:

```text
PRBs * slice_weight
-------------------
100 * numRefFactor
```

with integer conversion for the simulated allocation.

This mechanism belongs to the Py5cheSim training environment.

It should not be confused with the later OAI implementation.

The deployed OAI scheduler uses the DQN weights as protected scheduling
weights in a work-conserving scheduler rather than directly reproducing the
Py5cheSim PRB calculation.

---

## 22. Training vs. OAI Enforcement

The two stages have deliberately different responsibilities.

### Py5cheSim

```text
DQN action
    |
    v
simulation weight
    |
    v
simulated PRB allocation
    |
    v
reward
    |
    v
learning
```

### OAI deployment

```text
DQN action
    |
    v
slice weights
    |
    v
protected scheduling budgets
    |
    v
demand capping
    |
    v
redistribution
    |
    v
residual borrowing
```

The learned action semantics are preserved as slice weights, while the OAI
scheduler provides the actual runtime resource-enforcement mechanism.

---

## 23. Formal-V2 Training Parameters

The retained source establishes the following training parameters:

| Parameter | Value |
|---|---:|
| Training episodes | 20,000 |
| Action-space size | 22 |
| Replay-memory capacity | 10,000 |
| Minimum replay samples before training | 500 |
| Target-network update interval | 200 |
| Model-saving minimum reward variable | -200 |
| Memory-fraction configuration | 0.20 |
| State normalization divisor | 500 |

Other hyperparameters should be taken directly from the retained source
file when reproducing training rather than inferred from earlier Py5cheSim
variants.

The authoritative file is:

```text
formal-verification/py5chesim/formal_vanilla_v2.py
```

---

## 24. Important Source-Code Note

The Py5cheSim tree contains multiple scheduler and earlier experimental
implementations.

Examples include:

```text
formal_vanilla.py
formal_vanilla_v2.py
vanilla.py
apex.py
```

This project uses:

```text
formal_vanilla_v2.py
```

as the training source associated with the verified DQN v2 artifact.

Values from older files should not automatically be assumed to apply to
the formal-v2 model.

This is especially important when documenting:

- neural-network hyperparameters,
- reward coefficients,
- replay settings,
- training duration,
- and model-selection behavior.

---

## 25. Preserved Training Source

The GitHub repository retains the Py5cheSim files needed to understand the
formal-v2 learning path under:

```text
formal-verification/py5chesim/
```

The retained files include:

```text
formal_vanilla_v2.py
simulation_formal.py
InterSliceSch.py
IntraSliceSch.py
Scheds_Inter.py
Scheds_Intra.py
Cell.py
Slice.py
UE.py
Results.py
requirements.txt
LICENSE.md
```

These files preserve the learning/simulation context without vendoring the
entire original working environment.

---

## 26. Training Artifact Integrity

The formal-verification manifest records hashes for several source and
model artifacts.

The recorded training-source hash for:

```text
formal_vanilla_v2.py
```

is:

```text
fad9bdbc758c403be18c9b485fe355340a1f969500e0507e3981a77f62265b8a
```

The recorded hash for:

```text
simulation_profiles/simulation_formal.py
```

is:

```text
9b692dcc1d24fa651a5fa11b691602edad563bf6d004b31f469bf22146324494
```

The recorded hash for:

```text
Cell.py
```

is:

```text
f515e9a2698ebabcc7e5905800568aef312378677dd9eb84351ed1c0f286cca6
```

These hashes connect the verification/deployment artifact to specific
training and simulation source snapshots.

---

## 27. Recorded Software Environment

The verification manifest records the following software versions:

| Component | Version |
|---|---|
| Python | 3.10.12 |
| TensorFlow | 2.12.0 |
| ONNX | 1.17.0 |
| ONNX Runtime | 1.23.2 |
| Marabou | v2.0.0 |

These versions describe the retained verification artifact environment.

They are useful for reproducibility when rebuilding or rechecking the
learning-to-verification pipeline.

---

## 28. Learning and Assurance Pipeline

The complete learning-to-deployment path can be summarized as:

```text
Py5cheSim traffic simulation
        |
        v
3-slice state construction
        |
        v
state cap at 500
        |
        v
normalization by 500
        |
        v
DQN inference
        |
        v
22-action weight selection
        |
        v
simulated resource allocation
        |
        v
reward calculation
        |
        v
experience replay
        |
        v
main-network training
        |
        v
target-network update
        |
        v
candidate model
        |
        v
frozen ONNX
        |
        v
formal verification
        |
        v
FlexRIC/OAI deployment
```

---

## 29. What the Learning Model Establishes

The training process produces a DQN that learns a policy from simulated
Py5cheSim interactions.

Training demonstrates that the model has been optimized according to the
implemented simulation environment and reward function.

Training alone does **not** establish:

- formal safety,
- guaranteed URLLC SLA satisfaction,
- guaranteed throughput,
- guaranteed latency,
- correctness of E2 transport,
- correctness of OAI scheduling,
- or real-RF performance.

Those questions are addressed by separate project stages.

---

## 30. Separation of Evidence

The project intentionally separates three forms of evidence.

### Learning evidence

```text
Py5cheSim training
reward behavior
candidate model generation
```

### Formal evidence

```text
Marabou verification
frozen ONNX identity
bounded safety property
```

### Runtime evidence

```text
FlexRIC inference
E2 control
OAI policy installation
RB-level scheduler validation
```

This prevents simulation performance from being confused with either a
formal proof or a runtime-system guarantee.

---

## 31. Related Documentation

The broader architecture is documented in:

```text
docs/ARCHITECTURE.md
```

The formal proof is documented in:

```text
docs/FORMAL_VERIFICATION.md
```

The OAI MAC scheduler validation is documented in:

```text
docs/RB_VALIDATION.md
```

Together, these documents describe:

```text
learning
    ->
verification
    ->
deployment
    ->
runtime validation
```

---

## 32. Summary

The Py5cheSim learning stage trains a three-input, 22-action DQN for
adaptive resource-weight selection across:

```text
eMBB
URLLC
mMTC
```

The core learning pipeline is:

```text
three-slice simulator state
        |
        v
cap state at 500
        |
        v
normalize by 500
        |
        v
DQN
        |
        v
22 Q-values
        |
        v
argmax action
        |
        v
slice weight vector
        |
        v
simulated resource allocation
        |
        v
multi-objective reward
        |
        v
experience replay
        |
        v
network training
```

The model is trained with an explicit URLLC safety penalty and later
subjected to a separate formal-verification stage.

The final frozen model used by the deployed system is:

```text
formal_dqn_v2_candidate_16000.onnx
```

with SHA-256:

```text
699c2d326b8c86f81d86929fcf28e3a00086687e0391c6a1bd4119b92001103e
```

This establishes a traceable path from simulation-based learning to
formally verified and runtime-validated O-RAN scheduling.
