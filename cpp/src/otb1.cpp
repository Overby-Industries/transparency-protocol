#include "otb1/otb1.hpp"

#include <cstring>
#include <limits>
#include <vector>

#include "json.hpp"

namespace otb1 {

namespace {

template <typename E>
struct Name {
    E value;
    const char* text;
};

const Name<InvestmentType> kTypes[] = {{InvestmentType::Land, "land"},
                                       {InvestmentType::Facility, "facility"},
                                       {InvestmentType::Materials, "materials"},
                                       {InvestmentType::Equipment, "equipment"},
                                       {InvestmentType::Services, "services"}};

const Name<InvestmentStatus> kStatuses[] = {{InvestmentStatus::Planned, "planned"},
                                            {InvestmentStatus::InProgress, "in-progress"},
                                            {InvestmentStatus::Procurement, "procurement"},
                                            {InvestmentStatus::Completed, "completed"}};

template <typename E, std::size_t N>
const char* nameOf(const Name<E> (&table)[N], E value) {
    for (const Name<E>& entry : table) {
        if (entry.value == value) return entry.text;
    }
    return "";
}

template <typename E, std::size_t N>
bool valueOf(const Name<E> (&table)[N], const std::string& text, E& out) {
    for (const Name<E>& entry : table) {
        if (text == entry.text) {
            out = entry.value;
            return true;
        }
    }
    return false;
}

// For quantities the schema bounds only from below. The JSON parser has
// already refused anything that is not a finite double.
constexpr double kNoMaximum = std::numeric_limits<double>::infinity();

bool isDigit(char c) { return c >= '0' && c <= '9'; }

}  // namespace

const char* toString(InvestmentType value) { return nameOf(kTypes, value); }
const char* toString(InvestmentStatus value) { return nameOf(kStatuses, value); }

bool isInvestmentId(const std::string& text) {
    // ^INV-[0-9]{4}-[0-9]{4,}$
    if (text.size() < 13 || text.compare(0, 4, "INV-") != 0 || text[8] != '-') return false;
    for (std::size_t i = 4; i < text.size(); ++i) {
        if (i != 8 && !isDigit(text[i])) return false;
    }
    return true;
}

// --------------------------------------------------------------------------
// Encoding

std::string encode(const Record& record) {
    Json packet = Json::object();
    packet.set("version", "OTB-1");
    packet.set("timestamp", formatTimestamp(record.timestamp));

    Json investment = Json::object();
    investment.set("id", record.investment.id);
    investment.set("type", toString(record.investment.type));
    investment.set("item", record.investment.item);
    investment.set("status", toString(record.investment.status));
    packet.set("investment", std::move(investment));

    Json financials = Json::object();
    financials.set("costUSD", record.financials.costUsd);
    financials.set("vendor", record.financials.vendor);
    financials.set("fundingSource", record.financials.fundingSource);
    packet.set("financials", std::move(financials));

    Json location = Json::object();
    location.set("site", record.location.site);
    if (record.location.coordinates) {
        Json coordinates = Json::object();
        coordinates.set("lat", record.location.coordinates->lat);
        coordinates.set("lon", record.location.coordinates->lon);
        location.set("coordinates", std::move(coordinates));
    }
    packet.set("location", std::move(location));

    Json certifications = Json::object();
    if (record.certifications.smesscApplicable) {
        certifications.set("SMESSC_Applicable", *record.certifications.smesscApplicable);
    }
    certifications.set("OTB_Audit", record.certifications.otbAudit);
    if (record.certifications.proofOfInvestmentToken) {
        certifications.set("ProofOfInvestmentToken", *record.certifications.proofOfInvestmentToken);
    }
    packet.set("certifications", std::move(certifications));

    return packet.dump();
}

// --------------------------------------------------------------------------
// Decoding

namespace {

// Reads the members of one JSON object, remembering which were asked for.
// finish() then fails if any member was never read, which is how "no
// unknown fields" is enforced without listing the known ones twice.
//
// Every getter returns false and records the first problem; once one has
// failed the rest are no-ops, so a decoder can read a whole object and
// check ok() once at the end.
class Fields {
public:
    Fields(const Json& object, std::string path, std::string& error)
        : object_(object), path_(std::move(path)), error_(error),
          seen_(object.members().size(), false) {
        if (!object.isObject()) {
            fail("", "must be an object");
        }
    }

    bool ok() const { return ok_; }

    // The raw member, or nullptr if absent. Absence is an error if required.
    const Json* get(const char* key, bool required) {
        if (!ok_) return nullptr;
        const Json::Object& members = object_.members();
        for (std::size_t i = 0; i < members.size(); ++i) {
            if (members[i].first == key) {
                seen_[i] = true;
                return &members[i].second;
            }
        }
        if (required) fail(key, "is required");
        return nullptr;
    }

    bool string(const char* key, std::string& out, bool required = true) {
        const Json* value = get(key, required);
        if (value == nullptr) return false;
        if (!value->isString()) return fail(key, "must be a string");
        out = value->asString();
        return true;
    }

