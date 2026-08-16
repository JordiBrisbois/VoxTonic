#include "live_data_api.hpp"

#include <vector>

namespace voxtonic::live_data {

bool initialize(void*) { return false; }
void shutdown() {}
void pump() {}
bool ready() { return false; }
void setTrackedIds(std::vector<std::uint32_t>) {}
std::vector<std::uint32_t> activeEffectIds() { return {}; }
Snapshot snapshot() { return {}; }
Diagnostics diagnostics() { return {}; }
const char* diagnosticStage() { return "not_initialized"; }

} // namespace voxtonic::live_data
