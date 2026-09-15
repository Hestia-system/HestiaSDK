#pragma once

#include <stdint.h>

namespace HestiaEquipment {

enum class State : uint8_t {
  Unknown,       // aucune mesure encore disponible
  InvalidSample, // mesure absente, invalide ou perimee
  Matching,      // commande et retour concordent
  Settling,      // ecart observe, delai de grace en cours
  Fault          // ecart persistant au-dela du delai
};

// Compare une commande logique avec un retour deja interprete par
// l'application. Cette classe ne lit aucun capteur et n'actionne aucune sortie.
class Supervisor {
 public:
  explicit Supervisor(uint32_t settleDelayMs = 0);

  void setSettleDelayMs(uint32_t delayMs);
  uint32_t settleDelayMs() const;
  void reset();

  // `nowMs` est une horloge monotone 32 bits, typiquement millis(). Les
  // soustractions internes restent correctes au passage a zero de millis().
  State update(bool commandedOn, bool sampleValid, bool observedOn, uint32_t nowMs);

  State state() const;
  bool hasFault() const;
  bool stateChanged() const;
  bool command() const;
  bool observation() const;

 private:
  uint32_t settleDelayMs_;
  uint32_t mismatchStartedMs_ = 0;
  State state_ = State::Unknown;
  bool stateChanged_ = false;
  bool commandedOn_ = false;
  bool observedOn_ = false;
  bool mismatchActive_ = false;
};

}  // namespace HestiaEquipment