    bool optionalString(const char* key, std::optional<std::string>& out) {
        std::string text;
        if (!string(key, text, false)) return false;
        out = std::move(text);
        return true;
    }

    bool number(const char* key, double& out, bool required, double min, double max) {
        const Json* value = get(key, required);
        if (value == nullptr) return false;
        if (!value->isNumber()) return fail(key, "must be a number");
        if (value->asNumber() < min || value->asNumber() > max) return fail(key, "is out of range");
        out = value->asNumber();
        return true;
    }

    bool boolean(const char* key, bool& out) {
        const Json* value = get(key, true);
        if (value == nullptr) return false;
        if (!value->isBool()) return fail(key, "must be true or false");
        out = value->asBool();
        return true;
    }

    bool optionalBoolean(const char* key, std::optional<bool>& out) {
        const Json* value = get(key, false);
        if (value == nullptr) return false;
        if (!value->isBool()) return fail(key, "must be true or false");
        out = value->asBool();
        return true;
    }

    bool timestamp(const char* key, TimeMs& out) {
        std::string text;
        if (!string(key, text)) return false;
        if (!parseTimestamp(text, out)) return fail(key, "is not an ISO-8601 UTC timestamp");
        return true;
    }

    template <typename E, std::size_t N>
    bool enumeration(const char* key, const Name<E> (&table)[N], E& out, bool required = true) {
        std::string text;
        if (!string(key, text, required)) return false;
        if (!valueOf(table, text, out)) return fail(key, "is not a recognised value");
        return true;
    }

    // Marks this object invalid because something nested inside it was. The
    // nested reader has already recorded the specific reason; keep that.
    void poison() { ok_ = false; }

    bool finish() {
        if (!ok_) return false;
        const Json::Object& members = object_.members();
        for (std::size_t i = 0; i < members.size(); ++i) {
            if (!seen_[i]) return fail(members[i].first.c_str(), "is not an OTB-1 field");
        }
        return true;
    }

    std::string child(const char* key) const {
        return path_.empty() ? std::string(key) : path_ + "/" + key;
    }

    bool fail(const char* key, const char* message) {
        if (ok_) {
            ok_ = false;
            std::string where = std::strlen(key) == 0 ? path_ : child(key);
            if (where.empty()) where = "record";
            error_ = where + " " + message;
        }
        return false;
    }

private:
    const Json& object_;
    std::string path_;
    std::string& error_;
    std::vector<bool> seen_;
    bool ok_ = true;
};

}  // namespace

bool decode(const std::string& text, Record& out, std::string* error) {
    std::string problem;
    Json document;
    Record record;
    bool ok = false;

    if (!Json::parse(text, document, &problem)) {
        problem = "not valid JSON: " + problem;
    } else {
        Fields f(document, "", problem);
        std::string version;
        if (f.string("version", version) && version != "OTB-1") {
            f.fail("version", "must be \"OTB-1\"");
        }
        f.timestamp("timestamp", record.timestamp);

        if (const Json* investment = f.get("investment", true)) {
            Fields i(*investment, "investment", problem);
            if (i.string("id", record.investment.id) && !isInvestmentId(record.investment.id)) {
                i.fail("id", "must look like INV-2025-0001");
            }
            i.enumeration("type", kTypes, record.investment.type);
            i.string("item", record.investment.item);
            i.enumeration("status", kStatuses, record.investment.status);
            if (!i.finish()) f.poison();
        }

        if (const Json* financials = f.get("financials", true)) {
            Fields m(*financials, "financials", problem);
            m.number("costUSD", record.financials.costUsd, true, 0.0, kNoMaximum);
            m.string("vendor", record.financials.vendor);
            m.string("fundingSource", record.financials.fundingSource);
            if (!m.finish()) f.poison();
        }

        if (const Json* location = f.get("location", true)) {
            Fields l(*location, "location", problem);
            l.string("site", record.location.site);
            if (const Json* coordinates = l.get("coordinates", false)) {
                Coordinates where;
                Fields c(*coordinates, "location/coordinates", problem);
                c.number("lat", where.lat, true, -90.0, 90.0);
                c.number("lon", where.lon, true, -180.0, 180.0);
                if (c.finish()) record.location.coordinates = where;
                else l.poison();
            }
            if (!l.finish()) f.poison();
        }

        if (const Json* certifications = f.get("certifications", true)) {
            Fields c(*certifications, "certifications", problem);
            c.optionalBoolean("SMESSC_Applicable", record.certifications.smesscApplicable);
            c.boolean("OTB_Audit", record.certifications.otbAudit);
            c.optionalString("ProofOfInvestmentToken", record.certifications.proofOfInvestmentToken);
            if (!c.finish()) f.poison();
        }

        ok = f.finish();
    }

    if (ok) {
        out = std::move(record);
    } else if (error != nullptr) {
        *error = problem.empty() ? "invalid OTB-1 record" : problem;
    }
    return ok;
}

bool validate(const Record& record, std::string* error) {
    // One definition of validity, not two: a record is valid exactly when
    // its encoding is something decode() accepts.
    Record ignored;
    return decode(encode(record), ignored, error);
}

}  // namespace otb1
