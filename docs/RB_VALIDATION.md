# RB-Level Runtime Validation

## 1. Purpose

This document describes the RB-level runtime validation of the
DQN-controlled OpenAirInterface downlink scheduler.

The purpose of this validation is to determine whether a DQN-selected slice
weight vector is actually reflected in NR MAC resource scheduling.

The validation focuses specifically on the DQN-controlled Phase-3
new-data scheduling path.

The main questions are:

1. Does the DQN weight vector produce the intended protected scheduling
   opportunity?
2. Does successful protected allocation approximately follow the selected
   slice priority under congestion?
3. Is unused protected capacity redistributed rather than wasted?
4. Can a zero-weight slice use otherwise idle residual capacity?
5. Does the resulting scheduler remain work-conserving?

---

## 2. Validation Boundary

The validation applies to the new-data portion of the OAI downlink
scheduler.

The existing scheduling behavior for operations such as:

- HARQ retransmissions,
- timing advance,
- beam-related control,
- and MAC control elements,

remains outside the DQN-controlled Phase-3 budget.

Therefore the counters documented here should be interpreted as
**Phase-3 new-data RB accounting**, not as accounting for every radio
resource used by the gNB.

---

## 3. DQN Policy Under Test

During the saturated three-slice experiment, the xApp observed queue states
that reached the normalized maximum:

```text
state = [1.000, 1.000, 1.000]
```

The frozen DQN selected:

```text
action = 7
weights = [20, 80, 0]
```

This corresponds to:

```text
eMBB  weight = 20
URLLC weight = 80
mMTC  weight = 0
```

The runtime URLLC safety check also passed:

```text
URLLC_active = YES
w_URLLC      = 80
result       = PASS
```

The validation therefore examines the scheduler behavior produced by the
policy:

```text
[20, 80, 0]
```

---

## 4. Meaning of the Weight Vector

The DQN weights are **not literal PRB allocations**.

For example:

```text
[20, 80, 0]
```

does not mean:

```text
eMBB  always receives exactly 20 PRBs
URLLC always receives exactly 80 PRBs
mMTC  always receives exactly 0 PRBs
```

Instead, the weights determine the initial protected Phase-3 scheduling
opportunity.

Unused capacity may subsequently be redistributed or borrowed.

Therefore:

```text
DQN weight
    !=
fixed physical partition
```

and:

```text
DQN weight
    !=
guaranteed final throughput share
```

---

## 5. Scheduler Validation Instrumentation

The validation build records scheduler activity using log entries of the
form:

```text
[DQN RB VALID]
```

Each measurement window accumulates:

```text
1000
```

DQN-controlled Phase-3 scheduler invocations.

The principal fields are:

| Field | Meaning |
|---|---|
| `calls` | Number of DQN-controlled Phase-3 scheduler invocations in the measurement window |
| `policy_changes` | Number of policy changes observed during the window |
| `last_weight` | Last DQN weight vector observed during the window |
| `demand` | Aggregate requested Phase-3 RB demand for each slice |
| `target` | Initial DQN-derived integer RB target |
| `protected` | Target after being capped by actual slice demand |
| `effective` | Budget after common-pool redistribution |
| `pass1` | Successfully committed RBs during the first allocation pass |
| `protected_used` | Successful first-pass RBs attributable to the protected budget |
| `redistributed_used` | Successful first-pass RBs obtained from redistributed capacity |
| `borrow` | Successful residual RB borrowing |
| `unknown_borrow` | Residual allocation to candidates outside the three recognized slices |
| `phase3_committed` | Total successfully committed Phase-3 RB count in the window |

The slice ordering for vector-valued fields is:

```text
[eMBB, URLLC, mMTC]
```

---

## 6. DQN Target Construction

For the active policy:

```text
[20, 80, 0]
```

the scheduler repeatedly produced the 1000-call target:

```text
target = [21000, 85000, 0]
```

The nominal per-invocation Phase-3 ceiling in this RFsim configuration is:

```text
106 RB
```

Therefore a 1000-call window has a nominal RB-count ceiling of:

```text
106 * 1000 = 106000
```

Because allocation is integer-valued, the weight vector `[20,80,0]`
appears as an integer target of approximately:

