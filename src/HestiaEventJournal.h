#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace HestiaJournal {

enum class Severity : uint8_t { Info, Warning, Error, Critical };

// L'application definit librement ses identifiants source et code.
// `civilEpochSecond` vaut -1 tant qu'aucune horloge civile fiable n'est connue.
struct Event {
  uint32_t sequence = 0;
  uint64_t monotonicMs = 0;
  int64_t civilEpochSecond = -1;
  uint16_t source = 0;
  uint16_t code = 0;
  int32_t value = 0;
  int32_t context = 0;
  Severity severity = Severity::Info;
};

// Journal circulaire local. C'est une instance applicative : aucune RAM n'est
// reservee par le SDK tant que l'application ne declare pas un Journal.
// Utiliser depuis une seule tache, ou proteger l'instance dans l'application.
template <size_t Capacity, size_t TextSize = 0>
class Journal {
 public:
  static_assert(Capacity > 0, "Journal capacity must be positive");

  struct Record {
    Event event;
    char text[TextSize == 0 ? 1 : TextSize]{};
  };

  bool emit(const Event& event, const char* text = nullptr) {
    Record record{};
    record.event = event;
    record.event.sequence = nextSequence_;
    copyText(record.text, text);
    records_[head_] = record;
    head_ = (head_ + 1) % Capacity;
    if (count_ < Capacity) {
      ++count_;
    } else {
      ++overwrittenCount_;
    }
    ++nextSequence_;
    return true;
  }

  bool emit(uint16_t source, uint16_t code, Severity severity,
            uint64_t monotonicMs, int64_t civilEpochSecond = -1,
            int32_t value = 0, int32_t context = 0, const char* text = nullptr) {
    Event event;
    event.monotonicMs = monotonicMs;
    event.civilEpochSecond = civilEpochSecond;
    event.source = source;
    event.code = code;
    event.severity = severity;
    event.value = value;
    event.context = context;
    return emit(event, text);
  }

  size_t size() const { return count_; }
  bool empty() const { return count_ == 0; }
  uint32_t oldestSequence() const { return nextSequence_ - static_cast<uint32_t>(count_); }
  uint32_t nextSequence() const { return nextSequence_; }
  uint32_t overwrittenCount() const { return overwrittenCount_; }

  bool read(uint32_t sequence, Record& out) const {
    if (sequence < oldestSequence() || sequence >= nextSequence_) return false;
    const Record& record = records_[sequence % Capacity];
    if (record.event.sequence != sequence) return false;
    out = record;
    return true;
  }

  bool latest(Record& out) const {
    if (empty()) return false;
    out = records_[(head_ + Capacity - 1) % Capacity];
    return true;
  }

 private:
  static void copyText(char (&destination)[TextSize == 0 ? 1 : TextSize], const char* source) {
    if constexpr (TextSize == 0) {
      destination[0] = '\0';
    } else if (source == nullptr) {
      destination[0] = '\0';
    } else {
      strncpy(destination, source, TextSize - 1);
      destination[TextSize - 1] = '\0';
    }
  }

  Record records_[Capacity]{};
  size_t head_ = 0;
  size_t count_ = 0;
  uint32_t nextSequence_ = 0;
  uint32_t overwrittenCount_ = 0;
};

}  // namespace HestiaJournal
