#include "check.hpp"

#include "json.hpp"

using otb1::Json;

namespace {

bool parses(const std::string& text) {
    Json out;
    return Json::parse(text, out);
}

void test_round_trip_keeps_order_and_values() {
    TEST("a document survives dump -> parse -> dump byte for byte");

    Json doc = Json::object();
    doc.set("version", "OTB-1");
    doc.set("seq", 412);
    doc.set("progress", 0.4);
    doc.set("ok", true);
    doc.set("nothing", Json());
    Json list = Json::array();
    list.push("capture").push("deorbit");
    doc.set("capabilities", std::move(list));
    Json nested = Json::object();
    nested.set("zone", "LEO");
    nested.set("altitude_km", 825);
    doc.set("location", std::move(nested));

    const std::string text = doc.dump();
    CHECK_EQ(text, std::string("{\"version\":\"OTB-1\",\"seq\":412,\"progress\":0.4,\"ok\":true,"
                               "\"nothing\":null,\"capabilities\":[\"capture\",\"deorbit\"],"
                               "\"location\":{\"zone\":\"LEO\",\"altitude_km\":825}}"));

    Json parsed;
    CHECK(Json::parse(text, parsed));
    CHECK_EQ(parsed.dump(), text);
    CHECK(parsed.find("seq") != nullptr && parsed.find("seq")->asNumber() == 412.0);
    CHECK(parsed.find("missing") == nullptr);
}

void test_numbers_are_written_exactly() {
    TEST("numbers read back as the same double, and integers stay integers");

    CHECK_EQ(Json(0).dump(), std::string("0"));
    CHECK_EQ(Json(-17).dump(), std::string("-17"));
    CHECK_EQ(Json(1700000000000LL).dump(), std::string("1700000000000"));
    CHECK_EQ(Json(5.2).dump(), std::string("5.2"));
    CHECK_EQ(Json(51.6).dump(), std::string("51.6"));
    CHECK_EQ(Json(0.1 + 0.2).dump(), std::string("0.30000000000000004"));

    for (double value : {5.2, 51.6, 0.1 + 0.2, 1.0 / 3.0, 1.0e-9, 6.02214076e23, -0.000123}) {
        Json parsed;
        CHECK(Json::parse(Json(value).dump(), parsed));
        CHECK(parsed.isNumber() && parsed.asNumber() == value);
    }
}

void test_strings_escape_and_unescape() {
    TEST("string escapes, control characters and unicode round-trip");

    const std::string awkward = std::string("quote\" slash\\ tab\t newline\n bell") + '\x07' +
                                " caf\xC3\xA9 \xF0\x9F\x9A\x80";
    Json parsed;
    CHECK(Json::parse(Json(awkward).dump(), parsed));
    CHECK_EQ(parsed.asString(), awkward);

    // \u escapes, including a surrogate pair, decode to UTF-8.
    CHECK(Json::parse("\"caf\\u00e9 \\ud83d\\ude80 \\/\"", parsed));
    CHECK_EQ(parsed.asString(), std::string("caf\xC3\xA9 \xF0\x9F\x9A\x80 /"));
}

void test_malformed_input_is_refused() {
    TEST("anything that is not strict JSON is refused, never guessed at");

    CHECK(parses("{\"a\":[1,2,{\"b\":null}]}"));
    CHECK(parses("  [ ]  "));
    CHECK(parses("-0.5e+3"));

    CHECK(!parses(""));
    CHECK(!parses("{"));
    CHECK(!parses("{\"a\":1,}"));          // trailing comma
    CHECK(!parses("[1,2,]"));
    CHECK(!parses("{\"a\" 1}"));           // missing colon
    CHECK(!parses("{a:1}"));               // unquoted key
    CHECK(!parses("{\"a\":1} extra"));     // trailing garbage
    CHECK(!parses("\"unterminated"));
    CHECK(!parses("\"bad \\x escape\""));
    CHECK(!parses("\"raw\ttab\""));        // unescaped control character
    CHECK(!parses("\"\\ud83d alone\""));   // lone high surrogate
    CHECK(!parses("\"\\ude80\""));         // lone low surrogate
    CHECK(!parses("01"));                  // leading zero
    CHECK(!parses("1."));
    CHECK(!parses("+1"));
    CHECK(!parses(".5"));
    CHECK(!parses("1e"));
    CHECK(!parses("1e999"));               // overflows a double
    CHECK(!parses("NaN"));
    CHECK(!parses("tru"));
    CHECK(!parses("nulll"));

    std::string error;
    Json out;
    CHECK(!Json::parse("{\"a\":1,}", out, &error));
    CHECK(!error.empty());
    CHECK(out.isNull());
}

void test_only_well_formed_utf8_is_accepted() {
    TEST("strings must be well-formed UTF-8, with no overlong or surrogate forms");

    // JSON text is UTF-8 by definition. Bytes let through here would be
    // written straight back out by encode() for another parser to choke on.
    CHECK(parses("\"caf\xC3\xA9\""));                    // U+00E9, two bytes
    CHECK(parses("\"\xE2\x82\xAC\""));                   // U+20AC, three bytes
    CHECK(parses("\"\xF0\x9F\x9A\x80\""));               // U+1F680, four bytes
    CHECK(parses("\"\xF4\x8F\xBF\xBF\""));               // U+10FFFF, the last one
    CHECK(parses("{\"\xC3\xA9\":1}"));                   // in a key too

    CHECK(!parses("\"\x80\""));                          // continuation with no lead
    CHECK(!parses("\"\xC3\""));                          // lead with nothing after it
    CHECK(!parses("\"\xE2\x82\""));                      // truncated three-byte form
    CHECK(!parses("\"\xC3(\""));                         // lead followed by ASCII
    CHECK(!parses("\"\xC0\xAF\""));                      // overlong '/'
    CHECK(!parses("\"\xE0\x80\xAF\""));                  // overlong, three bytes
    CHECK(!parses("\"\xF0\x80\x80\xAF\""));              // overlong, four bytes
    CHECK(!parses("\"\xED\xA0\x80\""));                  // an encoded surrogate
    CHECK(!parses("\"\xF4\x90\x80\x80\""));              // above U+10FFFF
    CHECK(!parses("\"\xF8\x88\x80\x80\x80\""));          // five-byte form
    CHECK(!parses("\"\xFF\""));
    CHECK(!parses("{\"\xFF\":1}"));
}

void test_duplicate_keys_are_refused() {
    TEST("a duplicated key is an error, not a silent pick of one value");

    // Two parsers that disagree on which duplicate wins would disagree on
    // what a record says. Refusing the record removes the question.
    CHECK(!parses("{\"seq\":1,\"seq\":2}"));
}

void test_depth_is_bounded() {
    TEST("deeply nested input is refused rather than recursed into");

    CHECK(parses(std::string(20, '[') + std::string(20, ']')));
    CHECK(!parses(std::string(5000, '[') + std::string(5000, ']')));
    CHECK(!parses(std::string(100000, '[')));
}

}  // namespace

void runJsonTests() {
    std::printf("json\n");
    test_round_trip_keeps_order_and_values();
    test_numbers_are_written_exactly();
    test_strings_escape_and_unescape();
    test_malformed_input_is_refused();
    test_only_well_formed_utf8_is_accepted();
    test_duplicate_keys_are_refused();
    test_depth_is_bounded();
}
