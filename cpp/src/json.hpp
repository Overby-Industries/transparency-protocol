#pragma once

// A minimal JSON document model -- just enough to read and write OTB-1
// records with no external dependency. A transparency format whose
// reference implementation needs a package manager to parse it is one fewer
// people will check for themselves.
//
// Private to libotb1: the public interface is otb1/otb1.hpp, which deals in
// typed records and never exposes a JSON value.
//
// Deliberately strict, because a published record is read by parties who do
// not trust each other. The parser accepts exactly RFC 8259 and nothing
// else: no trailing commas, no bare control characters, no lone surrogates,
// no invalid UTF-8. It rejects duplicate keys rather than silently keeping
// one -- two readers that disagreed on which duplicate wins would disagree
// on what a record says -- and it bounds nesting depth.
//
// This is the same parser as libotf1's in the telemetry-protocol repository,
// kept as a copy so that this repository builds and is tested on its own.
// tools/differential.py fuzzes this copy against a strict reference here.

#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace otb1 {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    using Array = std::vector<Json>;
    using Member = std::pair<std::string, Json>;
    // A vector, not a map: members keep the order they were written in, so
    // an encoded record reads in the same order as the examples in the spec
    // and two runs of the same simulation produce byte-identical logs.
    using Object = std::vector<Member>;

    Json() = default;  // null
    Json(bool value) : type_(Type::Bool), bool_(value) {}
    Json(const char* value) : type_(Type::String), string_(value) {}
    Json(std::string value) : type_(Type::String), string_(std::move(value)) {}
    template <typename T,
              typename = std::enable_if_t<std::is_arithmetic<T>::value &&
                                          !std::is_same<T, bool>::value>>
    Json(T value) : type_(Type::Number), number_(static_cast<double>(value)) {}

    static Json array() { Json j; j.type_ = Type::Array; return j; }
    static Json object() { Json j; j.type_ = Type::Object; return j; }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    // Accessors return a neutral value when the type does not match, so a
    // caller that forgot to check cannot read uninitialised state. Callers
    // that care (the OTB-1 decoder does) check the type first.
    bool asBool() const { return bool_; }
    double asNumber() const { return number_; }
    const std::string& asString() const { return string_; }
    const Array& items() const { return array_; }
    const Object& members() const { return object_; }

    // Append to an array. Returns *this so encoders can chain.
    Json& push(Json value);
    // Add a member to an object, replacing an existing one with the same key.
    Json& set(const std::string& key, Json value);
    // Look up a member of an object; nullptr if absent or not an object.
    const Json* find(const std::string& key) const;

    // Compact, deterministic serialisation: no whitespace, members in
    // insertion order, integral numbers written without a decimal point.
    std::string dump() const;

    // Parse exactly one JSON value spanning the whole of `text`. On failure
    // returns false, leaves `out` null, and describes the problem in `error`.
    static bool parse(const std::string& text, Json& out, std::string* error = nullptr);

private:
    void dumpTo(std::string& out) const;

    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    Array array_;
    Object object_;
};

}  // namespace otb1
