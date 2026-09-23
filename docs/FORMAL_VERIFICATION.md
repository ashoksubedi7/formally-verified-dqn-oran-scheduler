# Formal Verification of the DQN Scheduling Policy

## 1. Purpose

This project uses formal verification to establish a safety property of the
frozen Deep Q-Network (DQN) scheduling policy before deploying the model in
the O-RAN control loop.

The formal analysis applies specifically to the neural-network decision
policy.

It does not constitute a formal proof of the complete OpenAirInterface,
FlexRIC, E2, MAC scheduler, or radio system. Those components are evaluated
separately through runtime and experimental validation.

## 2. Verified Model

The formally verified and deployed model is:

```text
formal_dqn_v2_candidate_16000.onnx
```

The SHA-256 hash of the verified ONNX model is:

```text
699c2d326b8c86f81d86929fcf28e3a00086687e0391c6a1bd4119b92001103e
```

The verification script computes the model hash before verification and
compares it against the expected value.

If the hash differs, verification is aborted.

This ensures that the formal result is associated with the exact frozen
model artifact that was verified.

## 3. Model Interface

The DQN receives three normalized inputs representing the queue load of the
three network slices:

```text
x0 = eMBB
x1 = URLLC
x2 = mMTC
```

The model produces 22 output Q-values:

```text
Q0, Q1, ..., Q21
```

The selected action is determined by the largest Q-value:

```text
a* = argmax(Q0, Q1, ..., Q21)
```

Each output corresponds to one predefined resource-weight vector.

## 4. Action Space

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

## 5. Safety Requirement

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

In words:

> Whenever URLLC has at least one queued packet in the verified state domain,
> the selected DQN action must assign URLLC a weight of at least 20.

## 6. Normalized Verification Domain

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

Therefore `x_URLLC = 0.002` represents the smallest non-zero URLLC queue
value in the verified domain.

## 7. Unsafe Actions

An action is unsafe with respect to the verified property when its URLLC
weight is less than 20.

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

Formal verification therefore checks whether any of these seven actions can
be selected anywhere in the URLLC-active input domain.

## 8. Counterexample Search

For each unsafe action `u`, Marabou searches for an admissible input state
for which that unsafe action can be an argmax.

The condition is:

```text
Q_u >= Q_j
```

for every other output:

```text
j != u
```

The verification script encodes this using:

```text
Q_j - Q_u <= 0
```

for every `j != u`.

If such a state exists, the query is satisfiable and represents a
counterexample to the safety property.

If no such state exists, the query is UNSAT.

## 9. Conservative Treatment of Ties

The verification query uses:

```text
Q_u >= Q_j
```

rather than:

```text
Q_u > Q_j
```

This is important because a state where an unsafe action merely ties for
the maximum Q-value is also treated as potentially unsafe.

Therefore the formal query conservatively includes ties.

An UNSAT result means that an unsafe action cannot even tie for the maximum
Q-value anywhere in the verified domain.

## 10. Marabou Verification Procedure

For each unsafe action, the verification script performs the following
steps:

1. Load the frozen ONNX model.
2. Verify the model SHA-256 hash.
3. Apply the bounded three-dimensional input domain.
4. Add constraints requiring the unsafe action to be an argmax.
5. Invoke Marabou.
6. Classify the result as SAT, UNSAT, or TIMEOUT.

The verifier uses a timeout of 300 seconds for each unsafe-action query.

The interpretation is:

```text
SAT
    Counterexample exists.

UNSAT
    Unsafe action cannot be maximal.

TIMEOUT
    The property is not established for that action.
```

The global safety property is considered verified only if every unsafe
action returns UNSAT.

## 11. Verification Results

The recorded results were:

```text
Action  0 [0,   0,   0] -> UNSAT
Action  1 [0,   0, 100] -> UNSAT
Action 11 [100, 0,   0] -> UNSAT
Action 12 [80,  0,  20] -> UNSAT
Action 13 [60,  0,  40] -> UNSAT
Action 14 [40,  0,  60] -> UNSAT
Action 15 [20,  0,  80] -> UNSAT
```