```text
[21, 85, 0]
```

per invocation.

Accumulated over 1000 invocations:

```text
[21000, 85000, 0]
```

---

## 7. Fully Saturated Three-Slice Condition

The clearest protected-allocation validation occurs when all three slices
remain fully backlogged.

The recorded demand was:

```text
demand = [106000, 106000, 106000]
```

Because eMBB and URLLC could both consume their entire weighted target:

```text
target    = [21000, 85000, 0]
protected = [21000, 85000, 0]
effective = [21000, 85000, 0]
```

No common-pool redistribution was required in these windows.

---

## 8. Representative Saturated Window

One representative scheduler window reported:

```text
calls=1000
policy_changes=0

last_weight=[
    20,
    80,
    0
]

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

unknown_borrow=0

phase3_committed=102674
```

This single window demonstrates both protected priority and
work-conserving borrowing.

---

## 9. Consecutive Saturated Windows

Nine consecutive fully saturated windows were recorded with the same
DQN policy and the same protected target.

| Window | eMBB protected used | URLLC protected used | mMTC borrow | Phase-3 committed |
|---:|---:|---:|---:|---:|
| 1 | 16086 | 62661 | 23927 | 102674 |
| 2 | 16086 | 62503 | 23290 | 101879 |
| 3 | 16086 | 62661 | 24267 | 103014 |
| 4 | 16044 | 62491 | 24734 | 103269 |
| 5 | 16086 | 62661 | 24097 | 102844 |
| 6 | 16086 | 62582 | 23885 | 102553 |
| 7 | 16086 | 62661 | 24267 | 103014 |
| 8 | 16086 | 62661 | 24267 | 103014 |
| 9 | 16044 | 62491 | 24479 | 103014 |

For all nine windows:

```text
weight    = [20, 80, 0]
demand    = [106000, 106000, 106000]
target    = [21000, 85000, 0]
protected = [21000, 85000, 0]
effective = [21000, 85000, 0]
```

---

## 10. Aggregate Protected Allocation

Across the nine fully saturated windows:

```text
Total eMBB protected_used:
144690

Total URLLC protected_used:
563372
```

Considering successful protected RB allocation between these two
positive-weight slices:

```text
eMBB protected-success share
= 144690 / (144690 + 563372)
= approximately 20.43%
```

and:

```text
URLLC protected-success share
= 563372 / (144690 + 563372)
= approximately 79.57%
```

Therefore the observed successful protected allocation ratio was:

```text
eMBB  ~= 20.43%
URLLC ~= 79.57%
```

This closely follows the DQN-selected priority:

```text
20 : 80
```

---

## 11. Interpretation of the 20/80 Result

The measured:

```text
20.43% / 79.57%
```

ratio applies to successful protected RB accounting between eMBB and
URLLC in the selected saturated validation windows.

It should **not** be interpreted as:

```text
20.43% / 79.57% final application throughput
```

or as:

```text
20.43% / 79.57% exact time-frequency utilization
```

The result demonstrates that the scheduler preserves the intended
relative DQN priority in successful protected Phase-3 allocation.

---

## 12. Why `pass1` Can Be Smaller Than `target`

For example, the target in a saturated window is:

```text
target = [21000, 85000, 0]
```

while successful first-pass allocation may be:

```text
pass1 = [16086, 62661, 0]
```

The difference does not mean that the DQN policy was changed.

Actual physical RB commitment still depends on OAI scheduling feasibility,
including factors such as:

- available contiguous RB regions,
- BWP boundaries,
- VRB occupancy,
- symbol masks,
- time-domain allocation,
- candidate minimum and maximum RB requirements,
- and successful scheduling commitment.

The DQN target therefore represents scheduling opportunity rather than a
guarantee that every target RB will be physically committed.

---

## 13. Zero-Weight mMTC Borrowing

The active DQN policy gives mMTC:

```text
weight = 0
```

and therefore:

```text
target = 0
```

for protected allocation.

However, the scheduler is intentionally work-conserving.

In the representative saturated window:

```text
borrow = [0, 0, 23927]
```

meaning that mMTC received:

```text
23927
```

residual RBs during the 1000-call window.

Across the nine fully saturated windows, mMTC borrowing was:

