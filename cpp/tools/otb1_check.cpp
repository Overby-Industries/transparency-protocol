// otb1_check -- validate OTB-1 records with the C++ reference decoder.
//
// The C++ counterpart of tools/validate.py, which validates with the JSON
// Schema. Two independent implementations of the same format are only worth
// having if they are held to agreeing, so this tool has two jobs:
//
//   otb1_check [--expect-invalid] FILE...
//       Check files by hand. A .json file holds one record; a .jsonl file
//       holds one record per line. Output and exit status match
//       tools/validate.py.
//
//   otb1_check --stream
//   otb1_check --stream-json
//       Machine mode, for tools/differential.py. Reads one hex-encoded
//       input per line on stdin and writes one verdict per line on stdout:
//           ok <hex of the record re-encoded>
//           fail <reason>
//       --stream decodes each input as an OTB-1 record. --stream-json only
//       parses it as JSON, which is how the parser itself is fuzzed. Inputs
//       are hex so that arbitrary bytes, newlines included, survive a
//       line-oriented pipe.

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "json.hpp"
#include "otb1/otb1.hpp"

namespace {

std::string toHex(const std::string& bytes) {
    static const char* kDigits = "0123456789abcdef";
    std::string hex;
    hex.reserve(bytes.size() * 2);
    for (const char raw : bytes) {
        const unsigned char c = static_cast<unsigned char>(raw);
        hex += kDigits[c >> 4];
        hex += kDigits[c & 0x0F];
    }
    return hex;
}

bool fromHex(const std::string& hex, std::string& bytes) {
    if (hex.size() % 2 != 0) return false;
    bytes.clear();
    bytes.reserve(hex.size() / 2);
    const auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const int high = digit(hex[i]);
        const int low = digit(hex[i + 1]);
        if (high < 0 || low < 0) return false;
        bytes += static_cast<char>((high << 4) | low);
    }
    return true;
}

std::string oneLine(std::string text) {
    for (char& c : text) {
        if (c == '\n' || c == '\r') c = ' ';
    }
    return text;
}

int stream(bool jsonOnly) {
    std::string line;
    std::string input;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!fromHex(line, input)) {
            std::cout << "fail input line is not hex\n";
            continue;
        }
        std::string why;
        if (jsonOnly) {
            otb1::Json document;
            if (otb1::Json::parse(input, document, &why)) {
                std::cout << "ok " << toHex(document.dump()) << '\n';
            } else {
                std::cout << "fail " << oneLine(why) << '\n';
            }
        } else {
            otb1::Record record;
            if (otb1::decode(input, record, &why)) {
                std::cout << "ok " << toHex(otb1::encode(record)) << '\n';
            } else {
                std::cout << "fail " << oneLine(why) << '\n';
            }
        }
    }
    return 0;
}

bool endsWith(const std::string& text, const char* suffix) {
    const std::size_t length = std::strlen(suffix);
    return text.size() >= length && text.compare(text.size() - length, length, suffix) == 0;
}

struct Tally {
    int checked = 0;
    int failures = 0;
};

void checkOne(const std::string& label, const std::string& text, bool expectInvalid, bool quiet,
              Tally& tally) {
    ++tally.checked;
    otb1::Record record;
    std::string why;
    const bool valid = otb1::decode(text, record, &why);
    if (expectInvalid) {
        if (!valid) {
            if (!quiet) std::printf("OK   %s  (rejected, as it should be)\n", label.c_str());
        } else {
            ++tally.failures;
            std::printf("FAIL %s  (accepted, but must be rejected)\n", label.c_str());
        }
    } else if (!valid) {
        ++tally.failures;
        std::printf("FAIL %s\n  %s\n", label.c_str(), why.c_str());
    } else if (!quiet) {
        std::printf("OK   %s\n", label.c_str());
    }
}

int checkFiles(const std::vector<std::string>& paths, bool expectInvalid) {
    Tally tally;
    for (const std::string& path : paths) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            std::fprintf(stderr, "cannot read %s\n", path.c_str());
            return 2;
        }
        if (endsWith(path, ".jsonl")) {
            // A ledger file can hold thousands of records: one line per file unless
            // something is wrong.
            const int before = tally.failures;
            int inFile = 0;
            int number = 0;
            std::string line;
            while (std::getline(in, line)) {
                ++number;
                if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
                ++inFile;
                checkOne(path + ":" + std::to_string(number), line, expectInvalid, true, tally);
            }
            if (tally.failures == before) {
                std::printf("OK   %s  (%d records)\n", path.c_str(), inFile);
            }
        } else {
            std::ostringstream buffer;
            buffer << in.rdbuf();
            checkOne(path, buffer.str(), expectInvalid, false, tally);
        }
    }

    if (tally.failures > 0) {
        std::printf("\n%d of %d record(s) failed the check.\n", tally.failures, tally.checked);
        return 1;
    }
    if (expectInvalid) {
        std::printf("\nAll %d record(s) rejected by OTB-1, as expected.\n", tally.checked);
    } else {
        std::printf("\nAll %d record(s) valid against OTB-1.\n", tally.checked);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    bool expectInvalid = false;
    std::vector<std::string> paths;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--stream") return stream(false);
        if (arg == "--stream-json") return stream(true);
        if (arg == "--expect-invalid") {
            expectInvalid = true;
        } else if (!arg.empty() && arg[0] == '-') {
            std::fprintf(stderr, "unknown option %s\n", arg.c_str());
            return 2;
        } else {
            paths.push_back(arg);
        }
    }
    if (paths.empty()) {
        std::fprintf(stderr,
                     "usage: %s [--expect-invalid] FILE...\n"
                     "       %s --stream | --stream-json   (hex lines on stdin)\n",
                     argv[0], argv[0]);
        return 2;
    }
    return checkFiles(paths, expectInvalid);
}
