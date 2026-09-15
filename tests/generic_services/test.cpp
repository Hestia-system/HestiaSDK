#include <assert.h>
#include <stdint.h>

#include "HestiaEventJournal.h"
#include "HestiaEquipmentSupervisor.h"

int main() {
  HestiaJournal::Journal<2, 8> journal;
  journal.emit(1, 10, HestiaJournal::Severity::Info, 100, -1, 1, 2, "first");
  journal.emit(1, 11, HestiaJournal::Severity::Warning, 200, 42, 3, 4, "second");
  journal.emit(2, 12, HestiaJournal::Severity::Error, 300, 43, 5, 6, "third");
  assert(journal.size() == 2);
  assert(journal.overwrittenCount() == 1);
  assert(journal.oldestSequence() == 1);
  HestiaJournal::Journal<2, 8>::Record record;
  assert(!journal.read(0, record));
  assert(journal.read(1, record));
  assert(record.event.code == 11);
  assert(journal.latest(record));
  assert(record.event.sequence == 2);
  assert(record.text[7] == '\0');

  HestiaEquipment::Supervisor supervisor(100);
  assert(supervisor.update(true, false, false, 10) == HestiaEquipment::State::InvalidSample);
  assert(supervisor.update(true, true, true, 20) == HestiaEquipment::State::Matching);
  assert(supervisor.update(true, true, false, 30) == HestiaEquipment::State::Settling);
  assert(supervisor.update(true, true, false, 129) == HestiaEquipment::State::Settling);
  assert(supervisor.update(true, true, false, 130) == HestiaEquipment::State::Fault);
  assert(supervisor.hasFault());
  assert(supervisor.update(true, true, true, 131) == HestiaEquipment::State::Matching);

  HestiaEquipment::Supervisor wrapping(30);
  assert(wrapping.update(true, true, false, UINT32_MAX - 10) == HestiaEquipment::State::Settling);
  assert(wrapping.update(true, true, false, 19) == HestiaEquipment::State::Fault);
  return 0;
}
