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
  pushable-set catalog (92 entries) the connector mirrors in
  `DuckDbExpressionTranslator.PUSHABLE_FUNCTIONS`.
- ✅ ICU best-effort INSTALL/LOAD on extension load so `trino_with_timezone`
  resolves DuckDB's `timezone()`.
- ✅ Vendored ICU under `third_party/icu/` (copied from DuckDB's bundled
  `extension/icu/third_party/icu`) — no vcpkg dependency at build time.
  Trades NFD/NFKC/NFKD pushdown for build-system simplicity; the connector
  pushes only `normalize/1` (NFC) as a result.
- ✅ Linux build container (`make linux-arm64`, `make linux-amd64`) for
  cross-platform binaries when developing on macOS.
- ✅ Full CI build matrix green on every push — `MainDistributionPipeline.yml`
  calls `extension-ci-tools`' `_extension_distribution.yml` reusable workflow,
  which builds and signs Linux (amd64/arm64), MacOS (amd64/arm64), Windows
  (amd64 MSVC + MinGW), and DuckDB-Wasm (mvp/eh/threads) on every push to
  `main`. Plus Format + Tidy checks. Artifacts are downloadable per-run
  via `scripts/fetch-from-ci-artifacts.sh`.
- ✅ CI-artifact fallback script
  ([`scripts/fetch-from-ci-artifacts.sh`](scripts/fetch-from-ci-artifacts.sh))
  for pulling platform builds without running the local container.

## Open

### 1. Publish to DuckDB community-extensions

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

### 2. Migrate `from_hex` / `unhex` rounds + remaining macros

Some round-6 entries are still SQL macros (`trino_from_hex` calls DuckDB's
`unhex`; multi-arg overload macros are stacked). Consolidate into single
named overloads, audit for unused entries.
