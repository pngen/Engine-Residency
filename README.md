# Engine Residency

Engine Residency is an open-source, vendor-neutral C++20 runtime for governing serving-engine residency, warm/cold readiness, process replacement, standby capacity, model/KV/kernel/graph preparedness, and recovery economics across heterogeneous accelerator infrastructure.

It answers one systems question:

> Which serving-engine incarnation is authoritative, what is it actually prepared to serve now, which required state and resources are ready, and what must be warmed, rebound, replaced, or revalidated before it can safely accept work?

## Defining thesis

A serving engine is not ready because its process exists. It is ready when its current incarnation holds compatible, verified, generation-bound state and resource authority for the exact work it is being asked to serve.

A running process is not necessarily a ready serving engine. An engine may be alive while its model is absent, its model generation is stale, its adapter stack is incompatible, its KV capacity is unavailable, its kernel artifact is cached but not loaded, its execution graph is bound to dead process-local handles, its device context was replaced, its warmup failed, its readiness evidence belongs to an old worker boot, or its drain has started.

## Systems boundary

Engine Residency owns serving-engine residency and preparedness at the engine-incarnation boundary: logical engine identity, engine configuration generation, process-incarnation binding, current engine authority, desired versus observed residency, cold/warming/warm/hot preparedness, readiness profiles, model/adapter/KV/kernel/graph preparedness bindings, backend and device readiness, preparation plans and attempts, warmup verification, serving eligibility, generation-bound readiness snapshots, serving-use acquisition and release, active-use accounting, standby readiness and capacity accounting, authorized local activation, drain and retirement, process replacement, stale-incarnation rejection, conservative recovery, engine-local preparation/recovery cost evidence, deterministic explanations, and reference process and CUDA proof.

It does not own request scheduling. Inference Scheduler, Batch Fabric, Prefill Fabric, and Decode Fabric own request admission, batching, phase scheduling, and serving-loop decisions.

It does not own workload placement, global failover selection, or global recovery strategy. Fabric Scheduler, Failover Fabric, and Recovery Planner retain those responsibilities. It does not own generic replica lifecycle, workload warmth, model residency, raw device-memory allocation policy, reusable asset storage, dependency propagation, resource arbitration, execution-attempt authority, or safe interruption. Those belong to Replica Fabric, Warmth Fabric, Model Residency, GPU Memory Service, the caches, Dependency Fabric, Resource Broker, Execution Fabric, and Preemption Fabric respectively.

Engine Residency is deliberately narrow. It is not a generic process supervisor, model cache, GPU allocator, request scheduler, or global failover manager.

## Core doctrine

- Process liveness is not serving readiness.
- Desired residency is not observed residency.
- A cached model artifact is not a loaded model.
- A loaded model is not a warmed engine.
- Allocated KV capacity is not reusable request-specific KV state.
- A compiled kernel is not a loaded executable binding.
- A graph artifact is not a valid process-local executable graph.
- A completed warmup for one profile does not prove readiness for every shape or configuration.
- Warm standby is not active serving authority.
- A readiness query is not an irrevocable permission to execute later.
- A stale process must not make a replacement engine ready.
- A persisted readiness record is not proof of current device state.
- A drain request is not a completed drain.
- A process disappearing does not prove graceful cleanup.
- Recovery must be conservative.

## State dimensions

State is kept orthogonal, not collapsed into one overloaded enum. Separate dimensions are maintained for desired residency (ABSENT/COLD/WARM/HOT), process lifecycle (REGISTERING … RETIRED), preparation (UNPREPARED … REVALIDATION_REQUIRED), readiness outcome (UNKNOWN/BLOCKED/PREPARING/READY/DEGRADED/STALE/REVALIDATION_REQUIRED), activation/role authority (INACTIVE/STANDBY/ACTIVATING/ACTIVE/DRAINING/FENCED), health, recovery, and registration. HOT has a precise configured meaning and does not silently imply current execution. DEGRADED never authorizes a profile whose required prerequisites are missing.

## Identities and generations

Distinct strongly typed identity and generation wrappers model the applicable concepts (EngineId, EngineGeneration, EngineIncarnationId, WorkerBootId, CoordinatorEpoch, AuthorityGeneration, ReadinessProfileId, ReadinessGeneration, EvidenceId, PreparationAttemptId, StandbyGeneration, ActivationId, ServingUseId, DrainGeneration, ReplacementId, and the model/adapter/KV/kernel/graph/allocation/dependency generations among others). Semantically independent generations are never collapsed into interchangeable integers: an old WorkerBootId cannot publish current preparedness, an old ReadinessGeneration cannot authorize serving after invalidation, and an old ServingUseGeneration cannot decrement current active-use accounting. Generation exhaustion throws instead of wrapping into a previously valid authority value. A PID is diagnostic metadata, never durable identity.

