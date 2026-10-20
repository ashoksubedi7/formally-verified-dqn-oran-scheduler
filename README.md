# Formally Verified DQN-Based O-RAN Scheduler

Research prototype integrating a Deep Q-Network (DQN) scheduling policy,
formal neural-network verification, FlexRIC, and OpenAirInterface for
three-slice O-RAN resource scheduling.

The system separates learning-based policy selection from deterministic
scheduler enforcement.

1. Project Overview

The DQN observes the relative queue state of three network slices:

```text
eMBB
URLLC
mMTC
```

and selects one of 22 predefined scheduling-weight actions.

The control path is:

```text
RLC queue state
    ->
E2SM-KPM
    ->
FlexRIC near-RT RIC
    ->
DQN xApp
    ->
ONNX inference
    ->
slice-weight action
    ->
Slice-SM
    ->
OAI slice manager
    ->
NR MAC scheduler
    ->
RB allocation
```

The DQN selects scheduling weights.

It does not directly allocate individual physical resource blocks.

2. DQN Model

The frozen deployment model is:

```text
models/formal_dqn_v2_candidate_16000.onnx
```

SHA-256:

```text
699c2d326b8c86f81d86929fcf28e3a00086687e0391c6a1bd4119b92001103e
```

The model receives:

```text
[x_eMBB, x_URLLC, x_mMTC]
```

and produces:

```text
22 Q-values
```

The selected action is:

```text
argmax(Q-values)
```

Each action corresponds to:

```text
[eMBB weight, URLLC weight, mMTC weight]
```

3. Verified Safety Property

The formally verified property is:

```text
URLLC active
    =>
selected URLLC allocation weight >= 20
```

The verified normalized input domain is:

```text
x_eMBB  in [0.000, 1.000]
x_URLLC in [0.002, 1.000]
x_mMTC  in [0.000, 1.000]
```

The value:

```text
x_URLLC = 0.002
```

corresponds to the smallest active raw URLLC queue state:

```text
1 / 500 = 0.002
```

The unsafe actions are:

```text
0, 1, 11, 12, 13, 14, 15
```

because each assigns URLLC a weight below 20.

4. Formal Verification

Formal verification is performed with Marabou.

The tested environment uses:

```text
maraboupy==2.0.0
numpy==2.2.6
onnx==1.23.2
onnxruntime==1.23.2
```

Marabou reports:

```text
Marabou version 2.0.0 [HEAD 215828c]
```

Two verification paths are preserved:

```text
Python Marabou API
```

and:

```text
ONNX + VNN-LIB + Marabou CLI
```

All seven unsafe-action queries returned:

```text
UNSAT
```

Therefore, within the verified bounded input domain, none of the actions
whose URLLC weight is below 20 can satisfy the encoded argmax condition.

Detailed verification documentation is available in
[Formal Verification](docs/FORMAL_VERIFICATION.md).

5. VNN-LIB Verification

The VNN-LIB properties are stored under:

```text
formal-verification/vnnlib/
```

They can be regenerated using:

```bash
python formal-verification/py5chesim/vnnlib.py
```

Run all seven verification queries using:

```bash
./formal-verification/run_vnnlib.sh
```

The complete recorded VNN-LIB output is stored at:

```text
evidence/formal-verification/marabou_vnnlib_results.txt
```

6. Reproduce the Verification Environment

From the repository root:

```bash
python3 -m venv .venv-verification
source .venv-verification/bin/activate

python -m pip install --upgrade pip
python -m pip install -r formal-verification/requirements-verification.txt
```

Then check:

```bash
Marabou --version
```

Run the Python API verifier:

```bash
python formal-verification/py5chesim/verify_formal_dqn_v2.py
```

Run the VNN-LIB verifier:

```bash
./formal-verification/run_vnnlib.sh
```

7. Network Slices

The current three-slice configuration is:

| Slice | SST | SD |
|---|---:|---:|
| eMBB | 1 | 000001 |
| URLLC | 2 | 000002 |
| mMTC | 3 | 000003 |

The ordering:

```text
eMBB
URLLC
mMTC
```

must remain consistent across:

```text
DQN state
DQN action table
KPM telemetry
Slice-SM control
OAI slice manager
MAC scheduling
```

8. Current Implementation

The repository contains:

- Py5cheSim DQN training source;
- frozen ONNX model;
- Marabou verification code;
- VNN-LIB properties;
- FlexRIC DQN xApp integration;
- E2SM-KPM per-slice telemetry;
- Slice-SM control;
- OAI slice manager;
- NSSAI-based slice classification;
- work-conserving NR MAC scheduling;
- RB-level runtime validation.

9. Current Status

Completed:

- DQN training prototype;
- ONNX model export;
- model SHA-256 identification;
- Python API formal verification;
- VNN-LIB formal verification;
- FlexRIC/OAI integration;
- RFsim testing;
- three-slice congestion testing;
- RB-level scheduler validation.

Next stage:

```text
USRP / real-RF validation
```

10. Model Card

The [Model Card](cards/MODEL_CARD.md) documents the frozen DQN model,
its intended use, inputs, outputs, training procedure, evaluation,
risks, limitations, and deployment requirements.

11. Data Card

The [Data Card](cards/DATA_CARD.md) documents the synthetic Py5cheSim
training data, state representation, reward information, limitations,
biases, privacy considerations, and simulation-to-reality concerns.

12. Documentation

Detailed documentation is stored under:

```text
docs/
```

Important files include:

```text
docs/ARCHITECTURE.md
docs/PY5CHESIM_LEARNING_MODEL.md
docs/FORMAL_VERIFICATION.md
docs/XAPP_INTEGRATION.md
docs/RB_VALIDATION.md
docs/REPRODUCTION.md
docs/BASELINES.md
```

13. Repository Structure

```text
cards/
configs/
docs/
evidence/
flexric/
formal-verification/
models/
oai/
patches/
scripts/
xapp/
```

Main purposes:

```text
cards/
    Model and data documentation

configs/
    Experiment configurations

docs/
    Detailed project documentation

evidence/
    Verification and runtime evidence

flexric/
    FlexRIC integration material

formal-verification/
    Verification source, dependencies, and VNN-LIB properties

models/
    Frozen ONNX model

oai/
    OpenAirInterface integration material

patches/
    Changes relative to upstream OAI/FlexRIC

scripts/
    Reproduction and helper scripts

xapp/
    DQN xApp implementation
```

14. Formal Verification Boundary

The formal result applies to:

```text
the frozen ONNX neural policy
+
the bounded three-input verification domain
+
the 22-action scheduling table
+
the URLLC minimum-weight property
```

It does not prove:

- end-to-end URLLC SLA compliance;
- latency guarantees;
- deadline guarantees;
- packet-delivery guarantees;
- packet-loss guarantees;
- throughput guarantees;
- starvation freedom;
- correctness of KPM telemetry;
- correctness of E2 transport;
- correctness of final PRB allocation;
- correctness of the complete O-RAN system;
- real-RF performance.

15. Reproduction Guide

The broader OAI/FlexRIC reproduction procedure is documented at:

```text
docs/REPRODUCTION.md
```

The repository preserves the model, project-specific source changes,
verification artifacts, and runtime evidence needed to understand and
reproduce the research prototype.

16. Verification Artifacts

The main formal-verification artifacts are:

```text
models/formal_dqn_v2_candidate_16000.onnx

formal-verification/py5chesim/verify_formal_dqn_v2.py
formal-verification/py5chesim/vnnlib.py
formal-verification/requirements-verification.txt
formal-verification/run_vnnlib.sh

formal-verification/vnnlib/unsafe_action_0.vnnlib
formal-verification/vnnlib/unsafe_action_1.vnnlib
formal-verification/vnnlib/unsafe_action_11.vnnlib
formal-verification/vnnlib/unsafe_action_12.vnnlib
formal-verification/vnnlib/unsafe_action_13.vnnlib
formal-verification/vnnlib/unsafe_action_14.vnnlib
formal-verification/vnnlib/unsafe_action_15.vnnlib

evidence/formal-verification/marabou_vnnlib_results.txt
```

These artifacts provide:

```text
model
+
property
+
verification tool
+
complete solver output
+
interpretation
+
reproduction instructions
```