```text
23927
23290
24267
24734
24097
23885
24267
24267
24479
```

The mean was approximately:

```text
24134.8 RB-counts per 1000 scheduler calls
```

Therefore:

```text
mMTC weight = 0
```

means:

```text
no protected DQN share
```

rather than:

```text
mMTC is permanently prevented from scheduling
```

---

## 14. Work-Conserving Behavior

The zero-weight mMTC result provides direct runtime evidence that the
scheduler does not leave usable residual capacity intentionally idle
solely because a slice has a zero protected weight.

The behavior is:

```text
Protected allocation
        |
        v
Positive-weight slices receive priority
        |
        v
Residual schedulable capacity remains
        |
        v
Backlogged zero-weight slice may borrow
```

This is the intended work-conserving behavior.

---

## 15. Demand-Capped Protected Allocation

A second important validation case occurs when one positive-weight slice
cannot use its full weighted target.

A recorded window showed:

```text
weight=[
    20,
    80,
    0
]

demand=[
    106000,
    62305,
    106000
]

target=[
    21000,
    85000,
    0
]
```

URLLC demand was below its full DQN target.

Its demand-capped protected allocation became:

```text
protected=[
    21000,
    50452,
    0
]
```

The unused capacity did not remain permanently reserved.

---

## 16. Common-Pool Redistribution

For the same window, the scheduler reported:

```text
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
```

The eMBB effective budget increased from:

```text
21000
```

to:

```text
55548
```

because unused protected capacity was returned to the common pool.

The actual first-pass accounting was:

```text
pass1=[
    42172,
    38734,
    0
]

protected_used=[
    16081,
    38734,
    0
]

redistributed_used=[
    26091,
    0,
    0
]
```

This directly shows:

```text
26091
```

successful eMBB RB allocations obtained through redistributed capacity.

---

## 17. Redistribution and Borrowing Can Coexist

The same reduced-URLLC-demand window also reported:

```text
borrow=[
    0,
    0,
    23092
]
```

Therefore two work-conserving mechanisms were visible within the same
measurement window:

```text
unused protected capacity
        |
        v
redistribution to eMBB
```

and:

```text
remaining residual capacity
        |
        v
borrowing by mMTC
```

This demonstrates that the scheduler distinguishes:

1. protected scheduling opportunity,
2. common-pool redistribution,
3. residual borrowing.

---

## 18. Phase-3 Committed RB Count

Across the first nine saturated windows, the mean:

```text
phase3_committed
```

was approximately:

```text
102808.3 RB-counts per 1000 calls
```

The nominal comparison ceiling is:

```text
106000 RB-counts
```

per 1000 calls.

The mean therefore corresponds to approximately:

```text
96.99%
```

of that nominal RB-count ceiling.

This should be interpreted only as an RB-count comparison.

It is **not** an exact measure of radio spectral utilization or
time-frequency occupancy.

---

## 19. Why RB Count Is Not Exact Radio Utilization

A simple RB count does not capture every physical dimension of an NR
allocation.

Different allocations may involve different:

- time-domain allocations,
- symbol lengths,
- modulation and coding schemes,
- retransmission behavior,
- transport-block sizes,
- channel conditions,
- and scheduling constraints.

For this reason:

```text
phase3_committed / 106000
```

must not be presented as an exact spectral-efficiency or
time-frequency-utilization percentage.

The validation uses RB counts specifically to inspect scheduler resource
accounting.

---

## 20. RB Allocation vs. Throughput

RB allocation and application throughput are related but are not
equivalent.

Final throughput depends on additional factors such as:

- MCS,
- channel quality,
- coding rate,
- HARQ retransmissions,
- transport-block size,
- RLC behavior,
- TCP behavior,
- and scheduling timing.

Therefore this validation supports the claim:

```text
The DQN priority is reflected in protected MAC scheduling opportunity.
```

It does not support the stronger claim:

```text
Final throughput must always exactly equal the DQN weight percentages.
```

---

## 21. What the Validation Establishes

The recorded RB-level evidence supports the following implementation
observations.

### Protected priority

With:

```text
weights = [20, 80, 0]
```

successful protected allocation between eMBB and URLLC was approximately:

```text
20.43% / 79.57%
```

