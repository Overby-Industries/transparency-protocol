#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "json.hpp"
#include "otb1/otb1.hpp"

using namespace otb1;

namespace {

// The corpus is the repository's own: the same files tools/validate.py runs
// the JSON Schema over. Nothing is copied, so the two cannot drift.
std::vector<std::filesystem::path> jsonFilesIn(const char* subdirectory) {
    std::vector<std::filesystem::path> files;
    const std::filesystem::path dir = std::filesystem::path(OTB1_REPO_DIR) / subdirectory;
    if (std::filesystem::is_directory(dir)) {
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.path().extension() == ".json") files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// Order-insensitive structural equality: two documents that say the same
// thing with their members in a different order are the same record.
bool sameDocument(const Json& a, const Json& b) {
    if (a.type() != b.type()) return false;
    switch (a.type()) {
        case Json::Type::Null: return true;
        case Json::Type::Bool: return a.asBool() == b.asBool();
        case Json::Type::Number: return a.asNumber() == b.asNumber();
        case Json::Type::String: return a.asString() == b.asString();
        case Json::Type::Array:
            if (a.items().size() != b.items().size()) return false;
            for (std::size_t i = 0; i < a.items().size(); ++i) {
                if (!sameDocument(a.items()[i], b.items()[i])) return false;
            }
            return true;
        case Json::Type::Object:
            if (a.members().size() != b.members().size()) return false;
            for (const Json::Member& member : a.members()) {
                const Json* other = b.find(member.first);
                if (other == nullptr || !sameDocument(member.second, *other)) return false;
            }
            return true;
    }
    return false;
}

bool refuses(const std::string& text, std::string* why = nullptr) {
    Record record;
    std::string error;
    const bool refused = !decode(text, record, &error);
    if (why != nullptr) *why = error;
    return refused && !error.empty();
}

// A complete, valid record: the land purchase from the README.
Record landPurchase() {
    Record record;
    record.timestamp = 1741960800000;  // 2025-03-14T14:00:00Z
    record.investment = {"INV-2025-0001", InvestmentType::Land,
                         "50 acres for Overby Airfield Development", InvestmentStatus::Completed};
    record.financials = {2200000.0, "AeroLand Realty LLC", "Early Supporter Funds"};
    record.location.site = "West Texas";
    record.location.coordinates = Coordinates{31.9686, -99.9018};
    record.certifications.smesscApplicable = false;
    record.certifications.otbAudit = true;
    record.certifications.proofOfInvestmentToken = "0xinv20250001";
    return record;
}

// encode(landPurchase()) with one substring swapped for another.
std::string landPurchaseWith(const std::string& from, const std::string& to) {
    std::string text = encode(landPurchase());
    const std::size_t at = text.find(from);
    if (at == std::string::npos) return "<<fixture substring not found: " + from + ">>";
    text.replace(at, from.size(), to);
    return text;
}

void test_canonical_examples_decode_losslessly() {
    TEST("every example in the spec decodes, and re-encodes to the same record");

    const auto files = jsonFilesIn("examples");
    CHECK(files.size() >= 3);
    for (const auto& path : files) {
        const std::string original = readFile(path);
        Record record;
        std::string error;
        const bool ok = decode(original, record, &error);
        if (!ok) std::printf("        %s: %s\n", path.filename().string().c_str(), error.c_str());
        CHECK(ok);
        if (!ok) continue;

        // Nothing may be dropped, renamed or altered on the way through.
        Json before, after;
        CHECK(Json::parse(original, before));
        CHECK(Json::parse(encode(record), after));
        const bool same = sameDocument(before, after);
        if (!same) std::printf("        %s changed in transit\n", path.filename().string().c_str());
        CHECK(same);
    }
}

void test_every_invalid_fixture_is_refused() {
    TEST("every record the schema rejects, the decoder rejects too");

    const auto files = jsonFilesIn("tests/invalid");
    CHECK(files.size() >= 18);
    for (const auto& path : files) {
        const bool refused = refuses(readFile(path));
        if (!refused) std::printf("        %s was accepted\n", path.filename().string().c_str());
        CHECK(refused);
    }
}

void test_a_record_decodes_field_for_field() {
    TEST("a record round-trips field for field, optional fields included");

    const Record sent = landPurchase();
    const std::string text = encode(sent);
    CHECK_EQ(text, std::string(
        R"({"version":"OTB-1","timestamp":"2025-03-14T14:00:00Z",)"
        R"("investment":{"id":"INV-2025-0001","type":"land",)"
        R"("item":"50 acres for Overby Airfield Development","status":"completed"},)"
        R"("financials":{"costUSD":2200000,"vendor":"AeroLand Realty LLC",)"
        R"("fundingSource":"Early Supporter Funds"},)"
        R"("location":{"site":"West Texas","coordinates":{"lat":31.9686,"lon":-99.9018}},)"
        R"("certifications":{"SMESSC_Applicable":false,"OTB_Audit":true,)"
        R"("ProofOfInvestmentToken":"0xinv20250001"}})"));

    Record got;
    CHECK(decode(text, got));
    CHECK_EQ(got.timestamp, sent.timestamp);
    CHECK_EQ(got.investment.id, std::string("INV-2025-0001"));
    CHECK(got.investment.type == InvestmentType::Land);
    CHECK_EQ(got.investment.item, sent.investment.item);
    CHECK(got.investment.status == InvestmentStatus::Completed);
    CHECK(got.financials.costUsd == 2200000.0);
    CHECK_EQ(got.financials.vendor, std::string("AeroLand Realty LLC"));
    CHECK_EQ(got.financials.fundingSource, std::string("Early Supporter Funds"));
    CHECK_EQ(got.location.site, std::string("West Texas"));
    CHECK(got.location.coordinates && got.location.coordinates->lat == 31.9686);
    CHECK(got.location.coordinates && got.location.coordinates->lon == -99.9018);
    CHECK(got.certifications.smesscApplicable && !*got.certifications.smesscApplicable);
    CHECK(got.certifications.otbAudit);
    CHECK(got.certifications.proofOfInvestmentToken &&
          *got.certifications.proofOfInvestmentToken == "0xinv20250001");
}

void test_absent_is_not_the_same_as_false() {
    TEST("an optional field left out stays left out; it is not turned into a value");

    Record record = landPurchase();
    record.location.coordinates.reset();
    record.certifications.smesscApplicable.reset();
    record.certifications.proofOfInvestmentToken.reset();
    const std::string text = encode(record);
    CHECK(text.find("coordinates") == std::string::npos);
    CHECK(text.find("SMESSC_Applicable") == std::string::npos);
    CHECK(text.find("ProofOfInvestmentToken") == std::string::npos);

    Record got;
    CHECK(decode(text, got));
    CHECK(!got.location.coordinates);
    // "The record did not say" must survive: an audit trail that quietly
    // turned silence into "false" would be asserting something nobody did.
    CHECK(!got.certifications.smesscApplicable);
    CHECK(!got.certifications.proofOfInvestmentToken);
    CHECK_EQ(encode(got), text);
}

void test_money_is_not_rounded() {
    TEST("a cost reads back as exactly the number that was written");

    for (double cost : {0.0, 0.01, 750000.0, 15000000.0, 1234567.89, 0.1 + 0.2, 9.99e12}) {
        Record record = landPurchase();
        record.financials.costUsd = cost;
        Record got;
        CHECK(decode(encode(record), got));
        CHECK(got.financials.costUsd == cost);
    }
}

void test_investment_ids() {
    TEST("an investment id is INV, a four-digit year and a serial of four or more digits");

    CHECK(isInvestmentId("INV-2025-0001"));
    CHECK(isInvestmentId("INV-2031-123456"));
    CHECK(isInvestmentId("INV-0000-0000"));

    CHECK(!isInvestmentId(""));
    CHECK(!isInvestmentId("INV-2025-001"));       // serial too short
    CHECK(!isInvestmentId("INV-25-0001"));        // year too short
    CHECK(!isInvestmentId("INV-20250-0001"));     // year too long
    CHECK(!isInvestmentId("INV-20250001"));       // no second dash
    CHECK(!isInvestmentId("inv-2025-0001"));      // case matters
    CHECK(!isInvestmentId("XINV-2025-0001"));
    CHECK(!isInvestmentId("INV-2O25-0001"));      // a letter O
    CHECK(!isInvestmentId("INV-2025-0001 "));
    CHECK(!isInvestmentId("INV-2025-0001\n"));    // what a careless "$" lets through
    CHECK(!isInvestmentId("INV-2025-00-01"));
    CHECK(!isInvestmentId("INV-2025--0001"));
}

void test_validate_applies_the_same_rules_as_decode() {
    TEST("validate() refuses a hand-built record for the reasons decode() would");

    CHECK(validate(landPurchase()));

    std::string why;
    Record record = landPurchase();
    record.investment.id = "PO-77";
    CHECK(!validate(record, &why));
    CHECK(why.find("investment/id") != std::string::npos);

    record = landPurchase();
    record.financials.costUsd = -5.0;
    CHECK(!validate(record, &why));
    CHECK(why.find("financials/costUSD") != std::string::npos);

    record = landPurchase();
    record.financials.costUsd = std::nan("");  // not a number is not a cost
    CHECK(!validate(record, &why));

    record = landPurchase();
    record.location.coordinates = Coordinates{91.0, 0.0};
    CHECK(!validate(record, &why));
    CHECK(why.find("location/coordinates/lat") != std::string::npos);

    record = landPurchase();
    record.location.coordinates = Coordinates{0.0, -180.5};
    CHECK(!validate(record, &why));
    CHECK(why.find("location/coordinates/lon") != std::string::npos);
}

void test_strictness_beyond_the_fixtures() {
    TEST("malformed records are refused, with a reason that names the field");

    Record record;
    CHECK(decode(encode(landPurchase()), record));  // the baseline the cases below break

    CHECK(refuses("not json at all"));
    CHECK(refuses("[]"));
    CHECK(refuses("{}"));
    CHECK(refuses(landPurchaseWith("\"OTB-1\"", "\"OTF-1\"")));
    CHECK(refuses(landPurchaseWith("2025-03-14T14:00:00Z", "14 March 2025")));
    CHECK(refuses(landPurchaseWith("\"costUSD\":2200000", "\"costUSD\":true")));
    CHECK(refuses(landPurchaseWith("\"OTB_Audit\":true", "\"OTB_Audit\":1")));
    CHECK(refuses(landPurchaseWith("\"SMESSC_Applicable\":false", "\"SMESSC_Applicable\":null")));
    CHECK(refuses(landPurchaseWith("\"site\":\"West Texas\"", "\"site\":7")));
    CHECK(refuses(landPurchaseWith("\"lat\":31.9686", "\"lat\":\"31.9686\"")));
    CHECK(refuses(landPurchaseWith("\"lon\":-99.9018", "\"lon\":-180.0001")));
    CHECK(refuses(landPurchaseWith(",\"lon\":-99.9018", ",\"lon\":-99.9018,\"alt\":120")));
    CHECK(refuses(landPurchaseWith("\"type\":\"land\"", "\"type\":\"Land\"")));

    std::string why;
    CHECK(refuses(landPurchaseWith("\"status\":\"completed\"", "\"status\":\"paid\""), &why));
    CHECK(why.find("investment/status") != std::string::npos);

    CHECK(refuses(landPurchaseWith("\"vendor\":\"AeroLand Realty LLC\",", ""), &why));
    CHECK(why.find("financials/vendor") != std::string::npos);

    CHECK(refuses(landPurchaseWith("\"OTB_Audit\":true", "\"OTB_Audit\":true,\"signedOff\":true"),
                  &why));
    CHECK(why.find("certifications/signedOff") != std::string::npos);

    // A decode that fails leaves the caller's record untouched.
    Record untouched = landPurchase();
    CHECK(!decode("{}", untouched));
    CHECK_EQ(untouched.investment.id, std::string("INV-2025-0001"));
}

}  // namespace

void runOtb1Tests() {
    std::printf("otb1\n");
    test_canonical_examples_decode_losslessly();
    test_every_invalid_fixture_is_refused();
    test_a_record_decodes_field_for_field();
    test_absent_is_not_the_same_as_false();
    test_money_is_not_rounded();
    test_investment_ids();
    test_validate_applies_the_same_rules_as_decode();
    test_strictness_beyond_the_fixtures();
}
