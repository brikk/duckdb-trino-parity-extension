# trino_parity — DuckDB ↔ Trino function parity

A DuckDB extension that provides native `trino_<name>(...)` scalar functions
for the specific cases where DuckDB's built-ins diverge from Trino's documented
behaviour on Unicode and byte-level inputs.

Designed to be loaded server-side by anything that pushes Trino-shaped
predicates down to DuckDB. The first consumer is
[duckbridge / trino-duckbridge](https://github.com/brikk/duckbridge).

## Why this exists

Trino implements `lower()`, `upper()`, `reverse()`, etc. in terms of Java's
Unicode tables. DuckDB has its own implementations that agree on ASCII but
disagree on real-world Unicode input. If a Trino connector naively pushes
`WHERE lower(name) = 'apple'` down to DuckDB, the filter runs with different
semantics in each engine — and rows visible to Trino can silently disappear
from the result.

This extension supplies **only the functions where that actually happens** —
where DuckDB's built-in genuinely diverges and a native C++ reimplementation is
required to match Trino. A caller emits `trino_<name>(...)` for these. For the
large majority of Trino functions DuckDB's built-in is already byte-for-byte
equivalent (or a trivial rename / operator / one-line rewrite), so the caller
emits those directly against DuckDB — no extension needed. See
[`docs/RESEARCH-trino-duckdb-function-mapping.md`](docs/RESEARCH-trino-duckdb-function-mapping.md)
for the full per-function classification and
[what this extension deliberately does not ship](#what-this-extension-deliberately-does-not-ship)
below.

The catalog table macro `trino_meta()` lists exactly the functions this
extension provides, so a consumer can probe it at startup.

### Concrete divergences fixed

Every function shipped by this extension exists because of a row like these:

| Function | DuckDB built-in | Trino spec | `trino_*` result |
|---|---|---|---|
| `upper('ß')` (U+00DF) | `'ẞ'` (U+1E9E — utf8proc special case) | `'ß'` unchanged (simple mapping: no uppercase in UnicodeData) | matches Trino |
| `upper('straße strauß')` | `'STRAẞE STRAUẞ'` | `'STRAßE STRAUß'` | matches Trino |
| `lower('İ')` (U+0130) | `'i'` | `'i'` (simple mapping — *not* `'i'` + U+0307) | matches Trino |
| `reverse('cafe' + U+0301)` | grapheme-aware (combining mark stays glued to `e`) | code-point-only (mark moves to front) | matches Trino |
| `reverse('👨‍👩‍👧')` (ZWJ family) | unchanged (one cluster) | reversed across ZWJ boundaries | matches Trino |
| `trim` (tab / LF / CR / FF / VT) | bare `trim` leaves them | Java `Character.isWhitespace` strips them | matches Trino |
| `xxhash64(varbinary)` | no direct built-in (`hash()` differs) | XXH64, big-endian bytes | matches Trino |

The full divergence catalog and its empirical-verification corpus live in
[`docs/REPORT-string-unicode-audit.md`](docs/REPORT-string-unicode-audit.md).

## Function inventory

`trino_meta()` is the source of truth — 10 functions in 2 categories, all
native C++. Their implementations live in
[`src/string_functions.cpp`](src/string_functions.cpp) +
[`src/hash_functions.cpp`](src/hash_functions.cpp); the catalog itself is in
[`src/macro_definitions.cpp`](src/macro_definitions.cpp):

| Category | Functions |
|---|---|
| String (native, ICU) | `trino_lower/1`, `trino_upper/1`, `trino_reverse/1`, `trino_trim/1`, `trino_ltrim/1`, `trino_rtrim/1`, `trino_normalize/1` |
| Hash (native, vendored) | `trino_xxhash64/1`, `trino_sha512/1`, `trino_hmac_sha256/2` |

The string functions use the statically-linked ICU and match Java's Unicode
semantics exactly:

- `trino_lower` / `trino_upper`: **simple**, per-code-point case mapping via
  `u_tolower` / `u_toupper` in a `U8_NEXT` loop. This mirrors Trino exactly:
  `StringFunctions.lower/upper` delegate to airlift `SliceUtf8`, which applies
  `Character.toLowerCase(int)` / `toUpperCase(int)` code point by code point —
  *not* `String.toLowerCase(Locale.ROOT)`. So `upper('ß')` = `'ß'` (no 1:2
  expansion), `lower('İ')` = `'i'`, no Greek final-sigma rule, ligatures
  unchanged. (Earlier releases used ICU full string case mapping, which
  diverged from Trino on every one of those inputs.) DuckDB's built-in differs
  only in special-casing `upper('ß')` = `'ẞ'`.
- `trino_reverse`: code-point reverse via `U8_PREV` (combining marks
  detach, ZWJ emoji reverse boundary-by-boundary).
- `trino_trim` / `trino_ltrim` / `trino_rtrim`: skip code points where
  `u_isWhitespace` is true (Java's `Character.isWhitespace` — NBSP /
  U+2007 / U+202F intentionally NOT stripped).
- `trino_normalize/1`: `icu::Normalizer2::getNFCInstance()` — the vendored
  ICU ships only NFC's static data, so NFC is the only form provided.

The hash functions are self-contained over vendored single-file primitives
(xxHash, BSD-2; WjCryptLib SHA, public domain) — no dependency on the `crypto`
/ `hashfuncs` community extensions:

- `trino_xxhash64`: XXH64 over raw bytes, rendered big-endian to match Trino's
  `xxhash64(varbinary) -> varbinary`.
- `trino_sha512`: SHA-512 over raw bytes → `VARBINARY`.
- `trino_hmac_sha256`: HMAC-SHA256 over raw `VARBINARY` key + message (DuckDB's
  `crypto_hmac` is VARCHAR-only and can't take arbitrary binary keys).

All ten functions are timezone- and locale-invariant, so loading this extension
has no session-state prerequisites.

### What this extension deliberately does NOT ship

Trino exposes hundreds of functions; the vast majority need no help because
DuckDB's built-in already matches. A consumer emits those directly against
DuckDB rather than through this extension:

- **Identical passthroughs** (~57): `length`, `abs`, `year`, `sqrt`,
  `substring`, `concat_ws`, `regexp_extract`, … — same name, same semantics.
- **Trivial renames** (~11): `truncate`→`trunc`, `regexp_like`→`regexp_matches`,
  `from_unixtime`→`to_timestamp`, `day_of_year`→`dayofyear`, …
- **Operator bridges** (5): `bitwise_and/or/not/left_shift/right_shift` →
  `&`, `|`, `~`, `<<`, `>>`.
- **One-line SQL rewrites** (~12): `regexp_replace(…, 'g')` (global default),
  `isodow(…)` (`day_of_week`), `unhex(md5(…))` (VARBINARY shape),
  `extract('isoyear' …)` (`year_of_week`), casts for `to_unixtime` /
  `millisecond`, arg-flip for `with_timezone`, …

These ~85 were previously shipped as passthrough macros; they were removed
because they added a dependency surface without changing any behaviour. The
[function mapping](docs/RESEARCH-trino-duckdb-function-mapping.md) records the
DuckDB equivalent and alignment verdict for every one.

> **Timezone note for callers.** DuckDB's date/time functions
> (`year`, `hour`, `date_trunc`, `to_unixtime`, `with_timezone`, …) evaluate
> `TIMESTAMPTZ` against the session `TimeZone`. A caller pushing those must set
> DuckDB's `TimeZone` to match Trino's session zone before evaluating
> predicates, or `year(ts) = 2024` can silently return rows from an adjacent
> year. This is a session-configuration obligation on the caller — none of the
> functions this extension ships are affected. See
> [`docs/REPORT-datetime-tz-handling.md`](docs/REPORT-datetime-tz-handling.md).

## Installation

`trino_parity` is published to the DuckDB
[community-extensions](https://github.com/duckdb/community-extensions) catalog,
so a stock (signed) DuckDB installs it directly — no `allow_unsigned_extensions`
needed:

```sql
INSTALL trino_parity FROM community;
LOAD trino_parity;

SELECT trino_upper('straße');
-- 'STRAßE' — matches Trino's simple per-code-point mapping (DuckDB's bare upper() gives 'STRAẞE')

SELECT * FROM trino_meta();
-- 10 rows: name, arity, category
```

Alternatively, load a locally-built or vendored binary via direct path (set
`allow_unsigned_extensions=true` at DuckDB startup):

```sql
LOAD '/absolute/path/to/trino_parity.duckdb_extension';
```

## Building

Requires ninja and ccache. ICU is vendored under `third_party/icu/` — no
vcpkg needed for local builds. One-time bootstrap on macOS:

```bash
brew install ninja ccache
```

Build:

```bash
git clone --recurse-submodules https://github.com/brikk/duckdb-trino-parity-extension.git
cd duckdb-trino-parity-extension
GEN=ninja make
```

First build is ~30 minutes (DuckDB + the vendored ICU snapshot). Subsequent
builds are seconds with ccache.

> An empty `vcpkg.json` is checked in only so the upstream extension-CI
> workflow's vcpkg manifest-mode toolchain doesn't abort configure on CI.
> Local builds bypass vcpkg entirely (`docker/build-in-container.sh`
> unsets `VCPKG_TOOLCHAIN_PATH`).

Artifacts:

- `build/release/duckdb` — interactive shell with the extension preloaded
- `build/release/extension/trino_parity/trino_parity.duckdb_extension` — the
  loadable binary
- `build/release/test/unittest` — sqllogic test runner

### Cross-platform builds via Docker

A consumer may run DuckDB inside a Linux container while you develop on macOS;
a host-built (darwin-arm64) `.duckdb_extension` cannot be loaded there. Two
make targets build Linux variants inside a Docker container:

```bash
make linux-arm64    # native on Apple Silicon; ~10-15 min first run, seconds thereafter
make linux-amd64    # slower under Rosetta/qemu on Apple Silicon
make all-platforms  # both
```

Output layout (one .duckdb_extension per platform, all optional, all
independently consumable by the trino-duckbridge plugin jar's gradle bundling):

```
build/release/extension/trino_parity/trino_parity.duckdb_extension              # host (output of `make`)
build/linux-arm64/release/extension/trino_parity/trino_parity.duckdb_extension  # `make linux-arm64`
build/linux-amd64/release/extension/trino_parity/trino_parity.duckdb_extension  # `make linux-amd64`
```

Shared vcpkg binary cache and ccache (named Docker volumes) keep subsequent
builds fast. See `docker/Dockerfile.linux-build` and
`docker/build-in-container.sh` for details.

### Fetching CI-built binaries (no local build)

GitHub Actions builds the full cross-platform matrix on every push to `main` —
Linux (amd64/arm64), MacOS (amd64/arm64), Windows (amd64 MSVC + MinGW), and
DuckDB-Wasm (mvp/eh/threads), plus Format + Tidy code-quality checks. To pull
the latest successful run's binaries instead of building locally:

```bash
scripts/fetch-from-ci-artifacts.sh                # latest successful run
scripts/fetch-from-ci-artifacts.sh --run <id>     # specific run
scripts/fetch-from-ci-artifacts.sh --platform linux-arm64,linux-amd64
```

The script uses the `gh` CLI (must be authenticated) and lands the binaries in
the same `build/<platform>/release/extension/trino_parity/` paths the local
`make` targets produce — so the connector's gradle bundling picks them up
without any further configuration.

## Testing

```bash
make test
```

The sqllogic suite covers the Unicode divergence fixtures (Turkish İ, German ß,
decomposed café, ZWJ emoji families, CJK), the trim whitespace set (Java vs.
DuckDB coverage), NFC normalization, the native hash reference vectors
(SHA-512 / xxHash64 / HMAC-SHA256), and `trino_meta()` shape pins (the 10-row
count and the two categories).

The reference connector additionally runs a cross-engine semantic suite
([`TestTrinoFunctionAliases`](https://github.com/brikk/duckbridge/blob/main/trino-duckbridge/test/src/dev/brikk/duckbridge/trino/plugin/TestTrinoFunctionAliases.kt))
that verifies both these native functions and the caller-emitted bare-DuckDB
rewrites against Trino's documented behaviour on a pinned DuckDB version.

## Architecture

Five source files:

- `src/string_functions.cpp` — native C++ scalar functions backed by
  the statically-linked vendored ICU: `trino_lower`, `trino_upper`,
  `trino_reverse`, `trino_trim`, `trino_ltrim`, `trino_rtrim`,
  `trino_normalize`. These are the places where DuckDB's built-ins diverge
  from Trino on real-world Unicode input; rewriting in C++ via ICU pins
  exact Java semantics.
- `src/hash_functions.cpp` — native C++ `trino_xxhash64`, `trino_sha512`,
  `trino_hmac_sha256` over vendored single-file primitives, so the hashes are
  self-contained with no community-extension dependency.
- `src/macro_definitions.cpp` — the `DefaultMacro[] kTrinoMacros` (now empty)
  and `DefaultTableMacro[] kTrinoTableMacros` arrays. The only object it
  registers is the `trino_meta()` table macro cataloguing the ten native
  functions.
- `src/alias_macros_loader.cpp` — `RegisterAliasMacros(loader)` registers
  `trino_meta()` via `DefaultTableFunctionGenerator::CreateTableMacroInfo`
  (the same path DuckDB's bundled `json` extension uses). The scalar-macro
  loop is retained but iterates an empty array.
- `src/trino_parity_extension.cpp` — entry point. Registers the native string
  functions, then the native hash functions, then `trino_meta()`.

ICU is vendored under `third_party/icu/` — a snapshot of DuckDB's bundled
ICU (`common` + `i18n` + `stubdata`). Statically linked into the loadable
extension binary (adds ~30MB) so Unicode behaviour is independent of the
host DuckDB build. The vendored snapshot ships only the NFC normalization
data; NFD/NFKC/NFKD are intentionally out of scope.

## Design notes & substantiation

The `trino_*` function set is not guesswork — every function is backed by an
empirical audit of where DuckDB's built-ins agree with or diverge from Trino's
documented semantics. Those audits, and the Trino↔DuckDB reference they were
derived from, live under [`docs/`](docs/):

- [`RESEARCH-trino-duckdb-function-mapping.md`](docs/RESEARCH-trino-duckdb-function-mapping.md)
  — the canonical, category-by-category Trino↔DuckDB function map (sourced from
  Trino 481 + DuckDB LTS docs) with a per-row alignment verdict and how each is
  handled: `native` (shipped here) vs. caller-side (bare built-in, rename,
  operator, or one-line rewrite).
- [`REPORT-string-unicode-audit.md`](docs/REPORT-string-unicode-audit.md)
  — the Unicode corpus audit that motivates the native ICU string functions
  (`lower`/`upper` simple per-code-point case mapping, code-point `reverse`, Java-whitespace `trim`).
- [`REPORT-datetime-tz-handling.md`](docs/REPORT-datetime-tz-handling.md)
  — DuckDB date/time + `TIMESTAMPTZ` session-zone behaviour, the per-function
  divergences (`day_of_week`→`isodow`, ISO week/year, `date_trunc`/`date_diff`),
  and the divergence-pressure test corpus. Reference for callers emitting the
  date/time functions (see the timezone note above) — no date function ships here.
- [`REPORT-hash-null-handling.md`](docs/REPORT-hash-null-handling.md)
  — NULL propagation for the hash/encoding functions, the `concat` vs `concat_ws`
  divergence, and why `sha512`/`xxhash64`/`hmac_sha256` are implemented natively
  rather than via runtime community-extension dependencies.
- [`RESEARCH-duckdb-extension-coverage.md`](docs/RESEARCH-duckdb-extension-coverage.md)
  — the scope boundary: which Trino-only functions other DuckDB extensions could
  supply, and which remain genuinely uncovered.

## Future work

See [`TODO.md`](TODO.md). Headline items:

- Bumping the vendored ICU snapshot (currently ICU 66 / Unicode 13) so case
  pairs added in Unicode 14–16 (e.g. Glagolitic U+2C2F, Vithkuqi) map as the
  JDK does — the one known residual `trino_lower`/`trino_upper` divergence.
- Adding any further native functions only where a new DuckDB↔Trino divergence
  is found — the bar for inclusion is "the built-in genuinely disagrees."

## License

MIT, matching the upstream DuckDB extension template.