## Authority model

The reference control plane uses one authoritative coordinator per managed domain, a monotonic durable epoch, explicit worker/session admission, and current authority required before any mutation. Fresh registration requires a coordinator-issued registration permit; a replay of a REGISTER under a fenced boot is rejected. Reconnect under the same boot is not process replacement. This is a single-coordinator reference model: it does not claim distributed consensus or partition-safe global leadership, and generation fencing is not cryptographic authentication. The reference service binds to loopback and documents its trust boundary.

## Readiness profiles and component evidence

Readiness is profile-specific. A profile binds the exact prerequisites — model and adapter generations, serving role, batch/shape limits, dtype/layout, backend ABI, device capability, memory/workspace capacity, KV capacity, kernel availability, graph replay compatibility, warmup requirements, and required dependency generations — and distinguishes REQUIRED from OPTIONAL from PERMITTED_FALLBACK from UNSUPPORTED. Component evidence is represented independently per component category (BACKEND, DEVICE_CONTEXT, MODEL, ADAPTER, KV_CAPACITY, KV_STATE, KERNEL, GRAPH, tokenizer/runtime assets, WORKSPACE, RESOURCE_CLAIM, DEPENDENCY, WARMUP), each bound to its source boot, incarnation, backend/device context, compatibility key, evidence generation, state, provenance, and freshness window. An AVAILABLE artifact never satisfies a requirement for a VERIFIED process-local binding. Provenance is explicit (MEASURED/REPORTED/DERIVED/ESTIMATED/SYNTHETIC/RECONSTRUCTED/UNKNOWN); coordinator bookkeeping is never presented as physical device telemetry.

## Serving-use fencing

A generation-bound serving-use acquisition atomically validates that the incarnation is current, the readiness snapshot still satisfies the profile, activation authority is valid, drain has not started, required resource capacity is present, and the required evidence generations are unchanged. This closes the readiness-query-to-execution race. It returns a typed token bound to the exact authority the backend needs. Active use is counted exactly; duplicate release, release from an old incarnation, and release under a drained authority are rejected. Existing admitted work that can finish safely under retained bindings is preserved until completion; a resource loss is reported as failed or unknown according to actual evidence. Invalidating future admission is never equated with forcibly stopping a kernel.

## Preparation, warmup, standby, activation, drain, replacement

Preparation plans are explicit, ordered, bounded, and validated against current authority before mutation. Attempts carry unique identities; transactional publication (validate → provisional → backend work → verify → revalidate authority → commit → retire) ensures a stale attempt cannot publish READY and a cancelled attempt cannot leave phantom READY state. Warmup executes real backend work and binds completion evidence, including a real warmup generation; a warmup for one profile never marks an incompatible profile ready. Standby pools and slots are represented explicitly: standby eligibility requires current engine preparedness and never counts dead processes, stale readiness, incompatible engines, already-active engines, or preparation intent. Hard compatibility and authority exclusions are applied first with stable tie-breaking; shared capacity is never double-counted. Activation is a compare-and-commit local transition that rejects stale or competing activation and, for an exclusive slot, allows at most one current activation. Drain fences new acquisition immediately and completes only when active use is closed and backend cleanup is acknowledged; forced process death is a separate failure transition, never reported as graceful drain. Replacement keeps old and candidate physical state separate; make-before-break retains old authority while the candidate prepares and proves readiness before an authorized cutover, and insufficient overlap resources yield a typed BLOCKED result rather than a zero-downtime claim.

## Recovery economics and conservative recovery

Recovery economics expose engine-local preparation/recovery estimates and measured observations for named components (process startup, backend/context setup, asset lookup, model transfer/binding, KV provisioning/restore, kernel binding, graph instantiation, warmup, verification, standby holding cost, temporary overlap capacity, estimated time to readiness) with explicit units and provenance; unknown costs remain unknown and no dollar, energy, or SLO values are fabricated. Aggregation respects the model (SEQUENTIAL vs PARALLEL_CRITICAL_PATH). After coordinator restart, durable definitions and history may survive but readiness becomes conservative: activation/use authority requires fresh revalidation, old-epoch messages reject, surviving workers revalidate current physical bindings, dead workers are not revived, and standby counts exclude unvalidated incarnations. A persisted READY field never bypasses revalidation.

