#include "json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace otb1 {

Json& Json::push(Json value) {
    if (type_ != Type::Array) {
        *this = Json::array();
    }
    array_.push_back(std::move(value));
    return *this;
}

Json& Json::set(const std::string& key, Json value) {
    if (type_ != Type::Object) {
        *this = Json::object();
    }
    for (Member& member : object_) {
        if (member.first == key) {
            member.second = std::move(value);
            return *this;
        }
    }
    object_.emplace_back(key, std::move(value));
    return *this;
}

const Json* Json::find(const std::string& key) const {
    if (type_ != Type::Object) {
        return nullptr;
    }
    for (const Member& member : object_) {
        if (member.first == key) {
            return &member.second;
        }
    }
    return nullptr;
}

namespace {

void appendString(std::string& out, const std::string& text) {
    out += '"';
    for (const char raw : text) {
        const unsigned char c = static_cast<unsigned char>(raw);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(c));
                    out += buf;
                } else {
                    out += static_cast<char>(c);  // UTF-8 passes through untouched
                }
        }
    }
    out += '"';
}

void appendNumber(std::string& out, double value) {
    if (!std::isfinite(value)) {
        out += "null";  // JSON has no NaN or infinity
        return;
    }
    char buf[40];
    if (value == std::floor(value) && std::fabs(value) < 9.0e15) {
        // Sequence numbers and millisecond timestamps are integers; write
        // them as integers so a schema's "type": "integer" is plainly met.
        std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(value));
    } else {
        // Shortest representation that reads back as exactly the same double.
        for (int precision = 15; precision <= 17; ++precision) {
            std::snprintf(buf, sizeof buf, "%.*g", precision, value);
            if (std::strtod(buf, nullptr) == value) {
                break;
            }
        }
    }
    out += buf;
}

}  // namespace

void Json::dumpTo(std::string& out) const {
    switch (type_) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += bool_ ? "true" : "false"; break;
        case Type::Number: appendNumber(out, number_); break;
        case Type::String: appendString(out, string_); break;
        case Type::Array: {
            out += '[';
            bool first = true;
            for (const Json& item : array_) {
                if (!first) out += ',';
                first = false;
                item.dumpTo(out);
            }
            out += ']';
            break;
        }
        case Type::Object: {
            out += '{';
            bool first = true;
            for (const Member& member : object_) {
                if (!first) out += ',';
                first = false;
                appendString(out, member.first);
                out += ':';
                member.second.dumpTo(out);
            }
            out += '}';
            break;
        }
    }
}

std::string Json::dump() const {
    std::string out;
    dumpTo(out);
    return out;
}

namespace {

// Nesting deeper than this is refused. OTB-1 records nest three levels; the
// bound exists so a hostile or corrupted record cannot overflow the stack.
constexpr int kMaxDepth = 32;

class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    bool run(Json& out, std::string* error) {
        Json value;
        skipWhitespace();
        bool ok = parseValue(value, 0);
        if (ok) {
            skipWhitespace();
            if (pos_ != text_.size()) {
                ok = fail("unexpected characters after the JSON value");
            }
        }
        if (!ok) {
            if (error != nullptr) {
                *error = error_ + " at offset " + std::to_string(pos_);
            }
            out = Json();
            return false;
        }
        out = std::move(value);
        return true;
    }

private:
    bool fail(const char* message) {
        if (error_.empty()) {
            error_ = message;
        }
        return false;
    }

    bool atEnd() const { return pos_ >= text_.size(); }
    char peek() const { return text_[pos_]; }

    void skipWhitespace() {
        while (!atEnd()) {
            char c = peek();
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
            ++pos_;
        }
    }

    bool consumeWord(const char* word) {
        std::size_t length = 0;
        while (word[length] != '\0') ++length;
        if (text_.compare(pos_, length, word) != 0) {
            return fail("unrecognised literal");
        }
        pos_ += length;
        return true;
    }

