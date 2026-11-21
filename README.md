# OTB‑1: Overby Transparency for Business v1

[![Version](https://img.shields.io/badge/Version-OTB--1-blue?style=for-the-badge&logo=github)](schema/otb-1.schema.json)
[![Validate](https://img.shields.io/github/actions/workflow/status/Overby-Industries/transparency-protocol/validate.yml?branch=main&style=for-the-badge&label=Validate&logo=githubactions&logoColor=white)](https://github.com/Overby-Industries/transparency-protocol/actions/workflows/validate.yml)
[![C++](https://img.shields.io/github/actions/workflow/status/Overby-Industries/transparency-protocol/cpp.yml?branch=main&style=for-the-badge&label=C%2B%2B%2017&logo=cplusplus&logoColor=white)](https://github.com/Overby-Industries/transparency-protocol/actions/workflows/cpp.yml)
[![License](https://img.shields.io/badge/License-Apache_2.0-lightgrey?style=for-the-badge&logo=apache)](LICENSE)
[![JSON Schema](https://img.shields.io/badge/JSON_Schema-Draft_2020--12-purple?style=for-the-badge&logo=json)](https://json-schema.org/draft/2020-12/release-notes)
[![Certification](https://img.shields.io/badge/Certification-OTB_Audit_%7C_PoI-green?style=for-the-badge)](#certifications--extensions)
[![Status](https://img.shields.io/badge/Status-Draft-orange?style=for-the-badge)](#formal-schema--validation)

**Overby Industries – Business Transparency Protocol Specification**

OTB‑1 defines the standard JSON schema for publicly reporting business expenditures, infrastructure projects, and material procurements.

## Purpose
This format allows investors, regulators, and the public to transparently verify WHERE funds are spent and WHAT assets are acquired in pursuit of Overby’s roadmap (airfields, hangars, labs, materials).

## Formal Schema & Validation

This spec is now a real, enforceable [JSON Schema](schema/otb-1.schema.json)
(Draft 2020-12), with the three examples below as runnable files in
[`examples/`](examples/) and a validator in [`tools/validate.py`](tools/validate.py):

```bash
pip install jsonschema
python tools/validate.py                 # validates everything under examples/
python tools/validate.py my-ledger-entry.json
python tools/validate.py ledger-2025.jsonl               # a ledger file: one record per line
python tools/validate.py --expect-invalid tests/invalid  # every one must be rejected
```

[`tests/invalid/`](tests/invalid/) holds one malformed record per file, each
broken in exactly one way. CI
([`.github/workflows/validate.yml`](.github/workflows/validate.yml)) runs
both checks on every push and PR, so a schema change that breaks a good
example, or that quietly starts accepting a bad record, gets caught before
it merges.

The validator checks two things a JSON Schema validator usually leaves
alone. A timestamp must be a real instant, so `2025-02-30T00:00:00Z` is
rejected. And an investment id must match its pattern to the very end of
the string, so `INV-2025-0001` followed by a stray newline is rejected too.

## C++ Reference Implementation

[`cpp/`](cpp/) holds **libotb1**: the same format as a C++ value, with two
functions that turn it into bytes and back. C++17, no dependencies beyond
the standard library.

```cpp
#include <otb1/otb1.hpp>

otb1::Record record;
record.timestamp = 1741960800000;               // 2025-03-14T14:00:00Z
record.investment = {"INV-2025-0001", otb1::InvestmentType::Land,
                     "50 acres for Overby Airfield Development",
                     otb1::InvestmentStatus::Completed};
record.financials = {2200000.0, "AeroLand Realty LLC", "Early Supporter Funds"};
record.location.site = "West Texas";
record.certifications.otbAudit = true;

std::string why;
if (!otb1::validate(record, &why)) {
    // Not publishable. `why` names the field, for example
    // "investment/id must look like INV-2025-0001".
}
std::string text = otb1::encode(record);        // one line of compact JSON

otb1::Record read;
if (otb1::decode(text, read, &why)) {
    // read.financials.costUsd, read.certifications.otbAudit, ...
}
```

`decode()` applies the schema's rules in C++: required fields, closed enums,
value ranges, the investment id's form, real timestamps, no unknown fields.
A program built on it never publishes or acts on a record the schema would
reject.

An optional field that was left out stays left out. A record that does not
say whether SMESSC applies is not read back as saying it does not.

**Build and test:**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

**Use it from another CMake project:**

```cmake
add_subdirectory(path/to/transparency-protocol)
target_link_libraries(your_target PRIVATE otb1::otb1)
```

**Check records with the C++ decoder** instead of the schema:

```bash
./build/otb1_check examples/*.json
./build/otb1_check ledger-2025.jsonl
./build/otb1_check --expect-invalid tests/invalid/*.json
```

### Two implementations, held to one answer

The schema and the C++ decoder are two descriptions of the same format, and
two descriptions drift unless something forces them together. Three things
do:

- **One corpus.** The C++ tests run over this repository's own `examples/`
  and `tests/invalid/`, the same files the schema is validated against.
- **Differential testing.** [`tools/differential.py`](tools/differential.py)
  mutates the example records about 8,800 ways (a field dropped, a field
  added, a value swapped for another type, another enum value, a boundary
  number, a malformed id or timestamp) and requires the schema and the C++
  decoder to give the same accept-or-reject verdict on every one. For each
  record both accept, the C++ re-encoding must say exactly what the input
  said: a cost that came back rounded would be a different claim. It then
  mutates JSON text at the byte level, about 46,000 inputs, and requires
  the C++ parser to accept exactly what a strict RFC 8259 reader accepts.
- **CI runs all of it** on every push and pull request, and builds the
  library on Linux and Windows.

```bash
python tools/differential.py        # needs build/otb1_check
```

## Core OTB‑1 JSON Schema
```json
{
  "version": "OTB-1",
  "timestamp": "2025-03-14T14:00:00Z",
  "investment": {
    "id": "INV-2025-0001",
    "type": "land",
    "item": "50 acres for Overby Airfield Development",
    "status": "completed"
  },
  "financials": {
    "costUSD": 2200000,
    "vendor": "AeroLand Realty LLC",
    "fundingSource": "Early Supporter Funds"
  },
  "location": {
    "site": "West Texas",
    "coordinates": {
      "lat": 31.9686,
      "lon": -99.9018
    }
  },
  "certifications": {
    "SMESSC_Applicable": false,
    "OTB_Audit": true,
    "ProofOfInvestmentToken": "0xinv20250001"
  }
}
```
## Field Definitions
- `version`: must be `"OTB-1"`.
- `timestamp`: UTC ISO timestamp of purchase/contract execution.
- `investment`: object describing ID, category, item, and current status (`planned`, `in-progress`, `procurement`, `completed`).
- `financials`:
  - `costUSD` → total cost
  - `vendor` → vendor/dealer/contractor
  - `fundingSource` → "Donations", "Capital Round A", etc.
- `location`:
  - `site` → description/string
  - `coordinates` → optional Lat/Lon.
- `certifications`:
  - `OTB_Audit` flag → if entry has been verified by Overby auditors.
  - `ProofOfInvestmentToken` → immutable blockchain token proving this item exists in ledger.
  - `SMESSC_Applicable` → flag if investment links to operational sustainability certification.

## Certifications & Extensions
- OTB Audit → Every item logged is audited by Overby internal + optionally external auditors for accuracy.
- Proof of Investment Token (PoI Credit) → Each line item issues a blockchain token just like PoC credits.
*Example: `INV-2025-0001` → `0xinv20250001` on ledger.*
- SMESSC Linkage → If investment is directly related to mining/facility operations, it inherits SMESSC accountability.
- ODCI Crossover → If investment leads to orbital capacity increase (like deploying new reclaimer pods), item can cross-credit ODCI transparency.

---

## Example Use Cases
Land Purchase (Airfield)
```json
{
  "version": "OTB-1",
  "timestamp": "2025-03-14T14:00:00Z",
  "investment": { "id": "INV-2025-0001", "type": "land", "item": "50 acres", "status": "completed" },
  "financials": { "costUSD": 2200000, "vendor": "AeroLand Realty LLC", "fundingSource": "Donations" },
  "location": { "site": "West Texas" },
  "certifications": { "OTB_Audit": true, "ProofOfInvestmentToken": "0xinv20250001" }
}
```
Facility (Lab Build)
```json
{
  "version": "OTB-1",
  "timestamp": "2025-04-02T17:30:00Z",
  "investment": {
    "id": "INV-2025-0002",
    "type": "facility",
    "item": "Advanced R&D Manufacturing Lab, Phase 1",
    "status": "in-progress"
  },
  "financials": {
    "costUSD": 15000000,
    "vendor": "Stellar Construction Group",
    "fundingSource": "Seed Investment A"
  },
  "location": { "site": "Houston Space Tech Park" },
  "certifications": {
    "OTB_Audit": true,
    "ProofOfInvestmentToken": "0xinv20250002"
  }
}
```
Materials Procurement
```json
{
  "version": "OTB-1",
  "timestamp": "2025-04-20T12:00:00Z",
  "investment": {
    "id": "INV-2025-0003",
    "type": "materials",
    "item": "100 tons aerospace-grade steel feedstock",
    "status": "procurement"
  },
  "financials": {
    "costUSD": 750000,
    "vendor": "StarMetals Incorporated",
    "fundingSource": "Supporter Pool B"
  },
  "location": { "site": "Gulf Coast Depot" },
  "certifications": {
    "OTB_Audit": true,
    "ProofOfInvestmentToken": "0xinv20250003"
  }
}
```
## Why OTB‑1 Matters
- Investor Trust → Every dollar logged, every project trackable.
- Supporter Engagement → Crowdfunding donors literally see “Our money bought X acres of land.”
- Operational Legitimacy → Regulators view Overby as radically transparent.
- ESG Compliance → Aligns with global compliance narratives (carbon credits → orbital credits → now investment credits).
- Twin Ledger with OTF‑1 → Full Overby Transparency Stack:
  - OTF‑1 = orbital telemetry trust
  - OTB‑1 = Earth business trust
