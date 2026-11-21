#include "otb1/time.hpp"

#include <cstdio>

namespace otb1 {

namespace {

constexpr TimeMs kMsPerSecond = 1000;
constexpr TimeMs kMsPerDay = 86400 * kMsPerSecond;

// Days from 1970-01-01 to a proleptic-Gregorian civil date, and back.
// Howard Hinnant's algorithms: exact, branch-light, no tables.
std::int64_t daysFromCivil(std::int64_t year, unsigned month, unsigned day) {
    year -= month <= 2;
    const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
    const unsigned dayOfYear = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return era * 146097 + static_cast<std::int64_t>(dayOfEra) - 719468;
}

void civilFromDays(std::int64_t days, std::int64_t& year, unsigned& month, unsigned& day) {
    days += 719468;
    const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned dayOfEra = static_cast<unsigned>(days - era * 146097);
    const unsigned yearOfEra =
        (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
    const unsigned dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
    const unsigned monthIndex = (5 * dayOfYear + 2) / 153;
    day = dayOfYear - (153 * monthIndex + 2) / 5 + 1;
    month = monthIndex < 10 ? monthIndex + 3 : monthIndex - 9;
    year = static_cast<std::int64_t>(yearOfEra) + era * 400 + (month <= 2);
}

unsigned daysInMonth(std::int64_t year, unsigned month) {
    static const unsigned kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return month == 2 && leap ? 29 : kDays[month - 1];
}

// Reads exactly `count` decimal digits at `pos`, advancing it.
bool readDigits(const std::string& text, std::size_t& pos, int count, unsigned& out) {
    if (pos + static_cast<std::size_t>(count) > text.size()) return false;
    unsigned value = 0;
    for (int i = 0; i < count; ++i) {
        const char c = text[pos++];
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<unsigned>(c - '0');
    }
    out = value;
    return true;
}

bool expect(const std::string& text, std::size_t& pos, char c) {
    if (pos >= text.size() || text[pos] != c) return false;
    ++pos;
    return true;
}

}  // namespace

std::string formatTimestamp(TimeMs time) {
    // Floor division, so times before 1970 still split into a valid
    // day and a non-negative time of day.
    std::int64_t days = time / kMsPerDay;
    TimeMs ofDay = time % kMsPerDay;
    if (ofDay < 0) {
        ofDay += kMsPerDay;
        --days;
    }
    std::int64_t year = 0;
    unsigned month = 0;
    unsigned day = 0;
    civilFromDays(days, year, month, day);

    const unsigned ms = static_cast<unsigned>(ofDay % kMsPerSecond);
    const unsigned seconds = static_cast<unsigned>(ofDay / kMsPerSecond);
    char buf[48];
    if (ms == 0) {
        std::snprintf(buf, sizeof buf, "%04lld-%02u-%02uT%02u:%02u:%02uZ",
                      static_cast<long long>(year), month, day,
                      seconds / 3600, (seconds / 60) % 60, seconds % 60);
    } else {
        std::snprintf(buf, sizeof buf, "%04lld-%02u-%02uT%02u:%02u:%02u.%03uZ",
                      static_cast<long long>(year), month, day,
                      seconds / 3600, (seconds / 60) % 60, seconds % 60, ms);
    }
    return buf;
}

bool parseTimestamp(const std::string& text, TimeMs& out) {
    std::size_t pos = 0;
    unsigned year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (!readDigits(text, pos, 4, year) || !expect(text, pos, '-') ||
        !readDigits(text, pos, 2, month) || !expect(text, pos, '-') ||
        !readDigits(text, pos, 2, day) || !expect(text, pos, 'T') ||
        !readDigits(text, pos, 2, hour) || !expect(text, pos, ':') ||
        !readDigits(text, pos, 2, minute) || !expect(text, pos, ':') ||
        !readDigits(text, pos, 2, second)) {
        return false;
    }
    if (month < 1 || month > 12 || day < 1 || day > daysInMonth(year, month) ||
        hour > 23 || minute > 59 || second > 60) {  // 60: a leap second
        return false;
    }

    unsigned ms = 0;
    if (pos < text.size() && text[pos] == '.') {
        ++pos;
        int digits = 0;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
            if (digits < 3) ms = ms * 10 + static_cast<unsigned>(text[pos] - '0');
            ++digits;
            ++pos;
        }
        if (digits == 0) return false;
        for (; digits < 3; ++digits) ms *= 10;
    }

    std::int64_t offsetMinutes = 0;
    if (expect(text, pos, 'Z')) {
        // UTC
    } else if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) {
        const bool negative = text[pos++] == '-';
        unsigned offsetHour = 0, offsetMinute = 0;
        if (!readDigits(text, pos, 2, offsetHour) || !expect(text, pos, ':') ||
            !readDigits(text, pos, 2, offsetMinute) || offsetHour > 23 || offsetMinute > 59) {
            return false;
        }
        offsetMinutes = static_cast<std::int64_t>(offsetHour) * 60 + offsetMinute;
        if (negative) offsetMinutes = -offsetMinutes;
    } else {
        return false;
    }
    if (pos != text.size()) return false;

    const std::int64_t days = daysFromCivil(year, month, day);
    const std::int64_t seconds =
        static_cast<std::int64_t>(hour) * 3600 + minute * 60 + second - offsetMinutes * 60;
    out = days * kMsPerDay + seconds * kMsPerSecond + ms;
    return true;
}

}  // namespace otb1
