# Formal Verification of the DQN Scheduling Policy

1. Purpose

This project uses formal verification to establish a safety property of the
frozen Deep Q-Network (DQN) scheduling policy before deploying the model in
the O-RAN control loop.

The formal analysis applies specifically to the neural-network decision
policy.

It does not constitute a formal proof of the complete OpenAirInterface,
FlexRIC, E2, MAC scheduler, or radio system.

Those components are evaluated separately through runtime and experimental
validation.

2. Verified Model

The formally verified and deployed model is:

```text
models/formal_dqn_v2_candidate_16000.onnx
```

The SHA-256 hash of the verified ONNX model is:

```text
699c2d326b8c86f81d86929fcf28e3a00086687e0391c6a1bd4119b92001103e
```

The Python verification script computes the model hash before verification
and compares it against the expected value.

If the hash differs, verification is aborted.

This associates the formal result with the exact frozen model artifact that
was verified.

3. Model Interface

The DQN receives three normalized inputs:

```text
x0 = eMBB
x1 = URLLC
x2 = mMTC
```

The model produces 22 output Q-values:

```text
Q0, Q1, ..., Q21
```

The selected action is determined by:

```text
a* = argmax(Q0, Q1, ..., Q21)
```

Each output corresponds to one predefined resource-weight vector.

4. Action Space

Each action has the form:

```text
[eMBB weight, URLLC weight, mMTC weight]
```

The complete action space is:

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

5. Safety Requirement

The safety requirement concerns active URLLC traffic.

The raw queue-state domain is:

```text
L_eMBB  in [0, 500]
L_URLLC in [1, 500]
L_mMTC  in [0, 500]
```

URLLC is considered active when:

```text
L_URLLC >= 1
```

The required safety property is:

```text
L_URLLC >= 1
    =>
w_URLLC(a*) >= 20
```

where `a*` is the action selected by the DQN.

In plain language:

Whenever URLLC has at least one queued packet in the verified state domain,
the selected DQN action must assign URLLC a scheduling weight of at least 20.

The value 20 is a scheduling weight.

It is not a claim that URLLC receives exactly 20 PRBs or exactly 20 percent
of the available radio resources.

6. Normalized Verification Domain

The DQN operates on normalized queue values.

The raw state is divided by 500 before being passed to the network.

Therefore the verification domain is:

```text
x_eMBB  in [0.000, 1.000]
x_URLLC in [0.002, 1.000]
x_mMTC  in [0.000, 1.000]
```

The minimum active URLLC value is:

```text
1 / 500 = 0.002
```

Therefore:

```text
x_URLLC = 0.002
```

represents the smallest non-zero URLLC queue value in the verified domain.

7. Unsafe Actions

An action is unsafe with respect to the verified property when its URLLC
weight is below 20.

The unsafe actions are:

```text
Action 0  -> [0,   0,   0]
Action 1  -> [0,   0, 100]
Action 11 -> [100, 0,   0]
Action 12 -> [80,  0,  20]
Action 13 -> [60,  0,  40]
Action 14 -> [40,  0,  60]
Action 15 -> [20,  0,  80]
```

The unsafe-action set is:

```text
U = {0, 1, 11, 12, 13, 14, 15}
```

8. Formal Property

Let:

```text
f : R^3 -> R^22
```

be the frozen DQN, with:

```text
x = (x_eMBB, x_URLLC, x_mMTC)
```

and outputs:

```text
f(x) = (Q0(x), Q1(x), ..., Q21(x))
```

The selected action is:

```text
a*(x) = argmax_i Qi(x)
```

The verified domain is:

```text
X = [0,1] x [0.002,1] x [0,1]
```

The safety property is:

```text
For every x in X:

a*(x) is not in U
```

Equivalently:

```text
For every x in X:

w_URLLC(a*(x)) >= 20
```

9. Counterexample Query

For every unsafe action `u`, Marabou searches for an input in the verified
domain where that unsafe action can be an argmax.

The counterexample condition is:

```text
Q_u(x) >= Q_j(x)
```

for every:

```text
j != u
```

Therefore the query for unsafe action `u` is:

```text
exists x in X such that

Q_u(x) >= Q_j(x)

for all j != u
```

If such a state exists, the query is SAT and represents a counterexample to
the property.

If no such state exists, the query is UNSAT.

10. Conservative Treatment of Ties

The formal query uses:

```text
Q_u >= Q_j
```

rather than:

```text
Q_u > Q_j
```

This means a state where an unsafe action ties for the maximum Q-value is
also treated as potentially unsafe.

