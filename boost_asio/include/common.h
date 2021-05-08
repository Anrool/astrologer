#ifndef COMMON_H
#define COMMON_H

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace astrologer {

constexpr char DELIM = '\n';

const std::string STAR_REQUEST_TYPE = "STARS SAY ";
const std::string CLIENT_REQUEST_TYPE = "HOROSCOPE ";

const std::string STAR_RESPONSE_SUCCESS = "THANKS!";
const std::string STAR_RESPONSE_FAILURE = "DENIED!";

const std::string UNKNOWN_PROGNOSIS = "YOU ARE OUT OF LUCK!";

inline const std::vector<std::string> &ZodiacList() {
  static const std::vector<std::string> zodiacs = {
      "Aries", "Taurus",  "Gemini",      "Cancer",    "Leo",      "Virgo",
      "Libra", "Scorpio", "Sagittarius", "Capricorn", "Aquarius", "Pisces"};

  return zodiacs;
}

inline bool StartsWith(const std::string &s, const std::string &prefix) {
  return !s.compare(0, prefix.size(), prefix);
}

inline bool FindZodiac(const std::string &zodiacWithRemainder,
                       std::string &zodiac) {
  for (const std::string &z : ZodiacList()) {
    if (StartsWith(zodiacWithRemainder, z)) {
      zodiac = z;
      return true;
    }
  }

  std::cerr << "Bad zodiac " << zodiacWithRemainder << std::endl;

  return false;
}

template <typename T>
bool Convert(const char *s, T &val,
             std::size_t min = std::numeric_limits<T>::min(),
             std::size_t max = std::numeric_limits<T>::max()) {
  static_assert(std::is_integral<T>::value && !std::is_signed<T>::value,
                "Only integral unsigned types are supported");

  char *endptr = nullptr;
  errno = 0;
  const auto tmp = std::strtoull(s, &endptr, 0);

  if (*endptr) {
    std::cerr << "Failed to convert value " << s << std::endl;
    return false;
  }

  if (errno) {
    std::cerr << "Bad value " << s << ", error: " << std::strerror(errno)
              << std::endl;

    errno = 0;
    return false;
  }

  if (tmp < min || tmp > max) {
    std::cerr << "Value " << tmp << " out of range [" << min << ", " << max
              << "]\n";
    return false;
  }

  val = static_cast<T>(tmp);
  return true;
}

using durationType = std::chrono::milliseconds;
using repType = durationType::rep;

constexpr repType ReadRequestTimeout = 120000;
constexpr repType WriteRequestTimeout = 120000;

constexpr repType ReadPrognosisTimeout = 120000;
constexpr repType WritePrognosisTimeout = 120000;

constexpr repType ReadResponseTimeout = 120000;
constexpr repType WriteResponseTimeout = 120000;

} // namespace astrologer

#endif // COMMON_H