## Persistence

Versioned, integrity-checked durable metadata persists engine definitions, configuration/profile generations, desired residency, pool policy, durable authority counters, replacement and preparation history, measured cost history, durable artifact references, and completed lifecycle history. Native pointers, sockets, process handles, CUDA contexts, executable graph handles, and live serving-use tokens are never persisted as recoverable authority. Recovery rejects bad magic, unsupported version, truncation, integrity mismatch, oversized lengths/counts, invalid enums, duplicate identities, invalid capacity, arithmetic overflow, NaN/Inf cost values, and trailing garbage. Corrupt load leaves the existing runtime unchanged.

## Reference backend and real process proof

Engine Residency provides a deterministic CPU reference backend and a real CUDA reference engine (RTX 5090 / sm_120) implementing bounded, deterministic serving-like computation with real model-like weight buffers, explicit workspace, KV-like capacity, real kernels, a real executable CUDA graph for a graph-required profile, host-pinned staging, and CPU-reference parity. The multiprocess reference deployment runs independent OS processes (coordinator, Worker A, Worker B, controller/client) over framed loopback TCP with HELLO/REGISTER, explicit registration authorization, bounded checksummed frames, partial read/write correctness, reconnect behavior, and clean shutdown. A deterministic reference workload is not a production LLM server. No TensorRT, vLLM, SGLang, ROCm, or other backend integration is claimed; those are not implemented.

## Installation

The core library is standalone (no CUDA, no TCP required). Configure and build:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target er_core
```

To build the reference transport, tools, tests, examples, benchmarks, and (with a CUDA toolkit) the CUDA engine:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DER_BUILD_TRANSPORT=ON \
      -DR_BUILD_TOOLS=ON -DR_BUILD_TESTS=ON -DR_BUILD_EXAMPLES=ON \
      -DR_BUILD_BENCHMARKS=ON -DR_BUILD_CUDA=ON
cmake --build build --config Release
```

Run tests with CTest (no test timeouts are set anywhere; a hang is a lifecycle defect, not a pass):

```
ctest --test-dir build -C Release --output-on-failure
```

The installed package exports `EngineResidency::EngineResidency`. A downstream consumer uses

```cmake
find_package(EngineResidency CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE EngineResidency::EngineResidency)
```

## Examples and CLI

Run the examples for cold-to-warm preparation, profile-specific readiness, serving-use fencing, recovery-cost comparison, and (with CUDA) real CUDA engine preparation and graph replay. The `er_cli` tool inspects a persisted state file (`er_cli inspect <state>`). The reference deployment is driven by `er_coordinator`, `er_worker`, and `er_controller`.

## Benchmarks

Timing covers component publication, readiness evaluation, serving-use acquire/release, invalidation, preparation planning, standby reconciliation, activation, drain bookkeeping, replacement commit, persistence save/recovery, and protocol encode/decode across 100/1k/10k engine and metadata scales. Enqueue latency is never reported as completed throughput.

## Provenance vocabulary

MEASURED, REPORTED, DERIVED, ESTIMATED, SYNTHETIC, RECONSTRUCTED, UNKNOWN, and UNSUPPORTED are distinguished where appropriate. The runtime does not claim multi-node or multi-GPU operation, production LLM serving, third-party backend integrations, zero-downtime replacement, session continuity, distributed consensus, or device-failure redundancy beyond what is implemented and proven.

## Actual limitations

- The CUDA reference engine and kernel are implemented and the kernel compiles with nvcc for sm_120, but the full CMake CUDA object build on this host (CMake 4.3 + MSVC 19.44 + CUDA 12.9 nvcc) fails at the kernel object step with "A single input file is required for a non-link phase when an outputfile is specified", caused by CMake injecting the MSVC depfile flag `-MT <object>` into the nvcc command line. The host CUDA library (cuda_engine.cpp) compiles and links. This is a toolchain integration limitation, not a defect in the engine or kernel.
- All reference workers share one GPU. Same-device process failover is reported; GPU/node redundancy is not claimed.
- The reference coordinator is single-coordinator and does not provide partition-safe leadership; fencing is not cryptographic authentication.
- The deterministic reference workload is not a production LLM server.

## License

Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.