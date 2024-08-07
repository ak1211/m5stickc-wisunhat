// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#pragma once
#include <cinttypes>
#include <cstring>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

// 2桁の16進数
struct HexedU8 final {
  uint8_t u8;
  constexpr HexedU8(uint8_t init = 0U) : u8{init} {}
  operator std::string() const;
};
inline bool operator==(const HexedU8 &lhs, const HexedU8 &rhs) {
  return lhs.u8 == rhs.u8;
}
inline bool operator!=(const HexedU8 &lhs, const HexedU8 &rhs) {
  return !(lhs == rhs);
}
inline std::istream &operator>>(std::istream &is, HexedU8 &v) {
  auto save = is.flags();
  int int_value;
  is >> std::setw(2) >> std::hex >> int_value;
  v = HexedU8{static_cast<uint8_t>(int_value)};
  is.flags(save);
  return is;
}
inline std::ostream &operator<<(std::ostream &os, const HexedU8 &v) {
  auto save = os.flags();
  os << std::setfill('0') << std::setw(2) << std::hex << std::uppercase
     << +v.u8;
  os.flags(save);
  return os;
}
inline std::optional<HexedU8> makeHexedU8(const std::string &in) {
  HexedU8 v;
  std::istringstream iss{in};
  iss >> v;
  return (iss.fail()) ? std::nullopt : std::make_optional(v);
}
inline HexedU8::operator std::string() const {
  std::ostringstream oss;
  oss << *this;
  return oss.str();
}

// 4桁の16進数
struct HexedU16 final {
  uint16_t u16;
  constexpr HexedU16(uint16_t init = 0U) : u16{init} {}
  operator std::string() const;
};
inline bool operator==(const HexedU16 &lhs, const HexedU16 &rhs) {
  return lhs.u16 == rhs.u16;
}
inline bool operator!=(const HexedU16 &lhs, const HexedU16 &rhs) {
  return !(lhs == rhs);
}
inline std::istream &operator>>(std::istream &is, HexedU16 &v) {
  auto save = is.flags();
  is >> std::setw(4) >> std::hex >> v.u16;
  is.flags(save);
  return is;
}
inline std::ostream &operator<<(std::ostream &os, const HexedU16 &v) {
  auto save = os.flags();
  os << std::setfill('0') << std::setw(4) << std::hex << std::uppercase
     << +v.u16;
  os.flags(save);
  return os;
}
inline std::optional<HexedU16> makeHexedU16(const std::string &in) {
  HexedU16 v;
  std::istringstream iss{in};
  iss >> v;
  return (iss.fail()) ? std::nullopt : std::make_optional(v);
}
inline HexedU16::operator std::string() const {
  std::ostringstream oss;
  oss << *this;
  return oss.str();
}

// 16桁の16進数
struct HexedU64 final {
  uint64_t u64;
  constexpr HexedU64(uint64_t init = 0U) : u64{init} {}
  operator std::string() const;
};
inline bool operator==(const HexedU64 &lhs, const HexedU64 &rhs) {
  return lhs.u64 == rhs.u64;
}
inline bool operator!=(const HexedU64 &lhs, const HexedU64 &rhs) {
  return !(lhs == rhs);
}
inline std::istream &operator>>(std::istream &is, HexedU64 &v) {
  auto save = is.flags();
  is >> std::setw(16) >> std::hex >> v.u64;
  is.flags(save);
  return is;
}
inline std::ostream &operator<<(std::ostream &os, const HexedU64 &v) {
  auto save = os.flags();
  os << std::setfill('0') << std::setw(16) << std::hex << std::uppercase
     << +v.u64;
  os.flags(save);
  return os;
}
inline std::optional<HexedU64> makeHexedU64(const std::string &in) {
  HexedU64 v;
  std::istringstream iss{in};
  iss >> v;
  return (iss.fail()) ? std::nullopt : std::make_optional(v);
}
inline HexedU64::operator std::string() const {
  std::ostringstream oss;
  oss << *this;
  return oss.str();
}
