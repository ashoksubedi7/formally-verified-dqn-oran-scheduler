# Formal Verification

This directory contains the formal verification artifacts for the trained
DQN scheduling policy.

Verified model

The verified ONNX model is:

```text
models/formal_dqn_v2_candidate_16000.onnx
```

SHA-256:

```text
699c2d326b8c86f81d86929fcf28e3a00086687e0391c6a1bd4119b92001103e
```

The Python verification script checks this hash before verification.

Safety property

The model input is:

```text
[x_eMBB, x_URLLC, x_mMTC]
```

The verified input domain is:

```text
x_eMBB  in [0.000, 1.000]
x_URLLC in [0.002, 1.000]
x_mMTC  in [0.000, 1.000]
```

The URLLC lower bound of `0.002` corresponds to a raw URLLC load of 1
under the `/500` normalization used by the model.

The verified property is:

```text
URLLC active => selected URLLC allocation weight >= 20
```

The unsafe actions are:

```text
0, 1, 11, 12, 13, 14, 15
```

Each of these actions assigns a URLLC allocation weight below 20.

VNN-LIB properties

The VNN-LIB property files are stored in:

```text
formal-verification/vnnlib/
```

There is one counterexample query for each unsafe action.

Each property:

- constrains the three model inputs to the verified domain;
- treats the selected unsafe action as a possible argmax;
- requires its Q-value to be greater than or equal to every other output.

Therefore:

```text
SAT   = a counterexample exists for that unsafe action
UNSAT = no counterexample exists in the encoded domain
```

Ties are included conservatively because the unsafe output is allowed to be
equal to the other Q-values.

Verification environment

The tested environment uses:

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

Installation

From the repository root:

```bash
python3 -m venv .venv-verification
source .venv-verification/bin/activate

python -m pip install --upgrade pip
python -m pip install -r formal-verification/requirements-verification.txt
```

Check Marabou:

```bash
Marabou --version
```

Python API verification

Run:

```bash
python formal-verification/py5chesim/verify_formal_dqn_v2.py
```

The script:

1. verifies the ONNX model hash;
2. defines the bounded input domain;
3. identifies all unsafe actions;
4. constructs one counterexample query per unsafe action;
5. checks whether the unsafe action can be an argmax;
6. reports SAT, UNSAT, or TIMEOUT.

VNN-LIB verification

Run:

```bash
./formal-verification/run_vnnlib.sh
```

This checks all seven VNN-LIB property files against the verified ONNX model
using Marabou with a 300-second timeout per query.

Expected result:

```text
unsafe action 0  -> UNSAT
unsafe action 1  -> UNSAT
unsafe action 11 -> UNSAT
unsafe action 12 -> UNSAT
unsafe action 13 -> UNSAT
unsafe action 14 -> UNSAT
unsafe action 15 -> UNSAT
```

All seven VNN-LIB queries returned `UNSAT` in the tested environment.

Interpretation

For the verified ONNX network and the specified bounded input domain,
none of the actions with URLLC allocation weight below 20 can satisfy
the encoded argmax condition.

This verifies a property of the neural scheduling policy only.

It does not prove:

- end-to-end URLLC SLA compliance;
- latency or deadline guarantees;
- packet delivery guarantees;
- correctness of downstream PRB allocation;
- correctness of the complete O-RAN system;
- simulator realism;
- correctness of runtime inputs;
- temporal safety or starvation freedom.

Reproducibility artifacts

The main verification artifacts are:

```text
formal-verification/
├── README.md
├── requirements-verification.txt
├── run_vnnlib.sh
├── py5chesim/
│   ├── verify_formal_dqn_v2.py
│   └── vnnlib.py
└── vnnlib/
    ├── unsafe_action_0.vnnlib
    ├── unsafe_action_1.vnnlib
    ├── unsafe_action_11.vnnlib
    ├── unsafe_action_12.vnnlib
    ├── unsafe_action_13.vnnlib
    ├── unsafe_action_14.vnnlib
    └── unsafe_action_15.vnnlib
```

The authoritative ONNX model is stored at:

```text
models/formal_dqn_v2_candidate_16000.onnx
```
