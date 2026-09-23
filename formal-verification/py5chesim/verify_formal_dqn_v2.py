#!/usr/bin/env python3

import hashlib
import sys
from pathlib import Path

# ------------------------------------------------------------
# Paths
# ------------------------------------------------------------

BASE_DIR = Path(__file__).resolve().parent
MARABOU_DIR = BASE_DIR.parent / "Marabou"
MODEL = BASE_DIR / "models" / "formal_dqn_v2_candidate_16000.onnx"

# Allow importing the locally built Marabou Python API
sys.path.insert(0, str(MARABOU_DIR))

from maraboupy import Marabou


# ------------------------------------------------------------
# VERIFIED MODEL IDENTITY
# ------------------------------------------------------------

EXPECTED_SHA256 = (
    "699c2d326b8c86f81d86929fcf28e3a00086687e0391c6a1bd4119b92001103e"
)


def sha256_file(path):
    h = hashlib.sha256()

    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)

    return h.hexdigest()


actual_hash = sha256_file(MODEL)

print("=" * 72)
print("FORMAL DQN V2 — MARABOU SAFETY VERIFICATION")
print("=" * 72)

print("\nModel:")
print(MODEL)

print("\nExpected SHA-256:")
print(EXPECTED_SHA256)

print("\nActual SHA-256:")
print(actual_hash)

if actual_hash != EXPECTED_SHA256:
    print("\nERROR: MODEL HASH DOES NOT MATCH VERIFIED MODEL.")
    print("Verification aborted.")
    sys.exit(1)

print("\nModel hash verified.")


# ------------------------------------------------------------
# ACTION TABLE
#
# [eMBB weight, URLLC weight, mMTC weight]
# ------------------------------------------------------------

ACTIONS = [
    [0,   0,   0],
    [0,   0, 100],
    [0,  20,  80],
    [0,  40,  60],
    [0,  60,  40],
    [0,  80,  20],
    [0, 100,   0],
    [20, 80,   0],
    [40, 60,   0],
    [60, 40,   0],
    [80, 20,   0],
    [100, 0,   0],
    [80,  0,  20],
    [60,  0,  40],
    [40,  0,  60],
    [20,  0,  80],
    [60, 20,  20],
    [20, 60,  20],
    [20, 20,  60],
    [40, 40,  20],
    [40, 20,  40],
    [20, 40,  40],
]


# ------------------------------------------------------------
# SAFETY PROPERTY
#
# Raw state:
#
#   L_e in [0,500]
#   L_u in [1,500]       URLLC active
#   L_m in [0,500]
#
# Network inputs are normalized by /500:
#
#   x_e in [0,1]
#   x_u in [0.002,1]
#   x_m in [0,1]
#
# Property:
#
#   L_u >= 1  ==>  w_URLLC(a*) >= 20
#
# Therefore unsafe actions are exactly those for which
# URLLC weight < 20.
# ------------------------------------------------------------

UNSAFE_ACTIONS = [
    i for i, allocation in enumerate(ACTIONS)
    if allocation[1] < 20
]

EXPECTED_UNSAFE_ACTIONS = [0, 1, 11, 12, 13, 14, 15]

assert UNSAFE_ACTIONS == EXPECTED_UNSAFE_ACTIONS


print("\nVerification domain:")
print("  x_eMBB  in [0.000, 1.000]")
print("  x_URLLC in [0.002, 1.000]")
print("  x_mMTC  in [0.000, 1.000]")

print("\nSafety property:")
print("  URLLC active  =>  selected URLLC allocation weight >= 20")

print("\nUnsafe actions:")
for action in UNSAFE_ACTIONS:
    print(f"  Action {action:2d}: {ACTIONS[action]}")


# ------------------------------------------------------------
# FORMAL VERIFICATION
# ------------------------------------------------------------

results = {}

for unsafe_action in UNSAFE_ACTIONS:

    network = Marabou.read_onnx(str(MODEL))

    inputs = network.inputVars[0][0]
    outputs = network.outputVars[0][0]

    assert len(inputs) == 3
    assert len(outputs) == 22

    # Normalized admissible input domain
    network.setLowerBound(inputs[0], 0.0)
    network.setUpperBound(inputs[0], 1.0)

    network.setLowerBound(inputs[1], 0.002)
    network.setUpperBound(inputs[1], 1.0)

    network.setLowerBound(inputs[2], 0.0)
    network.setUpperBound(inputs[2], 1.0)

    # --------------------------------------------------------
    # Search for a counterexample where the unsafe action
    # is an argmax:
    #
    #       Q_unsafe >= Q_j     for every j != unsafe
    #
    # Marabou addInequality represents:
    #
    #       sum(coeff_i * variable_i) <= scalar
    #
    # Therefore encode:
    #
    #       Q_j - Q_unsafe <= 0
    #
    # Using >= rather than > conservatively includes ties.
    # --------------------------------------------------------

    for j in range(len(outputs)):

        if j == unsafe_action:
            continue

        network.addInequality(
            [outputs[j], outputs[unsafe_action]],
            [1.0, -1.0],
            0.0,
        )

    options = Marabou.createOptions(
        timeoutInSeconds=300,
        verbosity=0,
    )

    exit_code, vals, stats = network.solve(
        options=options,
        verbose=False,
    )

    if stats.hasTimedOut():
        result = "TIMEOUT"

    elif len(vals) == 0:
        result = "UNSAT"

    else:
        result = "SAT"

    results[unsafe_action] = result

    print("\n" + "-" * 72)
    print(
        f"Action {unsafe_action:2d} "
        f"{ACTIONS[unsafe_action]} -> {result}"
    )

    if result == "SAT":

        normalized_state = [
            vals[inputs[0]],
            vals[inputs[1]],
            vals[inputs[2]],
        ]

        raw_state = [
            value * 500.0
            for value in normalized_state
        ]

        print("COUNTEREXAMPLE FOUND")
        print("  normalized state :", normalized_state)
        print("  raw-equivalent   :", raw_state)
        print(
            "  unsafe Q-value   :",
            vals[outputs[unsafe_action]],
        )


# ------------------------------------------------------------
# FINAL RESULT
# ------------------------------------------------------------

print("\n" + "=" * 72)
print("FINAL SUMMARY")
print("=" * 72)

for action in UNSAFE_ACTIONS:
    print(
        f"Action {action:2d} "
        f"{ACTIONS[action]} -> {results[action]}"
    )

all_unsat = all(
    results[action] == "UNSAT"
    for action in UNSAFE_ACTIONS
)

print()

if all_unsat:

    print("FORMAL RESULT: PROPERTY VERIFIED")
    print()
    print(
        "No action with URLLC allocation weight < 20 "
        "can be an argmax anywhere in the bounded "
        "URLLC-active input domain."
    )

    sys.exit(0)

else:

    print("FORMAL RESULT: PROPERTY NOT ESTABLISHED")
    print()
    print(
        "At least one unsafe-action query returned "
        "SAT or TIMEOUT."
    )

    sys.exit(1)
