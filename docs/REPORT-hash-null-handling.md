# REPORT: Hash function NULL handling + encoding sanity

> **Scope note.** The `trino_parity` extension ships only 10 native functions
> (see the repo [README](../README.md)); of the hash family, only
> `trino_xxhash64` / `trino_sha512` / `trino_hmac_sha256` are shipped natively.
> `md5` / `sha1` / `sha256` (and the encoding + `concat_ws` helpers discussed
> here) are **not** shipped — the caller emits them directly against DuckDB
> (`unhex(md5(x))`, `unhex(sha1(x))`, `unhex(sha256(x))`, etc.). The
> native-hash rationale below is still the doc's key point.

This report substantiates `trino_parity`'s hash and encoding surface. It
records the empirical NULL-handling behaviour of DuckDB's built-in hash and
concat-family functions against Trino's documented semantics, and explains why
the extra hash primitives (`sha512`, `xxhash64`, `hmac_sha256`) are implemented
natively over vendored single-file libraries rather than depending on the
DuckDB community-extension catalog at runtime.

It is the hash/encoding companion to
[REPORT-string-unicode-audit.md](REPORT-string-unicode-audit.md); together they
cover the correctness rationale behind every function `trino_meta()` lists as
pushable.

## TL;DR

1. **DuckDB built-in `md5`, `sha1`, `sha256` propagate NULL correctly** —
   `md5(NULL) → NULL`, `sha256('a' || NULL || 'c') → NULL`. Trino-aligned. These
   are **not** shipped by the extension — the caller emits `unhex(md5(x))` /
   `unhex(sha1(x))` / `unhex(sha256(x))` directly against DuckDB's hex-returning
   built-ins, producing BLOB output that matches Trino's VARBINARY return shape.

2. **DuckDB's variadic `hash(value, …)` does NOT propagate NULL.** `hash(NULL)`
   returns a stable UBIGINT (`13787848793156543929`), not NULL, and
   `hash('a', NULL, 'c')` returns a different stable UBIGINT than
   `hash('a', '', 'c')`. DuckDB treats NULL as a distinguished sentinel inside
   hash composition — the opposite of Trino's null-propagation. This is a
   DuckDB-only surface with no `trino_*` counterpart; it is documented here for
   completeness, not because it is reachable through the extension.

3. **`concat` vs `concat_ws` diverge on NULL.** DuckDB `concat(...)` silently
   skips NULLs (divergent from Trino, which NULL-propagates), so a caller must
   **not** emit it; the caller uses the `||` operator chain instead, which
   NULL-propagates in both engines. `concat_ws(...)` is aligned across all NULL
   shapes, so the caller emits DuckDB's bare `concat_ws(...)` directly. Neither
   is shipped by the extension — both are caller-side.

4. **The extra hashes are self-contained.** `trino_sha512`, `trino_xxhash64`,
   and `trino_hmac_sha256` are native C++ backed by vendored single-file
   libraries, chosen over a runtime dependency on the `crypto` / `hashfuncs`
   community extensions because per-extension catalog availability lags each
   DuckDB release — a fragile foundation for a server-side dependency.

## Findings detail

### Core DuckDB hash NULL propagation — aligned with Trino

