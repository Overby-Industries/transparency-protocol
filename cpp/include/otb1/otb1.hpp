#pragma once

// libotb1 -- the C++ reference implementation of OTB-1.
//
// OTB-1 is defined by the JSON Schema in schema/otb-1.schema.json: one
// record per expenditure, saying what was acquired, what it cost, from
// whom, where, and how it has been verified. This header is the same format
// as a C++ value, and the two functions that turn it into bytes and back.
//
//     std::string text = otb1::encode(record);     // compact JSON, one line
//     otb1::Record record;
//     std::string why;
//     if (!otb1::decode(text, record, &why)) { /* not OTB-1; `why` says so */ }
//
// decode() applies the schema's rules in C++ -- required fields, closed
// enums, value ranges, the investment id's form, no unknown fields -- so a
// program built on this library never publishes or acts on a record the
// schema would reject. The two are held together three ways: the test suite
// runs the decoder over the same examples/ and tests/invalid/ the schema is
// checked against; tools/differential.py feeds thousands of mutated records
// to both and fails on any disagreement; and everything encode() produces
// from a decoded record is validated against the schema.
//
// C++17, no dependencies beyond the standard library.

#include <optional>
#include <string>

#include "otb1/time.hpp"

namespace otb1 {

enum class InvestmentType { Land, Facility, Materials, Equipment, Services };
enum class InvestmentStatus { Planned, InProgress, Procurement, Completed };

const char* toString(InvestmentType value);
const char* toString(InvestmentStatus value);

// "INV-" then a four-digit year, "-", and a serial of four or more digits:
// INV-2025-0001. The form every investment id must have.
bool isInvestmentId(const std::string& text);

// What was acquired.
struct Investment {
    std::string id;  // INV-YYYY-NNNN
    InvestmentType type = InvestmentType::Land;
    std::string item;
    InvestmentStatus status = InvestmentStatus::Planned;
};

// What it cost, who was paid, and where the money came from.
struct Financials {
    double costUsd = 0.0;  // never negative
    std::string vendor;
    std::string fundingSource;
};

struct Coordinates {
    double lat = 0.0;  // -90 .. 90
    double lon = 0.0;  // -180 .. 180
};

struct Location {
    std::string site;
    std::optional<Coordinates> coordinates;
};

// How the entry has been verified.
struct Certifications {
    // True if the investment links to operational sustainability
    // certification (mining or facility operations). Unset means the
    // record did not say, which is not the same as false.
    std::optional<bool> smesscApplicable;

    // True once the entry has been verified by auditors.
    bool otbAudit = false;

    // Token id proving the line item exists in the ledger, if issued.
    std::optional<std::string> proofOfInvestmentToken;
};

// One OTB-1 record.
struct Record {
    TimeMs timestamp = 0;  // when the purchase or contract was executed
    Investment investment;
    Financials financials;
    Location location;
    Certifications certifications;
};

// Serialise a record as one line of compact JSON. encode() writes what it
// is given: a record assembled by hand with, say, a malformed id encodes to
// text that is not OTB-1. Use validate() before publishing one.
std::string encode(const Record& record);

// Parse and validate one record. Returns false -- and says why in `error`
// -- for anything that is not a well-formed OTB-1 record: malformed JSON, a
// missing or mistyped field, a value outside a closed enum or its range, a
// malformed investment id, or an unknown field.
bool decode(const std::string& text, Record& out, std::string* error = nullptr);

// Whether a record built in code is valid OTB-1, by the same rules decode()
// applies to one read from the wire.
bool validate(const Record& record, std::string* error = nullptr);

}  // namespace otb1
