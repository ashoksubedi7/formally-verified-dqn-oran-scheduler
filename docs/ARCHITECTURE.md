# System Architecture

## 1. Overview

This project integrates a formally verified Deep Q-Network (DQN) with
OpenAirInterface (OAI) and FlexRIC to perform adaptive slice-aware
downlink scheduling in an O-RAN environment.

The complete control loop is:

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
NR MAC scheduling
    |
    v
DL RB allocation

2. Network Slices

The current implementation contains three slices.

Slice	SST	SD
eMBB	1	000001
URLLC	2	000002
mMTC	3	000003

OAI uses S-NSSAI information to classify traffic into these slices.

The scheduler internally maps them as:

slice 0 -> eMBB
slice 1 -> URLLC
slice 2 -> mMTC
3. DQN State

The DQN receives one queue-load value for each slice:

[eMBB queue, URLLC queue, mMTC queue]

The runtime state is normalized using:

normalized_queue = clamp(raw_queue, 0, 500) / 500

Therefore each DQN input is in:

[0, 1]

The deployed neural network receives a three-dimensional floating-point
input vector.

4. DQN Model

The deployed model is:

formal_dqn_v2_candidate_16000.onnx

SHA-256:

699c2d326b8c86f81d86929fcf28e3a00086687e0391c6a1bd4119b92001103e

The model produces 22 Q-values.

The action is selected using:

action = argmax(Q-values)

Each action corresponds to a predefined slice-weight vector.

For example:

Action 7 -> [20, 80, 0]

These values represent scheduling preference weights rather than literal
physical PRB counts.

5. KPM Telemetry

The xApp subscribes to per-slice KPM telemetry.

Three separate subscriptions are used:

eMBB  -> SST=1, SD=000001
URLLC -> SST=2, SD=000002
mMTC  -> SST=3, SD=000003

The project exports the custom queue measurement:

OAI.RlcTxQueuePktsDl

Each subscription provides the queue-load state for one slice.

The xApp waits for a complete and sufficiently fresh three-slice state
before performing DQN inference.

6. FlexRIC DQN xApp

The xApp performs the following pipeline:

receive KPM indication
        |
        v
update per-slice queue state
        |
        v
freshness/readiness check
        |
        v
normalize queue state
        |
        v
ONNX Runtime inference
        |
        v
argmax action selection
        |
        v
runtime safety check
        |
        v
send Slice-SM control

The xApp also verifies the SHA-256 hash of the deployed ONNX model before
using it.

7. Slice-SM Control

The selected DQN action is translated into three slice weights and sent
through the Slice Service Model.

The current convention is:

slice ID 0 = eMBB
slice ID 1 = URLLC
slice ID 2 = mMTC

NVS capacity parameters are used to transport the normalized scheduling
weights.

The OAI E2 agent validates the received control message before installing
the policy.

8. OAI Slice Manager

The project introduces:

nr_slice_manager.c
nr_slice_manager.h

The slice manager stores the currently installed policy and provides a
consistent scheduler-visible snapshot.

Only predefined DQN action vectors are accepted.

This prevents arbitrary or malformed scheduling-weight vectors from being
installed through the E2 control path.

9. NR MAC Scheduler

The DQN policy affects only the new-data phase of the downlink scheduler.

The existing OAI scheduling stages for:

HARQ retransmissions
timing advance
beam control
MAC control elements

remain outside the DQN-controlled resource budget.

The DQN controls the scheduling opportunity for new downlink data.

10. Work-Conserving Scheduling

The slice weights are not implemented as rigid physical partitions.

The scheduler first creates weighted protected scheduling opportunities.

For example:

[20, 80, 0]

gives eMBB and URLLC protected scheduling opportunities in approximately
a 20/80 proportion.

If one slice cannot use its protected allocation, the unused resource is
returned to a common pool.

The remaining capacity can then be used by other backlogged slices.

This means a zero-weight slice such as mMTC can still receive otherwise
unused radio resources.

Therefore:

DQN weights != final throughput shares
DQN weights != fixed PRB partitions

They define protected scheduling preference.

11. Runtime Validation

RB-level scheduler instrumentation records:

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

This allows the implementation to distinguish:

DQN-protected allocation,
common-pool redistribution,
residual borrowing,
and total successful Phase-3 RB allocation.

Under saturated three-slice traffic, the DQN selected:

state   = [1.0, 1.0, 1.0]
action  = 7
weights = [20, 80, 0]

Measured protected allocation between eMBB and URLLC was approximately:

eMBB  ~20.4%
URLLC ~79.6%

mMTC also received residual RBs through the work-conserving borrowing
mechanism.

12. Experimental Platform

The current validation environment uses:

OpenAirInterface
FlexRIC
near-RT RIC
E2 interface
RFsim
OAI 5G Core
three simulated UEs

The tested RFsim radio configuration uses:

Band: n78
Bandwidth: 40 MHz
SCS: 30 kHz
NR PRBs: 106
TDD

The scheduler itself does not encode the DQN weights as literal 106-PRB
partitions.

13. Formal Verification Boundary

Formal verification applies to the frozen neural-network policy.

Runtime integration adds additional enforcement mechanisms including:

model hash validation,
exact action-vector validation,
input normalization,
state freshness checking,
runtime safety checking,
controlled Slice-SM installation.

Formal verification of the neural network and runtime system validation
are therefore treated as related but distinct assurance layers.

14. Current Validation Boundary

Completed:

DQN training
formal verification
ONNX deployment
FlexRIC integration
OAI integration
RFsim testing
three-slice congestion testing
RB-level scheduler validation

Next stage:

USRP / real-RF validation
