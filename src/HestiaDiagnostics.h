#pragma once

#include <stdint.h>

namespace HestiaDiagnostics {

// Instantane en lecture seule de la memoire du microcontroleur et de la NVS.
// Les champs NVS ne sont significatifs que si nvsStatsAvailable est vrai.
struct Snapshot {
  uint32_t freeHeap = 0;
  uint32_t minFreeHeap = 0;
  uint32_t nvsUsedEntries = 0;
  uint32_t nvsFreeEntries = 0;
  uint32_t nvsTotalEntries = 0;
  uint32_t nvsNamespaceCount = 0;
  int32_t nvsError = 0;
  bool nvsStatsAvailable = false;
};

// Ne publie rien et n'ecrit jamais dans la NVS.
Snapshot capture();

}  // namespace HestiaDiagnostics