    bool parseValue(Json& out, int depth) {
        if (depth > kMaxDepth) return fail("nesting too deep");
        if (atEnd()) return fail("unexpected end of input");
        switch (peek()) {
            case '{': return parseObject(out, depth);
            case '[': return parseArray(out, depth);
            case '"': {
                std::string text;
                if (!parseString(text)) return false;
                out = Json(std::move(text));
                return true;
            }
            case 't':
                if (!consumeWord("true")) return false;
                out = Json(true);
                return true;
            case 'f':
                if (!consumeWord("false")) return false;
                out = Json(false);
                return true;
            case 'n':
                if (!consumeWord("null")) return false;
                out = Json();
                return true;
            default:
                return parseNumber(out);
        }
    }

    bool parseObject(Json& out, int depth) {
        ++pos_;  // '{'
        Json object = Json::object();
        skipWhitespace();
        if (!atEnd() && peek() == '}') {
            ++pos_;
            out = std::move(object);
            return true;
        }
        for (;;) {
            skipWhitespace();
            if (atEnd() || peek() != '"') return fail("expected a string key");
            std::string key;
            if (!parseString(key)) return false;
            if (object.find(key) != nullptr) return fail("duplicate object key");
            skipWhitespace();
            if (atEnd() || peek() != ':') return fail("expected ':' after object key");
            ++pos_;
            skipWhitespace();
            Json value;
            if (!parseValue(value, depth + 1)) return false;
            object.set(key, std::move(value));
            skipWhitespace();
            if (atEnd()) return fail("unterminated object");
            if (peek() == ',') { ++pos_; continue; }
            if (peek() == '}') { ++pos_; break; }
            return fail("expected ',' or '}' in object");
        }
        out = std::move(object);
        return true;
    }

    bool parseArray(Json& out, int depth) {
        ++pos_;  // '['
        Json array = Json::array();
        skipWhitespace();
        if (!atEnd() && peek() == ']') {
            ++pos_;
            out = std::move(array);
            return true;
        }
        for (;;) {
            skipWhitespace();
            Json value;
            if (!parseValue(value, depth + 1)) return false;
            array.push(std::move(value));
            skipWhitespace();
            if (atEnd()) return fail("unterminated array");
            if (peek() == ',') { ++pos_; continue; }
            if (peek() == ']') { ++pos_; break; }
            return fail("expected ',' or ']' in array");
        }
        out = std::move(array);
        return true;
    }

