# 📜 OTB‑1: Overby Transparency for Business v1
OTB‑1 defines the standard JSON schema for publicly reporting business expenditures, infrastructure projects, and material procurements.

## 🌍 Purpose
This format allows investors, regulators, and the public to transparently verify WHERE funds are spent and WHAT assets are acquired in pursuit of Overby’s roadmap (airfields, hangars, labs, materials).

## ✅ Formal Schema & Validation

This spec is now a real, enforceable [JSON Schema](schema/otb-1.schema.json)
(Draft 2020-12), with the three examples below as runnable files in
[`examples/`](examples/) and a validator in [`tools/validate.py`](tools/validate.py):

```bash
pip install jsonschema
python tools/validate.py                 # validates everything under examples/
python tools/validate.py my-ledger-entry.json
```

CI ([`.github/workflows/validate.yml`](.github/workflows/validate.yml)) runs
this on every push and PR.

## 🧩 Core OTB‑1 JSON Schema
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
## 🛰️ Field Definitions
- `version`: must be `"OTB-1"`.
- `timestamp`: UTC ISO timestamp of purchase/contract execution.
- `investment`: object describing ID, category, item, and current status (`planned`, `in-progress`, `completed`).
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

## 🔗 Certifications & Extensions
- OTB Audit ✅ → Every item logged is audited by Overby internal + optionally external auditors for accuracy.
- Proof of Investment Token (PoI Credit) 🔗 → Each line item issues a blockchain token just like PoC credits.
*Example: `INV-2025-0001` → `0xinv20250001` on ledger.*
- SMESSC Linkage ♻️ → If investment is directly related to mining/facility operations, it inherits SMESSC accountability.
- ODCI Crossover 📊 → If investment leads to orbital capacity increase (like deploying new reclaimer pods), item can cross-credit ODCI transparency.

---

## 🛠️ Example Use Cases
✅ Land Purchase (Airfield)
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
✅ Facility (Lab Build)
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
✅ Materials Procurement
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
## ✅ Why OTB‑1 Matters
- Investor Trust → Every dollar logged, every project trackable.
- Supporter Engagement → Crowdfunding donors literally see “Our money bought X acres of land.”
- Operational Legitimacy → Regulators view Overby as radically transparent.
- ESG Compliance → Aligns with global compliance narratives (carbon credits → orbital credits → now investment credits).
- Twin Ledger with OTF‑1 → Full Overby Transparency Stack:
  - 🌌 OTF‑1 = orbital telemetry trust
  - 🌍 OTB‑1 = Earth business trust