The verification is therefore conservative with respect to ties.

An UNSAT result means the unsafe action cannot even tie for the maximum
Q-value anywhere in the verified domain.

11. Verification Tools

The current reproducible verification environment uses:

```text
maraboupy==2.0.0
numpy==2.2.6
onnx==1.23.2
onnxruntime==1.23.2
```

The Marabou executable reports:

```text
Marabou version 2.0.0 [HEAD 215828c]
```

The tested Python environment is:

```text
Python 3.10
```

12. Installation

From the repository root:

```bash
python3 -m venv .venv-verification
source .venv-verification/bin/activate
```

Install the verification dependencies:

```bash
python -m pip install --upgrade pip
python -m pip install -r formal-verification/requirements-verification.txt
```

Check Marabou:

```bash
Marabou --version
```

13. Python API Verification

The Python verifier is:

```text
formal-verification/py5chesim/verify_formal_dqn_v2.py
```

Run:

```bash
python formal-verification/py5chesim/verify_formal_dqn_v2.py
```

The script:

1. identifies the authoritative ONNX model;
2. computes its SHA-256 hash;
3. compares the hash against the expected model identity;
4. defines the bounded input domain;
5. identifies all unsafe actions;
6. constructs one counterexample query per unsafe action;
7. invokes Marabou;
8. reports SAT, UNSAT, or TIMEOUT.

A timeout of:

```text
300 seconds
```

is used per unsafe-action query.

14. VNN-LIB Verification

The assignment-compatible VNN-LIB properties are stored in:

```text
formal-verification/vnnlib/
```

The property files are:

```text
unsafe_action_0.vnnlib
unsafe_action_1.vnnlib
unsafe_action_11.vnnlib
unsafe_action_12.vnnlib
unsafe_action_13.vnnlib
unsafe_action_14.vnnlib
unsafe_action_15.vnnlib
```

Each file contains:

- declarations for 3 network inputs;
- declarations for 22 network outputs;
- the bounded input domain;
- the maximizing-output condition for one unsafe action.

Example structure:

```text
(assert (>= X_0 0.0))
(assert (<= X_0 1.0))

(assert (>= X_1 0.002))
(assert (<= X_1 1.0))

(assert (>= X_2 0.0))
(assert (<= X_2 1.0))
```

For unsafe action 0, the property additionally requires:

```text
Y_0 >= Y_1
Y_0 >= Y_2
...
Y_0 >= Y_21
```

The corresponding VNN-LIB files are generated by:

```text
formal-verification/py5chesim/vnnlib.py
```

15. Run All VNN-LIB Queries

Run:

```bash
./formal-verification/run_vnnlib.sh
```

The script checks all seven VNN-LIB files against:

```text
models/formal_dqn_v2_candidate_16000.onnx
```

using:

```text
--timeout 300
```

for each query.

16. VNN-LIB Verification Results

The recorded VNN-LIB results are:

```text
Action  0 -> UNSAT
Action  1 -> UNSAT
Action 11 -> UNSAT
Action 12 -> UNSAT
Action 13 -> UNSAT
Action 14 -> UNSAT
Action 15 -> UNSAT
```

The complete output is preserved at:

```text
evidence/formal-verification/marabou_vnnlib_results.txt
```

All seven unsafe-action VNN-LIB queries returned UNSAT.

17. Python API Verification Results

The Python API verification also returned:

```text
Action  0 [0,   0,   0] -> UNSAT
Action  1 [0,   0, 100] -> UNSAT
Action 11 [100, 0,   0] -> UNSAT
Action 12 [80,  0,  20] -> UNSAT
Action 13 [60,  0,  40] -> UNSAT
Action 14 [40,  0,  60] -> UNSAT
Action 15 [20,  0,  80] -> UNSAT
```

The final result is:

```text
FORMAL RESULT: PROPERTY VERIFIED
```

18. Interpretation

For the exact frozen ONNX model identified by the recorded SHA-256 hash,
and for every input in the bounded domain:

```text
x_eMBB  in [0, 1]
x_URLLC in [0.002, 1]
x_mMTC  in [0, 1]
```

none of the actions whose URLLC weight is below 20 can satisfy the encoded
argmax condition.

The result is universal over the continuous bounded verification domain.

It is stronger than testing only a finite collection of sampled input states.

19. What the Formal Result Establishes

The proof establishes a property of:

```text
Frozen ONNX DQN
        +
bounded normalized state space
        +
22-action scheduling table
        +
URLLC minimum-weight safety property
```

It establishes that the verified neural policy does not select an action
with URLLC weight below 20 when URLLC is active in the verified domain.

