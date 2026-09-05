# TODO

This extension provides native DuckDB scalar functions (and one table macro,
`trino_meta()`) for the specific cases where DuckDB's built-ins diverge from
Trino, so that a caller pushing Trino-shaped predicates to DuckDB — such as the
[duckbridge / trino-duckbridge](https://github.com/brikk/duckbridge)
connector — can preserve semantics on non-ASCII / byte-level input within a
validated compatibility profile (see 1a). Aligned Trino
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
- ✅ Statically vendored ICU under `third_party/icu/`, independent of DuckDB's
  ICU — no vcpkg dependency at build time. The historical ICU 66.1 / Unicode 13
  snapshot came from DuckDB's `extension/icu/third_party/icu`; see 1a for the
  upgrade. Ships NFC normalization data only; the public API is NFC-only.
- ✅ Linux build container (`make linux-arm64`, `make linux-amd64`) for
  cross-platform binaries when developing on macOS.
- ✅ Full CI build matrix established (Linux amd64/arm64, MacOS
  amd64/arm64, Windows MSVC + MinGW, DuckDB-Wasm mvp/eh/threads) + Format/Tidy,
  originally against DuckDB v1.5.4; current workflow target is v1.5.5.
  This is not a claim that the pending 0.5.0 upgrade has passed CI.
- ✅ CI-artifact fallback script
  ([`scripts/fetch-from-ci-artifacts.sh`](scripts/fetch-from-ci-artifacts.sh))
  for pulling platform builds without running the local container.

## Open

### 1. Publishing to DuckDB community-extensions (done; per-release procedure)

Published: `INSTALL trino_parity FROM community; LOAD trino_parity;` serves
signed binaries from `https://community-extensions.duckdb.org/<duckdb-version>/<platform>/`.
The catalog entry is `extensions/trino_parity/description.yml` in
https://github.com/duckdb/community-extensions and pins a `ref` (commit SHA) of
this repo — **a new release is not live until that ref is bumped and the signed
binaries are rebuilt and published.**

Per release:
1. Land the change on `main` here; CI (MainDistributionPipeline) must be green.
2. Bump `vcpkg.json` `version`.
3. PR to duckdb/community-extensions updating `ref` (new SHA) and `version`, and
   keeping `docs.hello_world` / `extended_description` truthful to the shipped
   semantics.
4. Consumers (e.g. duckbridge's `fetch-parity-extension.sh`) pick the new binary
   up once the community CDN is rebuilt.

### 1a. EV-E3: ICU 76.1 / Unicode 16 for 0.5.0 (local validation passed)

**Agreed implementation:** upgrade the statically vendored ICU from 66.1 /
Unicode 13 to **ICU 76.1 / Unicode 16.0**, preserving all ten functions and
the NFC-only `trino_normalize/1` public API. Vendor provenance belongs under
[`third_party/icu/`](third_party/icu/). Generated tables are an alternative
not selected for EV-E3; removing ICU or dropping normalization is not the plan.
NFC data can also serve NFD, so NFC-only is an API scope decision, not a
separate NFD-data requirement.

The old baseline misses case pairs added in Unicode 14-16: for example,
`trino_lower(U+2C2F)` stays U+2C2F rather than mapping to U+2C5F. The upgrade
targets **Trino 483 on a pinned JDK 25 (Unicode 16)**, not every Trino/JDK
combination. Local validation and remaining release work:

- Implemented `scripts/check_unicode.py` with an OpenJDK 25 GA `25+36-3489`
  oracle. On Linux x86_64 / DuckDB 1.5.5, all 1,112,064 valid scalar values
  and 99,825 NFC column checks pass. The sqllogic suite passes 94 assertions.
  Exact provenance and commands: [validation report](docs/REPORT-unicode16-validation.md).
- Resolve any ICU/JDK disagreements found by validation rather than assuming
  equal Unicode versions prove parity. Pin regression cases including U+2C2F.
- Preserve `u_isWhitespace` / Java `Character.isWhitespace` semantics, **not
  Unicode `White_Space`**. They differ: Java excludes NBSP, U+2007 and U+202F,
  but includes U+001C-U+001F, which `White_Space` does not.
- For an **unvalidated deployment profile**, the caller must disable pushdown
  of affected functions or prove a safe input restriction. Taking the minimum
  Unicode version does not establish losslessness, nor does a version label
  alone validate the implementation.
- Keep `trino_meta()` unchanged: ten rows with `trino_name`, `arg_count`, `category`.
  Add no metadata function or row until the consumer interface is agreed.
- Cross-platform CI remains required before release; local validation is not
  a claim that every distribution platform passes. Landing/publishing 0.4.0 (PR #2614) is independent of
  implementing and testing 0.5.0; neither blocks that work. The 0.5.0 version
  bump does not mean it is live on the community CDN.

### 1b. DuckDB release tracking (does not gate EV-E3)

The current DuckDB target is **1.5.5**. The calendar plans 1.5.6 for
**2026-09-16** and 2.0 for the **second half of October 2026**; those plans do
not automatically change our submodule, workflow pins or 0.5.0 target.
Evaluate each release separately with builds and semantic regression tests.

DuckDB's [2.0 preview](https://duckdb.org/2026/08/17/duckdb-20-highlights)
describes replacing its ICU-backed timezone/calendar/collation implementation.
The quoted ~45 kB is compressed **timezone data**, not the total replacement
footprint and not a size estimate for our case/normalization implementation.
Our ICU is statically vendored independently of DuckDB; upstream removing its
own ICU does not require us to remove ours for 2.0.

The preview is not evidence that DuckDB `lower` / `upper` / `nfc_normalize`
behaviour is unchanged: rerun comparisons against the actual release. Our
internal C++ API build will need release-specific compatibility checks and
any necessary source fixes. Stable-ABI adoption is optional, not a migration
forced by the preview; assess it separately from the ICU upgrade.

The unchanged extension source compiles on Linux against `v2.0-cyanoptera`
commit `e3946f2327a3cc622e1ec7fe71d51de49f93e61d` in extensions-only,
dynamic-symbol mode. This is compile/link coverage, not runtime or distribution
matrix coverage; the production pin remains 1.5.5.

### 2. Add further native functions only on demonstrated divergence

The bar for shipping a new `trino_*` function is "DuckDB's built-in genuinely
disagrees with Trino." If a new divergence is found (e.g. a locale-sensitive
op), add it natively here and to `trino_meta()`; otherwise it stays a
caller-side bare-DuckDB call.
