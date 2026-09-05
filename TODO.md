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
  (simple per-code-point case mapping via `u_tolower`/`u_toupper` — Trino's
  `SliceUtf8`/`Character.toUpperCase(int)` model, corrected 2026-09-02 from the
  earlier root-locale *full* mapping which diverged from Trino on ß, İ, final
  sigma and ligatures; code-point reverse).
- ✅ Native ICU-backed `trino_trim` / `trino_ltrim` / `trino_rtrim` via
  `u_isWhitespace` (Java `Character.isWhitespace` semantics — NBSP /
  FIGURE / NARROW NBSP intentionally NOT whitespace).
- ✅ Native `trino_normalize/1` (NFC) via `icu::Normalizer2`.
- ✅ Native `trino_xxhash64` / `trino_sha512` / `trino_hmac_sha256` over
  vendored single-file primitives (xxHash + WjCryptLib SHA) — no dependency on
  the `crypto` / `hashfuncs` community extensions.
- ✅ `trino_hmac_sha256` rejects a zero-byte key with `Empty key`, matching Trino
  (0.4.0 / EV-E2); empty data, binary-NUL keys, NULL propagation and >64-byte
  key hashing are sqllogic-pinned.
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

### 1. Publishing to DuckDB community-extensions (done; per-release procedure)

Published: `INSTALL trino_parity FROM community; LOAD trino_parity;` serves
signed binaries from `https://community-extensions.duckdb.org/<duckdb-version>/<platform>/`.
The catalog entry is `extensions/trino_parity/description.yml` in
https://github.com/duckdb/community-extensions and pins a `ref` (commit SHA) of
this repo — **a new release is not live until that ref is bumped.**

Per release:
1. Land the change on `main` here; CI (MainDistributionPipeline) must be green.
2. Bump `vcpkg.json` `version`.
3. PR to duckdb/community-extensions updating `ref` (new SHA) and `version`, and
   keeping `docs.hello_world` / `extended_description` truthful to the shipped
   semantics.
4. Consumers (e.g. duckbridge's `fetch-parity-extension.sh`) pick the new binary
   up once the community CDN is rebuilt.

### 1a. Bump vendored ICU (Unicode 13 → ≥ 16)

`third_party/icu` is DuckDB's ICU 66 snapshot (Unicode 13). Trino runs on the
worker JDK (JDK 21 = Unicode 15, JDK 25 = Unicode 16), so case pairs added in
Unicode 14–16 diverge: `trino_lower(U+2C2F)` (Glagolitic, 14.0) stays U+2C2F
where Trino gives U+2C5F. Bump to an ICU release carrying Unicode 16 data and
pin a canary for U+2C2F in `test/sql/trino_parity.test`.

### 2. Add further native functions only on demonstrated divergence

The bar for shipping a new `trino_*` function is "DuckDB's built-in genuinely
disagrees with Trino." If a new divergence is found (e.g. a locale-sensitive
op), add it natively here and to `trino_meta()`; otherwise it stays a
caller-side bare-DuckDB call.
