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

Two extra requirements beyond a plain data bump:
- **Match the *deployment's* JDK, not "latest."** The parity target is whatever
  Unicode version the connected Trino's JDK uses (JDK 24/25 = 16, JDK 23 = 15.1).
  Pinning Unicode 16 is right for current Trino but will skew again as the JDK
  advances — and in the *other* direction for older-JDK workers. Surface the
  extension's Unicode version (e.g. a `trino_unicode_version()` scalar or a
  `trino_meta()` row) so the connector can gate PARITY pushdown on the min of the
  two versions. That gate — not the data bump — is what actually guarantees
  losslessness between releases.
- **Prove ICU == JDK, don't assume it.** The algorithm relies on ICU's
  `u_tolower`/`u_toupper` reproducing `Character.toLowerCase/toUpperCase(int)`.
  Generate the canary corpus by enumerating all code points through the *actual*
  target JDK and diffing against the extension; carry a tiny override table for
  any code point where ICU disagrees with the JDK. **See 1b — this may be moot.**

### 1b. DuckDB 2 will remove ICU — plan the ICU exit (gates 1a)

DuckDB 2 (due "soon", per DuckDB 2 prep notes) drops ICU from its libraries in
favour of internal Unicode implementations. Impact on this plugin:
- **Runtime is NOT broken.** We statically link our *own* vendored ICU into the
  `.duckdb_extension`, independent of DuckDB's libraries — so removing DuckDB's
  ICU doesn't break our Unicode at load time.
- **But the source anchor and the strategy change.** Our snapshot was copied from
  DuckDB's `extension/icu`; once DuckDB drops it we must track upstream ICU
  ourselves, reconcile the DuckDB 2 build/extension-template, and keep carrying a
  ~20 MB vendored ICU (binary size, Wasm cost) that the rest of the ecosystem is
  deprecating.
- **Therefore weigh 1a against going ICU-independent instead of bumping to 76:**
  - `lower`/`upper` — a **generated simple-case table** pinned to the target
    Unicode/JDK. Tiny, JDK-exact (solves 1a's "prove ICU == JDK" outright), and
    drops ICU for case mapping.
  - `trim` — a generated `White_Space` set. Trivial.
  - `reverse` — needs no Unicode data (pure code-point reverse); already
    ICU-free in spirit.
  - `normalize` (NFC) — the hard dependency: needs canonical decomposition/
    composition + combining classes. Options: keep only a slim normalizer, ship a
    generated NFC table, or drop pushed `normalize`.
  Decision to make: **(A)** bump vendored ICU → 76 for 0.5.0 as a short-lived
  stopgap, or **(B)** replace ICU with generated tables for case/trim (cheap,
  future-proof, JDK-exact) and settle NFC separately. Given DuckDB 2 is near,
  (A)-then-(B) likely duplicates work; leaning toward (B) for case/trim now.

### 2. Add further native functions only on demonstrated divergence

The bar for shipping a new `trino_*` function is "DuckDB's built-in genuinely
disagrees with Trino." If a new divergence is found (e.g. a locale-sensitive
op), add it natively here and to `trino_meta()`; otherwise it stays a
caller-side bare-DuckDB call.
