#include "engine_residency/components.hpp"

namespace engine_residency {
// Adjacent-runtime adapter contract boundary. Adapters never run while core
// state locks are held; the runtime stages evidence out of the lock and invokes
// callbacks only outside. This reference adapter performs no external I/O.
ComponentEvidence apply_adapter_stub(ComponentEvidence ev) {
  return ev;
}
}  // namespace engine_residency
