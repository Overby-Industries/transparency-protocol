#include "check.hpp"

#include "otb1/time.hpp"

using otb1::TimeMs;
using otb1::formatTimestamp;
using otb1::parseTimestamp;

namespace {

bool parsesTo(const char* text, TimeMs expected) {
    TimeMs out = -1;
    return parseTimestamp(text, out) && out == expected;
}

bool refuses(const char* text) {
    TimeMs out = 0;
    return !parseTimestamp(text, out);
}

void test_known_instants() {
    TEST("known instants format and parse to the right millisecond");

    CHECK_EQ(formatTimestamp(0), std::string("1970-01-01T00:00:00Z"));
    CHECK_EQ(formatTimestamp(1739887522000), std::string("2025-02-18T14:05:22Z"));
    CHECK_EQ(formatTimestamp(1739887522250), std::string("2025-02-18T14:05:22.250Z"));
    CHECK_EQ(formatTimestamp(951782400000), std::string("2000-02-29T00:00:00Z"));  // leap day
    CHECK_EQ(formatTimestamp(-1), std::string("1969-12-31T23:59:59.999Z"));

    CHECK(parsesTo("1970-01-01T00:00:00Z", 0));
    CHECK(parsesTo("2025-02-18T14:05:22Z", 1739887522000));
    CHECK(parsesTo("2025-02-18T14:05:22.25Z", 1739887522250));
    CHECK(parsesTo("2025-02-18T14:05:22.250999Z", 1739887522250));  // truncated, not rounded
    CHECK(parsesTo("2025-02-18T15:05:22+01:00", 1739887522000));    // offset form
    CHECK(parsesTo("2025-02-18T09:35:22-04:30", 1739887522000));
}

void test_round_trip_across_a_wide_range() {
    TEST("format and parse are inverses across centuries");

    // Step by a prime number of milliseconds a little over 11 days so the
    // walk lands on every month, leap years, and a spread of times of day.
    const TimeMs step = 1000003LL * 977;
    int mismatches = 0;
    for (TimeMs t = -2208988800000LL; t < 7258118400000LL; t += step) {  // 1900 .. 2200
        TimeMs back = 0;
        if (!parseTimestamp(formatTimestamp(t), back) || back != t) ++mismatches;
    }
    CHECK_EQ(mismatches, 0);
}

void test_malformed_timestamps_are_refused() {
    TEST("anything that is not an RFC 3339 instant is refused");

    CHECK(refuses(""));
    CHECK(refuses("2025-02-18"));
    CHECK(refuses("2025-02-18T14:05:22"));        // no zone
    CHECK(refuses("2025-02-18 14:05:22Z"));       // space instead of T
    CHECK(refuses("2025-02-30T00:00:00Z"));       // no such day
    CHECK(refuses("2025-02-29T00:00:00Z"));       // 2025 is not a leap year
    CHECK(refuses("2025-13-01T00:00:00Z"));
    CHECK(refuses("2025-00-10T00:00:00Z"));
    CHECK(refuses("2025-02-18T24:00:00Z"));
    CHECK(refuses("2025-02-18T14:60:00Z"));
    CHECK(refuses("2025-02-18T14:05:22.Z"));
    CHECK(refuses("2025-02-18T14:05:22Zjunk"));
    CHECK(refuses("2025-02-18T14:05:22+0100"));
    CHECK(refuses("25-02-18T14:05:22Z"));
    CHECK(refuses("yesterday"));
}

}  // namespace

void runTimeTests() {
    std::printf("time\n");
    test_known_instants();
    test_round_trip_across_a_wide_range();
    test_malformed_timestamps_are_refused();
}