All seven unsafe-action queries returned UNSAT.

The final verification result is therefore:

```text
FORMAL RESULT: PROPERTY VERIFIED
```

No action having a URLLC allocation weight below 20 can be an argmax
anywhere in the bounded URLLC-active input domain.

## 12. What the Formal Proof Establishes

For the exact frozen ONNX model identified by the recorded SHA-256 hash,
the verification establishes:

```text
For all inputs satisfying:

x_eMBB  in [0, 1]
x_URLLC in [0.002, 1]
x_mMTC  in [0, 1]

no action whose URLLC weight is less than 20 can be an argmax.
```

This is a universal property over the bounded continuous verification
domain.

It is therefore stronger than testing only a finite number of sampled
states.

## 13. Training Safety Penalty

The DQN training code also contains a safety penalty.

The penalty is applied when:

```text
URLLC load >= 1
and
URLLC action weight < 20
```

The safety penalty encourages the DQN to learn behavior consistent with the
desired property.

However, training does not itself prove that the learned neural network
satisfies the requirement for every possible state.

The formal verification stage independently analyzes the frozen neural
network and searches for counterexamples across the complete bounded input
domain.

## 14. Artifact Integrity

The project records hashes for the major verification artifacts.

### Verified ONNX model

```text
699c2d326b8c86f81d86929fcf28e3a00086687e0391c6a1bd4119b92001103e
```

### Verification script

```text
9adc69969f5ccfc19a1d89f0fc6f6df84c7cb7e3927c74ef76eb88232a75ce4a
```

### Final verification log

```text
5bc8ec1d265803656b8cb673f56db44d0613e1cae40bbc84d5e29ef04cb4c863
```

These hashes associate the verification result with exact artifacts rather
than only filenames.

## 15. Recorded Verification Environment

The recorded verification environment is:

| Component | Version |
|---|---|
| Python | 3.10.12 |
| TensorFlow | 2.12.0 |
| ONNX | 1.17.0 |
| ONNX Runtime | 1.23.2 |
| Marabou | v2.0.0 |

## 16. Formal Verification Boundary

The formal proof applies to the following combination:

```text
Frozen ONNX DQN
        +
bounded normalized state space
        +
22-action scheduling table
        +
URLLC minimum-weight safety property
```

The proof does **not** formally establish correctness of the complete
O-RAN system.

In particular, it does not formally prove:

- KPM telemetry delivery
- E2AP transport
- FlexRIC execution
- Slice-SM serialization
- OAI control-message processing
- MAC scheduler implementation
- work-conserving redistribution
- HARQ behavior
- timing behavior
- physical-layer behavior
- throughput guarantees
- latency guarantees
- packet-loss guarantees
- real-RF behavior

These properties are addressed separately through implementation checks,
runtime safeguards, scheduler instrumentation, and experimental validation.

## 17. Runtime Assurance

The deployed system adds runtime assurance mechanisms around the formally
verified neural network.

These include:

- ONNX model SHA-256 verification
- complete three-slice state checking
- KPM measurement freshness checking
- exact DQN action-vector validation
- URLLC safety checking
- controlled Slice-SM policy installation

The OAI slice manager also rejects weight vectors that are not members of
the predefined DQN action space.

The project therefore uses two complementary assurance layers:

```text
Formal assurance
    |
    +-- verifies a property of the frozen neural-network policy

Runtime assurance
    |
    +-- validates model identity, telemetry state, actions,
        control installation, and scheduler behavior
```

## 18. Verification Evidence

Formal-verification evidence is stored under:

```text
evidence/formal-verification/
```

The main evidence files are:

```text
formal_dqn_v2_final_verification.txt
marabou_formal_dqn_v2_results.txt
FORMAL_VERIFICATION_MANIFEST.txt
FORMAL_VERIFICATION_SHA256.txt
```

The exact verified ONNX model is stored at:

```text
models/formal_dqn_v2_candidate_16000.onnx
```

The original verification source is retained under:

```text
formal-verification/py5chesim/
```
