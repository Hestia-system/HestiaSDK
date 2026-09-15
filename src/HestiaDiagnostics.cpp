#include "HestiaDiagnostics.h"

#include <Arduino.h>
#include <nvs.h>

namespace HestiaDiagnostics {

Snapshot capture() {
  Snapshot snapshot;
  snapshot.freeHeap = ESP.getFreeHeap();
  snapshot.minFreeHeap = ESP.getMinFreeHeap();

  nvs_stats_t stats{};
  snapshot.nvsError = nvs_get_stats(nullptr, &stats);
  if (snapshot.nvsError == ESP_OK) {
    snapshot.nvsStatsAvailable = true;
    snapshot.nvsUsedEntries = stats.used_entries;
    snapshot.nvsFreeEntries = stats.free_entries;
    snapshot.nvsTotalEntries = stats.total_entries;
    snapshot.nvsNamespaceCount = stats.namespace_count;
  }
  return snapshot;
}

}  // namespace HestiaDiagnostics