| Function | Input | DuckDB result | Trino-expected |
|---|---|---|---|
| `md5(NULL)` | NULL | **NULL** | NULL |
| `md5('')` | empty | `d41d8cd98f00b204e9800998ecf8427e` | same hex (cast via `unhex` for VARBINARY) |
| `md5('a' \|\| NULL \|\| 'c')` | NULL-containing concat | **NULL** | NULL (`\|\|` propagates) |
| `sha256(NULL)` | NULL | **NULL** | NULL |
| `sha256('')` | empty | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` | same hex |

The caller emits `unhex(md5(...))` / `unhex(sha1(...))` / `unhex(sha256(...))`
directly for BLOB-typed output that matches Trino's VARBINARY return; none of
these three is shipped by the extension.

### DuckDB's variadic `hash(value, …)` — NULL is a sentinel, not propagated

| SQL | Result (UBIGINT) | Observation |
|---|---|---|
| `hash(NULL)` | `13787848793156543929` | NULL hashed as a value, not propagated |
| `hash('a')` | `12561829011207016135` | baseline |
| `hash('a', NULL)` | `12149695185283952434` | NULL in trailing position changes hash |
| `hash('a', NULL, 'c')` | `451469780638546153` | NULL middle, changes hash |
| `hash('a', 'b', 'c')` | `6066993828470297781` | all non-NULL baseline |
| `hash('a', '', 'c')` | `8161848143140746562` | distinct from NULL middle — empty string is its own value |

DuckDB distinguishes NULL from empty string in variadic `hash()`. Trino has no
equivalent multi-arg form on `xxhash64` / `md5` / `sha*`, so this surface is
not part of the `trino_*` layer. If a caller writes a wrapped expression like
`trino_xxhash64(col1 || col2)`, the concatenation step `col1 || col2` is NULL
whenever either operand is NULL, and the wrapping hash is then NULL — which
matches Trino's `xxhash64(col1 || col2)` behaviour.

### Encoding built-ins — sanity check

All NULL-propagate correctly: `hex(NULL)`, `unhex(NULL)`, `to_base64(NULL)`,
`from_base64(NULL)`, `url_encode(NULL)`, `url_decode(NULL)`, `length(NULL)`.
None of these is shipped by the extension — the caller emits the bare DuckDB
built-ins (`hex` / `unhex` / `to_base64` / `from_base64` / `url_encode` /
`url_decode` / `length`) directly for `to_hex` / `from_hex` / `to_base64` /
`from_base64` / `url_encode` / `url_decode` / `length`.

## `concat` / `concat_ws` NULL behaviour

Same shape question as the multi-arg hashes — does NULL inside a multi-arg
concat-style call propagate or get silently skipped?

| Function | Input shape | DuckDB result | Trino-expected | Verdict |
|---|---|---|---|---|
| `concat('a', NULL, 'c', 'd')` | middle NULL | `'acd'` (NULL skipped) | NULL | ❌ **DIVERGENT** |
| `concat(NULL, 'b', 'c')` | first NULL | `'bc'` | NULL | ❌ DIVERGENT |
| `concat(NULL, NULL)` | all NULL | `''` (empty string) | NULL | ❌ DIVERGENT |
| `concat_ws('\|', 'a', NULL, 'c', 'd')` | NULL element | `'a\|c\|d'` (skipped) | `'a\|c\|d'` (skipped) | ✅ aligned |
| `concat_ws('\|', NULL, 'b', 'c')` | first NULL element | `'b\|c'` | `'b\|c'` | ✅ aligned |
| `concat_ws(NULL, 'a', 'b')` | NULL separator | NULL | NULL | ✅ aligned (both NULL-propagate sep) |
| `concat_ws('\|', NULL, NULL)` | all NULL elements | `''` (empty string) | `''` | ✅ aligned |
| `'a' \|\| NULL \|\| 'c'` (operator form) | NULL middle | NULL | NULL | ✅ aligned (both `\|\|` propagate) |

**Takeaway:**

- `concat(...)` is divergent on every NULL-bearing case, so a caller must
  **not** emit it. A caller that pushes Trino predicates to DuckDB and needs
  NULL-propagating concatenation should use the `||` operator, which is aligned.
- `concat_ws(...)` is aligned across all NULL shapes, so the caller emits
  DuckDB's bare `concat_ws(...)` directly. Neither concat form is shipped by the
  extension — both are caller-side.
- The `concat` divergence is the same shape as DuckDB's variadic
  `hash(value, …)`: NULL gets silently absorbed into the result rather than
  propagated. The shared pattern reinforces the general rule — do not expose a
  DuckDB function that takes variadic mixed-NULL inputs unless the engine has
  been verified to drop the NULL the same way Trino does.

## Why the extra hashes are native, not community-extension-backed

Trino's hash surface also includes `sha512`, `xxhash64`, and `hmac_sha256`,
which DuckDB's core does not provide as hex-returning built-ins. The obvious
route would be to load a community extension at runtime (`crypto` for
SHA-512 / HMAC, `hashfuncs` for xxHash) and macro over it. That route was
rejected.

### The availability problem

The DuckDB community-extensions catalog publishes one build per
(extension × DuckDB version × platform). Each cell is backfilled independently
as maintainers cut builds, so for any recent DuckDB release a given extension
may return **HTTP 404** from the extension repository for days or weeks after
the release ships — even when the same extension is available for the previous
patch version. A probe against a recent DuckDB release illustrated the gap:

| Extension | Status | Notes |
|---|---|---|
| `crypto` | HTTP 404 | No build yet for the probed DuckDB version/platform; other extensions on the same platform had builds. |
| `hashfuncs` | HTTP 404 | No build yet for the probed version. |
| `netquack` | HTTP 404 | No build yet for the probed version. |

The 404s were almost certainly "the maintainer has not pushed a build for this
DuckDB version yet" rather than a permanent platform gap — but that timing is
exactly the problem. A server-side interpretation layer cannot condition its
correctness on whether a third-party maintainer has backfilled a build for the
DuckDB version the host happens to be running. Pinning DuckDB to an older patch
just to keep an extension available trades one fragility for another.

### The resolution: self-contained primitives

The extension vendors the hash primitives as single-file libraries and links
them statically, so `trino_sha512`, `trino_xxhash64`, and `trino_hmac_sha256`
have no runtime extension dependency:

- **xxHash** — BSD-2-licensed, header-only (backs `trino_xxhash64`).
- **WjCryptLib SHA-256 / SHA-512** — public domain (backs `trino_sha512` and
  the SHA-256 core inside `trino_hmac_sha256`).

See [`CMakeLists.txt`](../CMakeLists.txt) (the `third_party/hash` include and
`WjCryptLib_Sha256.c` / `WjCryptLib_Sha512.c` sources) and
`third_party/hash/THIRD_PARTY_NOTICES.md`. `hmac_sha256` is native-only because
DuckDB's `crypto_hmac` is VARCHAR-only and could not carry Trino's arbitrary
VARBINARY key/message; the native function operates on raw bytes.

### Hash functions — where each is handled

| Trino function | How it is emitted | Implementation |
|---|---|---|
| `md5` | caller-side (bare DuckDB) | caller emits `unhex(md5(b))` directly |
| `sha1` | caller-side (bare DuckDB) | caller emits `unhex(sha1(b))` directly |
| `sha256` | caller-side (bare DuckDB) | caller emits `unhex(sha256(b))` directly |
| `sha512` | `trino_sha512` (native) | native C++ over vendored WjCryptLib |
| `xxhash64` | `trino_xxhash64` (native) | native C++ over vendored xxHash |
| `hmac_sha256` | `trino_hmac_sha256` (native) | native C++ over vendored WjCryptLib SHA-256 |

Only the three native functions appear in `trino_meta()` under the `hash`
category; `md5` / `sha1` / `sha256` are not shipped and are emitted by the
caller directly against DuckDB's built-ins.

An operator who prefers the community-extension route (loading `crypto` /
`hashfuncs` and mapping to them) can still do so above the extension; the
availability and coverage tradeoffs are catalogued in
[RESEARCH-duckdb-extension-coverage.md](RESEARCH-duckdb-extension-coverage.md).

## See also

- [REPORT-string-unicode-audit.md](REPORT-string-unicode-audit.md) — the
  Unicode divergence audit motivating the native string functions.
- [REPORT-datetime-tz-handling.md](REPORT-datetime-tz-handling.md) — session
  timezone obligations for the date/time functions.
- [RESEARCH-trino-duckdb-function-mapping.md](RESEARCH-trino-duckdb-function-mapping.md)
  — the full Trino → DuckDB function mapping.
- [RESEARCH-duckdb-extension-coverage.md](RESEARCH-duckdb-extension-coverage.md)
  — community-extension availability tradeoffs.