20. What the Formal Result Does Not Establish

The formal verification does not prove:

- end-to-end URLLC SLA compliance;
- URLLC latency guarantees;
- deadline-miss guarantees;
- packet-delivery guarantees;
- packet-loss guarantees;
- throughput guarantees;
- fairness guarantees;
- starvation freedom for other slices;
- temporal behavior;
- correctness of runtime input measurements;
- correctness of KPM telemetry;
- correctness of E2AP transport;
- correctness of Slice-SM serialization;
- correctness of OAI control-message processing;
- correctness of the MAC scheduler implementation;
- correctness of final PRB assignments;
- simulator realism;
- HARQ correctness;
- physical-layer correctness;
- complete O-RAN system safety.

21. Training Safety Penalty

The DQN training source also contains a URLLC safety penalty.

The penalty condition is:

```text
URLLC load >= 1
and
selected URLLC weight < 20
```

When this condition is true:

```text
safety_penalty = 1
```

and the reward includes:

```text
-2.0 * safety_penalty
```

This encourages the DQN to learn behavior consistent with the safety
requirement.

However, reward shaping is not a formal proof.

The formal verification independently analyzes the frozen neural network
after training.

22. Training and Verification Separation

The assurance process is:

```text
training safety penalty
        |
        v
encourage desired behavior
        |
        v
freeze candidate model
        |
        v
identify model with SHA-256
        |
        v
formal verification
        |
        v
deploy verified artifact
```

The training mechanism and the formal verification stage therefore have
different roles.

23. Model Identity

The authoritative model is:

```text
models/formal_dqn_v2_candidate_16000.onnx
```

Its SHA-256 hash is:

```text
699c2d326b8c86f81d86929fcf28e3a00086687e0391c6a1bd4119b92001103e
```

The repository intentionally uses this root model artifact as the
authoritative verified model.

The previous nested symbolic-link path under:

```text
formal-verification/py5chesim/models/
```

has been removed to avoid multiple apparent model locations.

24. Reproducibility Artifacts

The main formal-verification artifacts are:

```text
formal-verification/
README.md
requirements-verification.txt
run_vnnlib.sh
py5chesim/verify_formal_dqn_v2.py
py5chesim/vnnlib.py
vnnlib/unsafe_action_0.vnnlib
vnnlib/unsafe_action_1.vnnlib
vnnlib/unsafe_action_11.vnnlib
vnnlib/unsafe_action_12.vnnlib
vnnlib/unsafe_action_13.vnnlib
vnnlib/unsafe_action_14.vnnlib
vnnlib/unsafe_action_15.vnnlib
```

The authoritative verified ONNX artifact is:

```text
models/formal_dqn_v2_candidate_16000.onnx
```

The VNN-LIB verification output is:

```text
evidence/formal-verification/marabou_vnnlib_results.txt
```

25. Formal Verification Evidence

Formal verification evidence is stored under:

```text
evidence/formal-verification/
```

The directory contains the older Python-based verification evidence and the
new VNN-LIB-based result log.

The VNN-LIB result log should be treated as the main assignment evidence for
the ONNX plus VNN-LIB verification path.

26. Model Card

The model card is stored at:

```text
cards/MODEL_CARD.md
```

It documents:

- model purpose;
- inputs;
- outputs;
- training procedure;
- intended uses;
- evaluation;
- risks;
- limitations;
- deployment requirements;
- reproducibility.

27. Data Card

The data card is stored at:

```text
cards/DATA_CARD.md
```

It documents:

- synthetic Py5cheSim training data;
- transition structure;
- data source;
- state representation;
- reward information;
- collection method;
- dataset-size limitation;
- simulation bias;
- coverage limitations;
- simulation-to-reality concerns;
- privacy;
- ethical considerations.

28. Summary

The formal-verification workflow is:

```text
frozen ONNX model
        |
        v
verify SHA-256
        |
        v
define bounded three-input domain
        |
        v
identify unsafe actions
        |
        v
generate counterexample properties
        |
        +----------------------+
        |                      |
        v                      v
Python Marabou API       VNN-LIB properties
        |                      |
        v                      v
unsafe-action queries    Marabou CLI
        |                      |
        +----------+-----------+
                   |
                   v
             all UNSAT
                   |
                   v
         property verified
```

The verified claim should be stated narrowly:

```text
Within the bounded URLLC-active input domain, no action with
URLLC allocation weight below 20 can be an argmax of the
verified frozen DQN.
```

This is a formal property of the neural scheduling policy.

It is not a proof of the complete O-RAN system.
