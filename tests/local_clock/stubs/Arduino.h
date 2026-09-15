#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

// Surface Arduino String utilisée par datetime_utils, pour le test hôte.
class String {
 public:
  String(const char* value = "") : value_(value) {}
  size_t length() const { return value_.size(); }
  char charAt(size_t i) const { return i < value_.size() ? value_[i] : 0; }
  String substring(size_t first, size_t last) const {
    return String(value_.substr(first, last - first).c_str());
  }
  long toInt() const { return std::strtol(value_.c_str(), nullptr, 10); }
  bool operator==(const String& other) const { return value_ == other.value_; }
 private:
  std::string value_;
};
