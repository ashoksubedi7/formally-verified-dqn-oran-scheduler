# Formally Verified DQN-Based O-RAN Scheduler

Research prototype integrating a formally verified DQN scheduler with OpenAirInterface and FlexRIC.

## Components

- DQN scheduling policy
- ONNX Runtime inference
- Formal verification
- FlexRIC near-RT RIC xApp
- E2SM-KPM per-slice telemetry
- Slice-SM control
- OAI slice manager
- NSSAI-based slice classification
- Work-conserving NR MAC scheduling
- RB-level runtime validation

## Slices

| Slice | SST | SD |
|---|---:|---:|
| eMBB | 1 | 000001 |
| URLLC | 2 | 000002 |
| mMTC | 3 | 000003 |

## Control Path

```text
RLC queues -> E2SM-KPM -> FlexRIC -> DQN xApp -> ONNX inference
           -> Slice-SM -> OAI Slice Manager -> NR MAC -> DL RB allocation
```

## Current Status

- RFsim integration complete
- Three-slice congestion testing complete
- RB-level DQN scheduler validation complete
- USRP/OTA validation is the next stage

## Repository Structure

- docs/ - detailed documentation
- formal-verification/ - training and verification material
- models/ - frozen verified model artifacts
- xapp/ - FlexRIC DQN xApp
- oai/ - OAI integration files
- configs/ - experiment configurations
- scripts/ - reproducibility scripts
- patches/ - patches against upstream OAI/FlexRIC
- evidence/ - validation evidence and hashes
