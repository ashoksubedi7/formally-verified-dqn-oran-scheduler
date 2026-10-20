from pathlib import Path

UNSAFE_ACTIONS = [0, 1, 11, 12, 13, 14, 15]

SCRIPT_DIR = Path(__file__).resolve().parent
OUTPUT_DIR = SCRIPT_DIR.parent / "vnnlib"
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)


def make_property(unsafe_action):
    lines = []

    lines.append(
        f"; Counterexample query for unsafe scheduling action "
        f"{unsafe_action}"
    )
    lines.append(";")
    lines.append("; X_0 = normalized eMBB load")
    lines.append("; X_1 = normalized URLLC load")
    lines.append("; X_2 = normalized mMTC load")
    lines.append("; Y_i = Q-value for action i")
    lines.append("")

    for i in range(3):
        lines.append(f"(declare-const X_{i} Real)")

    lines.append("")

    for i in range(22):
        lines.append(f"(declare-const Y_{i} Real)")

    lines.append("")
    lines.append("; Input domain")
    lines.append("(assert (>= X_0 0.0))")
    lines.append("(assert (<= X_0 1.0))")
    lines.append("(assert (>= X_1 0.002))")
    lines.append("(assert (<= X_1 1.0))")
    lines.append("(assert (>= X_2 0.0))")
    lines.append("(assert (<= X_2 1.0))")

    lines.append("")
    lines.append(
        f"; Unsafe condition: action {unsafe_action} can be an argmax"
    )

    for j in range(22):
        if j == unsafe_action:
            continue

        lines.append(
            f"(assert (>= Y_{unsafe_action} Y_{j}))"
        )

    return "\n".join(lines) + "\n"


for action in UNSAFE_ACTIONS:
    path = OUTPUT_DIR / f"unsafe_action_{action}.vnnlib"

    path.write_text(
        make_property(action),
        encoding="utf-8",
    )

    print(f"Created {path}")
