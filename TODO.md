# TODO

This extension exposes DuckDB scalar functions (and one table macro,
`trino_meta()`) whose semantics are exact-equivalent to the matching Trino
built-ins, so that predicate pushdown from Trino to DuckDB through the
[ducklake-integrations](https://github.com/brikk/ducklake-integrations)
connector is lossless on non-ASCII / corner-case input where DuckDB's own
built-ins diverge.

## Done

- ✅ Native ICU-backed `trino_lower` / `trino_upper` / `trino_reverse`
  (root-locale full case folding, code-point reverse).
- ✅ `~86` `trino_<name>` macros registered as native `DefaultMacro[]` via
  `DefaultFunctionGenerator::CreateInternalMacroInfo` — no SQL parse loop
  at load time (matches `json` extension and Query-farm's `clickhouse-sql`).
- ✅ `trino_meta()` table macro as `DefaultTableMacro[]` — authoritative
  pushable-set catalog the connector mirrors in
  `DuckDbExpressionTranslator.PUSHABLE_FUNCTIONS`.
- ✅ ICU best-effort INSTALL/LOAD on extension load so `trino_with_timezone`
  resolves DuckDB's `timezone()`.
- ✅ Linux build container (`make linux-arm64`, `make linux-amd64`) for
  cross-platform binaries when developing on macOS.

## Open

### 1. CI build matrix for releases

The upstream extension template ships `MainDistributionPipeline.yml` which
calls `extension-ci-tools`' `_extension_distribution.yml` reusable workflow.
That builds and signs binaries for every standard DuckDB platform
(linux-amd64, linux-arm64, osx-arm64, osx-amd64, windows-amd64) and uploads
them as workflow artifacts.

Local-dev story works today (Docker build container + per-platform gradle
bundling). For releases we want:

- Drop bundled binaries from the artifacts dir on PR / push to `main`.
- Tag-triggered job that also publishes a community-extensions catalog entry
  (see item 2).
- The trino-ducklake plugin jar in CI consumes the freshly-built binaries
  via the same `build/<platform>/release/extension/trino_parity/`
  layout the local Docker targets produce.

### 2. Publish to DuckDB community-extensions

Path to `INSTALL trino_parity FROM community; LOAD trino_parity;` —
zero-binary-management for operators.

- Add a `description.yml` (per
  https://duckdb.org/community_extensions/documentation).
- Submit a PR to https://github.com/duckdb/community-extensions adding our
  description.
- After acceptance, DuckDB CI builds and serves signed binaries from
  `https://community-extensions.duckdb.org/...`.
- The connector then drops the bundled binaries in favour of `INSTALL ...
  FROM community` at attach time. No more 36MB-per-platform in the plugin jar.

### 3. `trino_normalize/{1,2}` for NFC/NFD/NFKC/NFKD

DuckDB only ships `nfc_normalize`. ICU's `unorm2_normalize` covers all four
forms. Add a native scalar function alongside `trino_lower`/`upper`/`reverse`;
add a `trino_normalize` row to `trino_meta()` and to the connector's
`PUSHABLE_FUNCTIONS`. Pin under the Unicode corpus in `REPORT-string-unicode-audit.md`.

### 4. Native trim family

`trino_trim` / `trino_ltrim` / `trino_rtrim` currently macro-wrap DuckDB's
`trim(s, chars)` with a hand-rolled `Character.isWhitespace` charset string.
Moving them to native C++ (iterate code points, skip Java-whitespace
codepoints from both ends) would be marginally faster and avoid the risk of
drifting from Java's whitespace definition. Not blocking — the macro form is
correct today.

### 5. Migrate `from_hex` / `unhex` rounds + remaining macros

Some round-6 entries are still SQL macros (`trino_from_hex` calls DuckDB's
`unhex`; multi-arg overload macros are stacked). Consolidate into single
named overloads, audit for unused entries.
