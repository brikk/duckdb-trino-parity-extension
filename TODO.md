# TODO

This extension provides native DuckDB scalar functions (and one table macro,
`trino_meta()`) for the specific cases where DuckDB's built-ins diverge from
Trino, so that a caller pushing Trino-shaped predicates to DuckDB — such as the
[duckbridge / trino-duckbridge](https://github.com/brikk/duckbridge)
connector — stays lossless on non-ASCII / byte-level input. Aligned Trino
functions are emitted by the caller directly against DuckDB and are not shipped
here (see [`docs/RESEARCH-trino-duckdb-function-mapping.md`](docs/RESEARCH-trino-duckdb-function-mapping.md)).

## Done

- ✅ Native ICU-backed `trino_lower` / `trino_upper` / `trino_reverse`
  (root-locale full case folding, code-point reverse).
- ✅ Native ICU-backed `trino_trim` / `trino_ltrim` / `trino_rtrim` via
  `u_isWhitespace` (Java `Character.isWhitespace` semantics — NBSP /
  FIGURE / NARROW NBSP intentionally NOT whitespace).
- ✅ Native `trino_normalize/1` (NFC) via `icu::Normalizer2`.
- ✅ Native `trino_xxhash64` / `trino_sha512` / `trino_hmac_sha256` over
  vendored single-file primitives (xxHash + WjCryptLib SHA) — no dependency on
  the `crypto` / `hashfuncs` community extensions.
- ✅ `trino_meta()` table macro — catalog of the ten native functions the
  extension provides; a caller probes it at startup.
- ✅ **Passthrough shrink**: removed the ~85 macros that merely renamed /
  reshaped a DuckDB built-in with no behaviour change. The caller emits those
  as bare DuckDB SQL, verified per-entry by the reference connector's
  cross-engine canary.
- ✅ Vendored ICU under `third_party/icu/` (copied from DuckDB's bundled
  `extension/icu/third_party/icu`) — no vcpkg dependency at build time. Ships
  NFC data only.
- ✅ Linux build container (`make linux-arm64`, `make linux-amd64`) for
  cross-platform binaries when developing on macOS.
- ✅ Full CI build matrix green on every push (Linux amd64/arm64, MacOS
  amd64/arm64, Windows MSVC + MinGW, DuckDB-Wasm mvp/eh/threads) + Format/Tidy,
  built against DuckDB v1.5.4 to match community-extensions.
- ✅ CI-artifact fallback script
  ([`scripts/fetch-from-ci-artifacts.sh`](scripts/fetch-from-ci-artifacts.sh))
  for pulling platform builds without running the local container.

## Open

### 1. Publish to DuckDB community-extensions

Path to `INSTALL trino_parity FROM community; LOAD trino_parity;` —
zero-binary-management for operators.

- Finalize `description.yml` (per
  https://duckdb.org/community_extensions/documentation) — version `0.2.0`,
  `ref` pinned to the released commit.
- Submit a PR to https://github.com/duckdb/community-extensions adding our
  description.
- After acceptance, DuckDB CI builds and serves signed binaries from
  `https://community-extensions.duckdb.org/...`.
- The connector then loads it via `INSTALL ... FROM community` instead of
  bundling per-platform binaries.

### 2. Add further native functions only on demonstrated divergence

The bar for shipping a new `trino_*` function is "DuckDB's built-in genuinely
disagrees with Trino." If a new divergence is found (e.g. a locale-sensitive
op), add it natively here and to `trino_meta()`; otherwise it stays a
caller-side bare-DuckDB call.