    bool parseHex4(unsigned& out) {
        if (pos_ + 4 > text_.size()) return fail("truncated \\u escape");
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
            char c = text_[pos_++];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned>(c - 'A' + 10);
            else return fail("invalid \\u escape");
        }
        out = value;
        return true;
    }

    static void appendUtf8(std::string& out, unsigned codepoint) {
        if (codepoint < 0x80) {
            out += static_cast<char>(codepoint);
        } else if (codepoint < 0x800) {
            out += static_cast<char>(0xC0 | (codepoint >> 6));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else if (codepoint < 0x10000) {
            out += static_cast<char>(0xE0 | (codepoint >> 12));
            out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (codepoint >> 18));
            out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
    }

    // Copies one multi-byte UTF-8 sequence starting at pos_, or fails. Only
    // well-formed sequences pass (Unicode Table 3-7): no overlong forms, no
    // encoded surrogates, nothing above U+10FFFF. JSON text is UTF-8 by
    // definition, and bytes let through here would be written back out by
    // encode() for some other parser to choke on.
    bool copyUtf8Sequence(std::string& out) {
        const auto byteAt = [this](std::size_t offset) -> unsigned {
            return pos_ + offset < text_.size()
                       ? static_cast<unsigned char>(text_[pos_ + offset])
                       : 0u;  // 0 is never a valid continuation byte
        };
        const unsigned lead = byteAt(0);
        std::size_t length = 0;
        unsigned low = 0x80;   // allowed range of the second byte
        unsigned high = 0xBF;
        if (lead >= 0xC2 && lead <= 0xDF) {
            length = 2;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            length = 3;
            if (lead == 0xE0) low = 0xA0;   // otherwise overlong
            if (lead == 0xED) high = 0x9F;  // otherwise a surrogate
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            length = 4;
            if (lead == 0xF0) low = 0x90;   // otherwise overlong
            if (lead == 0xF4) high = 0x8F;  // otherwise above U+10FFFF
        } else {
            return fail("invalid UTF-8 in string");
        }
        if (byteAt(1) < low || byteAt(1) > high) return fail("invalid UTF-8 in string");
        for (std::size_t i = 2; i < length; ++i) {
            if (byteAt(i) < 0x80 || byteAt(i) > 0xBF) return fail("invalid UTF-8 in string");
        }
        out.append(text_, pos_, length);
        pos_ += length;
        return true;
    }

    bool parseString(std::string& out) {
        ++pos_;  // opening quote
        for (;;) {
            if (atEnd()) return fail("unterminated string");
            unsigned char c = static_cast<unsigned char>(text_[pos_++]);
            if (c == '"') return true;
            if (c < 0x20) return fail("unescaped control character in string");
            if (c >= 0x80) {
                --pos_;
                if (!copyUtf8Sequence(out)) return false;
                continue;
            }
            if (c != '\\') {
                out += static_cast<char>(c);
                continue;
            }
            if (atEnd()) return fail("unterminated escape");
            char escape = text_[pos_++];
            switch (escape) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned unit = 0;
                    if (!parseHex4(unit)) return false;
                    if (unit >= 0xDC00 && unit <= 0xDFFF) {
                        return fail("lone low surrogate in \\u escape");
                    }
                    if (unit >= 0xD800 && unit <= 0xDBFF) {
                        if (pos_ + 2 > text_.size() || text_[pos_] != '\\' || text_[pos_ + 1] != 'u') {
                            return fail("lone high surrogate in \\u escape");
                        }
                        pos_ += 2;
                        unsigned low = 0;
                        if (!parseHex4(low)) return false;
                        if (low < 0xDC00 || low > 0xDFFF) {
                            return fail("invalid surrogate pair in \\u escape");
                        }
                        unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                    }
                    appendUtf8(out, unit);
                    break;
                }
                default:
                    return fail("invalid escape sequence");
            }
        }
    }

    bool isDigit() const { return !atEnd() && peek() >= '0' && peek() <= '9'; }

    bool parseNumber(Json& out) {
        const std::size_t start = pos_;
        if (!atEnd() && peek() == '-') ++pos_;
        if (!isDigit()) {
            pos_ = start;
            return fail("unexpected character");
        }
        if (peek() == '0') {
            ++pos_;  // a leading zero stands alone: "01" is not JSON
        } else {
            while (isDigit()) ++pos_;
        }
        if (!atEnd() && peek() == '.') {
            ++pos_;
            if (!isDigit()) return fail("expected digits after decimal point");
            while (isDigit()) ++pos_;
        }
        if (!atEnd() && (peek() == 'e' || peek() == 'E')) {
            ++pos_;
            if (!atEnd() && (peek() == '+' || peek() == '-')) ++pos_;
            if (!isDigit()) return fail("expected digits in exponent");
            while (isDigit()) ++pos_;
        }
        const std::string token = text_.substr(start, pos_ - start);
        const double value = std::strtod(token.c_str(), nullptr);
        if (!std::isfinite(value)) return fail("number out of range");
        out = Json(value);
        return true;
    }

    const std::string& text_;
    std::size_t pos_ = 0;
    std::string error_;
};

}  // namespace

bool Json::parse(const std::string& text, Json& out, std::string* error) {
    return Parser(text).run(out, error);
}

}  // namespace otb1
