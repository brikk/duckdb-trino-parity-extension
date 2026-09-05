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

### 1b. DuckDB 2.0 drops ICU — plan the ICU exit (gates 1a)

DuckDB v2.0 "Cyanoptera" (this fall; v2.0-alpha already out) **removes the ICU
library entirely** — the `icu` extension reimplements timezones/calendars/
collations natively, with IANA tz data compressed to ~45 kB
([highlights §9](https://duckdb.org/2026/08/17/duckdb-20-highlights), PRs
#24463 / #24403). Verified against the blog; scope and impact:

- **Scope is tz / calendar / collation only.** It does NOT touch DuckDB's
  `lower`/`upper`/`nfc_normalize`, so the Trino-vs-DuckDB *case* divergences this
  extension fixes are unchanged — the extension is still needed.
- **Our runtime is NOT broken.** We statically link our *own* vendored ICU into
  the `.duckdb_extension`, independent of DuckDB's libraries.
- **The real DuckDB-2.0 walls for this plugin are elsewhere:**
  1. **Source anchor gone.** Our snapshot was copied from DuckDB's
     `extension/icu`; once upstream drops it we must vendor upstream ICU
     ourselves and keep hauling ~20 MB (binary size, Wasm cost).
  2. **Extension API/ABI rework** ([§10](https://duckdb.org/2026/08/17/duckdb-20-highlights),
     PRs #24702 / #24135 / #24435). New versioned C API + a stable-ABI C++ API
     (`duckdb_cpp.hpp`, `DUCKDB_CPP_EXTENSION_ENTRYPOINT`, builder-style
     `ScalarFunction`). We use the *old* unstable C++ API
     (`DUCKDB_CPP_EXTENSION_ENTRY`, `ScalarFunction(name,{args},ret,fn)`), so a
     2.0 build needs at least a rebuild and likely migration to the new API.
     Upside: on the stable ABI the extension no longer needs per-DuckDB-version
     rebuilds ("write once, host yourself"), and org-hosted signed repositories
     (PR #24777) become an option for distribution.
- **Strategic signal — this validates option (B).** DuckDB just replaced a 20 MB
  ICU with a ~45 kB native slice. We should do the same for the slice we need:
  - `lower`/`upper` — a **generated simple-case table** pinned to the target
    Unicode/JDK. Tiny, JDK-exact (solves 1a's "prove ICU == JDK" outright), drops
    ICU for case mapping.
  - `trim` — a generated `White_Space` set. Trivial.
  - `reverse` — no Unicode data needed (pure code-point reverse); already ICU-free.
  - `normalize` (NFC) — the only hard dependency (canonical decomposition/
    composition + combining classes). Options: a slim generated NFC table, or drop
    pushed `normalize`.
  Decision: **(A)** bump vendored ICU → 76 for 0.5.0 (fixes 1a now, but a
  short-lived stopgap that still gets rebuilt/migrated for 2.0), or **(B)**
  replace ICU with generated tables for case/trim now and settle NFC separately —
  future-proof against 2.0 and JDK-exact. Given 2.0 is near and forces an API
  migration anyway, **(B) for case/trim is the better investment**; pair it with
  the 2.0 C++ API migration rather than doing (A) first.

  **Release calendar (DuckDB) & our sequencing** — runway as of 2026-09:
  - **2026-09-16 — DuckDB 1.5.6** (patch). Track it like 1.5.4→1.5.5: bump the
    `duckdb` submodule + workflow `duckdb_version`, confirm CI green. Community
    stable moves here, so 0.5.0 targets 1.5.6.
  - **Second half of Oct 2026 — DuckDB 2.0.0** (ICU removed, new C/C++ extension
    API, new parser/storage). ~6–7 weeks out — enough runway to do (B) properly.

  Plan on this timeline:
  1. Land 0.4.0 (PR #2614), then track 1.5.6 on release (submodule/workflow bump).
  2. Ship EV-E3 as **0.5.0 via path (B)** on 1.5.x: generated JDK-exact case +
     `White_Space` tables (drop ICU for `lower`/`upper`/`trim`), plus the
     `trino_unicode_version()` gate and JDK-enumeration canaries. Decide NFC
     (slim generated table vs. drop pushed `normalize`).
  3. When 2.0.0 lands, do the C++ API/ABI migration — which is now ICU-free, so
     it's an API port only, not an ICU-on-2.0 reconciliation.
  Doing (A) first would mean porting a 20 MB ICU 76 snapshot onto 2.0's build
  weeks later — wasted work. (B) collapses EV-E3 and 2.0-readiness into one line.

### 2. Add further native functions only on demonstrated divergence

The bar for shipping a new `trino_*` function is "DuckDB's built-in genuinely
disagrees with Trino." If a new divergence is found (e.g. a locale-sensitive
op), add it natively here and to `trino_meta()`; otherwise it stays a
caller-side bare-DuckDB call.
