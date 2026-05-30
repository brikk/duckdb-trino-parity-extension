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
- ✅ Native ICU-backed `trino_trim` / `trino_ltrim` / `trino_rtrim` via
  `u_isWhitespace` (Java `Character.isWhitespace` semantics — NBSP /
  FIGURE / NARROW NBSP intentionally NOT whitespace).
- ✅ Native `trino_normalize/{1,2}` via `icu::Normalizer2` instances
  (NFC default; 2-arg accepts NFC/NFD/NFKC/NFKD case-insensitively).
- ✅ `~80` `trino_<name>` macros registered as native `DefaultMacro[]` via
  `DefaultFunctionGenerator::CreateInternalMacroInfo` — no SQL parse loop
  at load time (matches `json` extension and Query-farm's `clickhouse-sql`).
- ✅ `trino_meta()` table macro as `DefaultTableMacro[]` — authoritative
  pushable-set catalog (93 entries) the connector mirrors in
  `DuckDbExpressionTranslator.PUSHABLE_FUNCTIONS`.
- ✅ ICU best-effort INSTALL/LOAD on extension load so `trino_with_timezone`
  resolves DuckDB's `timezone()`.
- ✅ Linux build container (`make linux-arm64`, `make linux-amd64`) for
  cross-platform binaries when developing on macOS.
- ✅ CI-artifact fallback script
  ([`scripts/fetch-from-ci-artifacts.sh`](scripts/fetch-from-ci-artifacts.sh))
  for pulling platform builds without running the local container.

## Open

### 1. CI build matrix for releases

The upstream extension template ships `MainDistributionPipeline.yml` which
calls `extension-ci-tools`' `_extension_distribution.yml` reusable workflow.
That builds and signs binaries for every standard DuckDB platform
(linux-amd64, linux-arm64, osx-arm64, osx-amd64, windows-amd64) and uploads
them as workflow artifacts.

Local-dev story works today (Docker build container + per-platform gradle
bundling + the fetch-from-CI script). For releases we want:

- Stable artifact naming so consumers can pin tags.
- Tag-triggered job that also publishes a community-extensions catalog entry
  (see item 2).
- The trino-ducklake plugin jar in CI consumes the freshly-built binaries
  via the same `build/<platform>/release/extension/trino_parity/`
  layout the local Docker targets produce.

Known CI failure mode to chase: an earlier run hit a `multiple definition
of duckdb::BufferedFileWriter::DEFAULT_OPEN_FLAGS` link error in DuckDB's
own `tools/plan_serializer` under `gcc-toolset-14` on Linux. If it
recurs after the current code passes format/tidy, we can either bump the
DuckDB submodule past v1.5.3 or disable that tool target from the
extension's CMake.

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

### 3. Migrate `from_hex` / `unhex` rounds + remaining macros

Some round-6 entries are still SQL macros (`trino_from_hex` calls DuckDB's
`unhex`; multi-arg overload macros are stacked). Consolidate into single
named overloads, audit for unused entries.
