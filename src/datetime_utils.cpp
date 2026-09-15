#include "datetime_utils.h"

#include <esp_timer.h>

namespace {
// Heure civile, indépendante du fuseau système et des connexions réseau.
constexpr int64_t kMaxCivilSecond = 253402300799LL; // 9999-12-31 23:59:59
int64_t s_epochAtSync = -1;
int64_t s_microsAtSync = 0;
String s_lastHaDateTime;

bool isLeapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int daysInMonth(int year, int month) {
  static const uint8_t kDays[] = {31, 28, 31, 30, 31, 30,
                                  31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  return (month == 2 && isLeapYear(year)) ? 29 : kDays[month - 1];
}

int64_t daysSinceUnixEpoch(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
  const unsigned adjustedMonth = month > 2 ? month - 3 : month + 9;
  const unsigned dayOfYear = (153 * adjustedMonth + 2) / 5 + day - 1;
  const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 -
                            yearOfEra / 100 + dayOfYear;
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(dayOfEra) -
         719468;
}

void civilFromDays(int64_t days, int& year, unsigned& month, unsigned& day) {
  days += 719468;
  const int era = (days >= 0 ? days : days - 146096) / 146097;
  const unsigned dayOfEra = static_cast<unsigned>(days - era * 146097);
  const unsigned yearOfEra =
      (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) /
      365;
  year = static_cast<int>(yearOfEra) + era * 400;
  const unsigned dayOfYear =
      dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
  const unsigned monthPrime = (5 * dayOfYear + 2) / 153;
  day = dayOfYear - (153 * monthPrime + 2) / 5 + 1;
  month = monthPrime + (monthPrime < 10 ? 3 : -9);
  year += month <= 2;
}

bool parseHaDateTime(const String& value, int64_t& epochSecond) {
  if (value.length() != 19 || value.charAt(4) != '-' || value.charAt(7) != '-' ||
      value.charAt(10) != ' ' || value.charAt(13) != ':' ||
      value.charAt(16) != ':') {
    return false;
  }

  for (size_t i = 0; i < 19; ++i) {
    if (i == 4 || i == 7 || i == 10 || i == 13 || i == 16) continue;
    if (value.charAt(i) < '0' || value.charAt(i) > '9') return false;
  }

  const int year = value.substring(0, 4).toInt();
  const int month = value.substring(5, 7).toInt();
  const int day = value.substring(8, 10).toInt();
  const int hour = value.substring(11, 13).toInt();
  const int minute = value.substring(14, 16).toInt();
  const int second = value.substring(17, 19).toInt();
  if (year < 1970 || month < 1 || month > 12 || day < 1 ||
      day > daysInMonth(year, month) || hour < 0 || hour > 23 || minute < 0 ||
      minute > 59 || second < 0 || second > 59) {
    return false;
  }

  epochSecond = daysSinceUnixEpoch(year, static_cast<unsigned>(month),
                                   static_cast<unsigned>(day)) *
                    86400LL +
                static_cast<int64_t>(hour) * 3600LL +
                static_cast<int64_t>(minute) * 60LL + second;
  return true;
}

bool splitEpoch(int64_t epochSecond, int& year, unsigned& month, unsigned& day,
                int& hour, int& minute, int& second) {
  if (epochSecond < 0 || epochSecond > kMaxCivilSecond) return false;
  const int64_t days = epochSecond / 86400LL;
  int64_t secondsInDay = epochSecond % 86400LL;
  if (secondsInDay < 0) secondsInDay += 86400LL;
  civilFromDays(days, year, month, day);
  hour = static_cast<int>(secondsInDay / 3600LL);
  minute = static_cast<int>((secondsInDay % 3600LL) / 60LL);
  second = static_cast<int>(secondsInDay % 60LL);
  return true;
}

}  // namespace

namespace LocalClock {

void invalidate() {
  s_epochAtSync = -1;
  s_microsAtSync = 0;
  s_lastHaDateTime = "";
}

bool sync(const String& localDateTime) {
  int64_t parsed = -1;
  if (!parseHaDateTime(localDateTime, parsed)) return false;
  s_epochAtSync = parsed;
  s_microsAtSync = esp_timer_get_time();
  s_lastHaDateTime = "";
  return true;
}

bool syncFromHA(const String& haDateTime) {
  if (haDateTime == s_lastHaDateTime) {
    return false;
  }

  if (!sync(haDateTime)) return false;
  s_lastHaDateTime = haDateTime;
  return true;
}

bool isValid() {
  return nowEpochSecond() >= 0;
}

int64_t nowEpochSecond() {
  if (s_epochAtSync < 0) return -1;
  const int64_t elapsedUs = esp_timer_get_time() - s_microsAtSync;
  if (elapsedUs < 0) return -1;
  const int64_t now = s_epochAtSync + elapsedUs / 1000000LL;
  return now <= kMaxCivilSecond ? now : -1;
}

int64_t nowEpochMinute() {
  const int64_t epochSecond = nowEpochSecond();
  return epochSecond < 0 ? -1 : epochSecond / 60LL;
}

int minuteOfDay() {
  const int64_t epochSecond = nowEpochSecond();
  return epochSecond < 0 ? -1 : static_cast<int>((epochSecond % 86400LL) / 60LL);
}

String nowDateTime() {
  const int64_t epochSecond = nowEpochSecond();
  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  if (!splitEpoch(epochSecond, year, month, day, hour, minute, second)) {
    return "";
  }
  char buffer[20];
  snprintf(buffer, sizeof(buffer), "%04d-%02u-%02u %02d:%02d:%02d", year,
           month, day, hour, minute, second);
  return String(buffer);
}

String dayKey() {
  const String dateTime = nowDateTime();
  return dateTime.length() >= 10 ? dateTime.substring(0, 10) : "";
}

String formatClockMinute(int64_t localEpochSecond) {
  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  if (!splitEpoch(localEpochSecond, year, month, day, hour, minute, second)) {
    return "--:--";
  }
  char buffer[6];
  snprintf(buffer, sizeof(buffer), "%02d:%02d", hour, minute);
  return String(buffer);
}

String formatClockSecond(int64_t localEpochSecond) {
  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  if (!splitEpoch(localEpochSecond, year, month, day, hour, minute, second)) {
    return "--:--:--";
  }
  char buffer[9];
  snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", hour, minute, second);
  return String(buffer);
}

int dayOfMonth(int64_t localEpochSecond) {
  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  return splitEpoch(localEpochSecond, year, month, day, hour, minute, second)
             ? static_cast<int>(day)
             : -1;
}

}  // namespace LocalClock

namespace DateTimeUtils {

int parseHourFromDateTime(const String& dateTime) {
  const int minute = parseMinuteOfDay(dateTime);
  return minute < 0 ? -1 : minute / 60;
}

int parseMinuteOfDay(const String& dateTime) {
  const int64_t epoch = parseEpochSecond(dateTime);
  return epoch < 0 ? -1 : static_cast<int>((epoch % 86400LL) / 60LL);
}

String parseDayKey(const String& dateTime) {
  return parseEpochSecond(dateTime) < 0 ? String("") : dateTime.substring(0, 10);
}

int64_t parseEpochSecond(const String& dateTime) {
  int64_t epoch = -1;
  return parseHaDateTime(dateTime, epoch) ? epoch : -1;
}

int64_t parseEpochMinute(const String& dateTime) {
  const int64_t epochSecond = parseEpochSecond(dateTime);
  return (epochSecond < 0) ? -1 : (epochSecond / 60);
}

bool isHourInDayWindow(int hour, int hourDay, int hourNight) {
  if (hour < 0 || hour > 23 || hourDay < 0 || hourDay > 23 || hourNight < 0 || hourNight > 23) {
    return false;
  }
  if (hourDay < hourNight) {
    return hour >= hourDay && hour < hourNight;
  }
  if (hourDay > hourNight) {
    return hour >= hourDay || hour < hourNight;
  }
  return true;
}

String formatClock(int minuteOfDay) {
  if (minuteOfDay < 0 || minuteOfDay >= 1440) {
    return "--:--";
  }
  const int hour = (minuteOfDay / 60) % 24;
  const int minute = minuteOfDay % 60;
  char buffer[6];
  snprintf(buffer, sizeof(buffer), "%02d:%02d", hour, minute);
  return String(buffer);
}

String formatClockMinute(int64_t epochSecond) {
  return LocalClock::formatClockMinute(epochSecond);
}

String formatClockSecond(int64_t epochSecond) {
  return LocalClock::formatClockSecond(epochSecond);
}

} // namespace DateTimeUtils
