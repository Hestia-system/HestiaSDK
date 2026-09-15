#include "datetime_utils.h"
#include <cassert>
#include <cstdlib>
#include <ctime>
#include <limits>

static int64_t nowUs = 0;
int64_t esp_timer_get_time() { return nowUs; }

int main() {
  assert(!LocalClock::isValid());
  assert(LocalClock::nowEpochSecond() == -1);
  assert(LocalClock::nowDateTime() == "");
  assert(DateTimeUtils::parseEpochSecond("1970-01-01 00:00:00") == 0);
  assert(LocalClock::syncFromHA("2024-02-28 23:59:59"));
  nowUs += 1500000;
  assert(LocalClock::nowDateTime() == "2024-02-29 00:00:00");
  assert(!LocalClock::syncFromHA("2024-02-28 23:59:59"));
  nowUs += 500000;
  assert(LocalClock::nowDateTime() == "2024-02-29 00:00:01");

  const int64_t before = LocalClock::nowEpochSecond();
  for (const char* invalid : {"", "aaaa-mm-jj hh:mm:ss", "2023-02-29 12:00:00",
       "2100-02-29 12:00:00", "2024-04-31 12:00:00", "2024-01-01 24:00:00",
       "2024-01-01 12:60:00", "2024-01-01 12:00:60", "2024-01-01 xx:00:00",
       "2024-01-01 12:00:00Z", "2024-01-01T12:00:00", "1969-12-31 23:59:59",
       "2024-01-01 12:00", "2024-00-01 12:00:00", "2024-01-00 12:00:00"}) {
    assert(!LocalClock::sync(invalid));
    assert(DateTimeUtils::parseEpochSecond(invalid) == -1);
    assert(DateTimeUtils::parseHourFromDateTime(invalid) == -1);
    assert(DateTimeUtils::parseDayKey(invalid) == "");
    assert(LocalClock::nowEpochSecond() == before);
  }

  // Deux tours de millis sans lecture ni tick entre-temps.
  assert(LocalClock::sync("2024-01-01 00:00:00"));
  nowUs += (2LL * (1LL << 32) + 1234LL) * 1000LL;
  assert(LocalClock::nowEpochSecond() ==
         DateTimeUtils::parseEpochSecond("2024-01-01 00:00:00") +
         (2LL * (1LL << 32) + 1234LL) / 1000LL);

  // Corrections d'heure dans les deux sens et passage d'année.
  assert(LocalClock::sync("2023-12-31 23:59:59"));
  nowUs += 1000000;
  assert(LocalClock::nowDateTime() == "2024-01-01 00:00:00");
  assert(LocalClock::sync("2040-06-15 13:14:15"));
  assert(LocalClock::minuteOfDay() == 794);
  assert(LocalClock::dayKey() == "2040-06-15");

  // Les conversions ne changent pas avec TZ et restent cohérentes après 2038.
  for (const char* zone : {"UTC0", "EST5EDT", "JST-9"}) {
    setenv("TZ", zone, 1);
    tzset();
    const int64_t epoch = DateTimeUtils::parseEpochSecond("2040-06-15 13:14:15");
    assert(epoch == LocalClock::nowEpochSecond());
    assert(DateTimeUtils::formatClockSecond(epoch) == "13:14:15");
    assert(LocalClock::formatClockSecond(epoch) == "13:14:15");
    assert(DateTimeUtils::parseEpochMinute("2040-06-15 13:14:15") == epoch / 60);
  }
  assert(DateTimeUtils::isHourInDayWindow(23, 22, 6));
  assert(!DateTimeUtils::isHourInDayWindow(6, 22, 6));
  assert(DateTimeUtils::isHourInDayWindow(0, 6, 6));
  assert(!DateTimeUtils::isHourInDayWindow(24, 6, 6));
  assert(DateTimeUtils::formatClock(1440) == "--:--");
  assert(DateTimeUtils::formatClockMinute(std::numeric_limits<int64_t>::max()) == "--:--");
  assert(LocalClock::sync("9999-12-31 23:59:59"));
  nowUs += 1000000;
  assert(!LocalClock::isValid());
  LocalClock::invalidate();
  assert(!LocalClock::isValid());
  assert(LocalClock::syncFromHA("2024-02-28 23:59:59"));
}
