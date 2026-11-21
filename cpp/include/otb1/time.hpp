#pragma once

// Time on the wire and time in the code.
//
// In code a time is a count of milliseconds since the Unix epoch, UTC. On
// the wire it is the ISO-8601 / RFC 3339 string OTB-1 specifies. Nothing in
// this library reads the machine's clock: a caller that needs "now" supplies
// it, so the same code gives the same answer wherever and whenever it runs.

#include <cstdint>
#include <string>

namespace otb1 {

using TimeMs = std::int64_t;

// "2025-02-18T14:05:22Z", or "2025-02-18T14:05:22.250Z" when there is a
// sub-second part. Always UTC, always 'Z'.
std::string formatTimestamp(TimeMs time);

// Accepts the same form, plus a numeric offset ("+01:00") in place of 'Z',
// since RFC 3339 allows one and other producers may use it. Fractions finer
// than a millisecond are truncated. Returns false for anything else.
bool parseTimestamp(const std::string& text, TimeMs& out);

}  // namespace otb1