under the selected fully saturated windows.

### Work-conserving borrowing

mMTC had:

```text
protected weight = 0
```

but still received residual resources through the borrowing path.

### Demand-aware redistribution

When URLLC could not use its entire protected target, unused capacity was
returned to the common pool and successfully used by eMBB.

### High Phase-3 resource commitment

The selected saturated windows showed a mean committed RB count close to
the nominal per-window Phase-3 ceiling.

---

## 22. What the Validation Does Not Establish

The RB-level experiment does not by itself establish:

- guaranteed slice throughput,
- guaranteed URLLC latency,
- guaranteed packet-loss bounds,
- end-to-end SLA satisfaction,
- scheduler behavior for every possible traffic pattern,
- physical-layer spectral efficiency,
- USRP behavior,
- over-the-air behavior,
- or universal correctness of the entire OAI stack.

Those require separate experiments or formal properties.

---

## 23. Relationship to Formal Verification

The formal-verification result and the RB-level validation answer different
questions.

Formal verification establishes a property of the frozen DQN:

```text
URLLC active
    =>
selected URLLC weight >= 20
```

RB-level validation examines what happens after that DQN decision is
installed in the OAI scheduler.

The combined assurance path is:

```text
Formal verification
        |
        v
Safe DQN decision property
        |
        v
ONNX inference in xApp
        |
        v
Slice-SM control
        |
        v
OAI Slice Manager
        |
        v
MAC scheduling
        |
        v
RB-level runtime evidence
```

Formal verification therefore validates the policy decision property,
while RB-level instrumentation validates implementation behavior in the
scheduler.

---

## 24. Evidence Files

The repository preserves the primary RB-validation evidence under:

```text
evidence/rb-validation/
```

including:

```text
dqn_rb_validation_evidence.log
gNB_scheduler_dlsch_default_policies.c.dqn_rb_validated
```

Artifact hashes are stored under:

```text
evidence/hashes/
```

including:

```text
dqn_rb_validated_sha256.txt
```

The validated scheduler source snapshot allows the runtime evidence to be
associated with the scheduler implementation used during the experiment.

---

## 25. Inspecting the Preserved Evidence

The RB-validation log can be inspected with:

```bash
grep '\[DQN RB VALID\]' \
  evidence/rb-validation/dqn_rb_validation_evidence.log
```

The preserved hashes can be inspected with:

```bash
cat evidence/hashes/dqn_rb_validated_sha256.txt
```

The validated scheduler source can be inspected with:

```bash
less \
  evidence/rb-validation/gNB_scheduler_dlsch_default_policies.c.dqn_rb_validated
```

---

## 26. Recomputing the Protected Ratio

For a set of selected fully saturated windows, the protected allocation
ratio should be computed using:

```text
eMBB ratio =
sum(eMBB protected_used)
/
(
    sum(eMBB protected_used)
    +
    sum(URLLC protected_used)
)

URLLC ratio =
sum(URLLC protected_used)
/
(
    sum(eMBB protected_used)
    +
    sum(URLLC protected_used)
)
```

For the nine saturated windows documented here:

```text
sum(eMBB protected_used)  = 144690
sum(URLLC protected_used) = 563372
```

which gives approximately:

```text
eMBB  = 20.43%
URLLC = 79.57%
```

---

## 27. Validation Summary

The RB-level experiment provides runtime evidence that the DQN policy is
translated into actual OAI MAC scheduling behavior.

For the tested policy:

```text
[20, 80, 0]
```

the experiment showed:

```text
DQN decision
    |
    v
[20,80,0]
    |
    v
integer target
[21000,85000,0]
    |
    v
protected scheduling
    |
    +--> eMBB / URLLC successful protected ratio
    |    approximately 20.43 / 79.57
    |
    +--> unused protected capacity
    |    redistributed through common pool
    |
    +--> residual capacity
         borrowed by mMTC
```

The result supports the intended scheduler design:

```text
priority protection
        +
demand awareness
        +
common-pool redistribution
        +
residual borrowing
        =
work-conserving DQN-controlled scheduling
```

The validation should be interpreted as evidence of scheduler-level
resource behavior rather than as a claim of rigid final throughput shares
or exact physical-layer utilization.
