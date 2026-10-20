#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

MODEL="$ROOT_DIR/models/formal_dqn_v2_candidate_16000.onnx"
PROPERTY_DIR="$ROOT_DIR/formal-verification/vnnlib"

UNSAFE_ACTIONS=(0 1 11 12 13 14 15)

if ! command -v Marabou >/dev/null 2>&1; then
    echo "Error: Marabou is not available on PATH."
    echo
    echo "Create and activate the verification environment:"
    echo "  python3 -m venv .venv-verification"
    echo "  source .venv-verification/bin/activate"
    echo "  pip install -r formal-verification/requirements-verification.txt"
    exit 1
fi

if [[ ! -f "$MODEL" ]]; then
    echo "Error: ONNX model not found:"
    echo "  $MODEL"
    exit 1
fi

echo "Marabou version:"
Marabou --version
echo

for action in "${UNSAFE_ACTIONS[@]}"; do
    PROPERTY="$PROPERTY_DIR/unsafe_action_${action}.vnnlib"

    if [[ ! -f "$PROPERTY" ]]; then
        echo "Error: property file not found:"
        echo "  $PROPERTY"
        exit 1
    fi

    echo "============================================================"
    echo "Checking unsafe action $action"
    echo "============================================================"

    Marabou \
        "$MODEL" \
        "$PROPERTY" \
        --timeout 300

    echo
done
