# trino_parity — DuckDB ↔ Trino function parity

A DuckDB extension that registers `trino_<name>(...)` scalar functions whose
semantics match Trino's documented behaviour, even on Unicode and other
edge-case inputs where DuckDB's built-ins diverge.

Designed to be loaded server-side by anything that pushes Trino-shaped
predicates down to DuckDB. The first consumer is
[ducklake-integrations / trino-ducklake](https://github.com/brikk/ducklake-integrations);
a future trino-duckdb (direct) connector will load it the same way.

## Why this exists

Trino implements `lower()`, `upper()`, `reverse()`, regex semantics, etc. in
terms of Java's Unicode 15 tables. DuckDB has its own implementations that
agree on ASCII but disagree on real-world Unicode input. If a Trino connector
naively pushes `WHERE lower(name) = 'apple'` down to DuckDB, the filter runs
with different semantics in each engine — and rows visible to Trino can
silently disappear from the result.

This extension is the **interpretation layer**: the Trino connector emits
`trino_<name>(...)` calls instead of bare Trino built-ins, and each `trino_*`
in this extension resolves to either a native C++ implementation (when DuckDB's
built-in diverges) or a thin macro over the equivalent DuckDB built-in (when
they agree). The catalog table macro `trino_meta()` is the authoritative list
the connector reads to decide what's pushable.

### Concrete divergences pinned today

| Function | DuckDB built-in | Trino spec | Extension result |
|---|---|---|---|
| `lower('İ')` (U+0130) | `'i'` (1 cp, simple case folding) | `'i'` + U+0307 (2 cp, full case folding) | matches Trino |
| `upper('ß')` (U+00DF) | `'ẞ'` (U+1E9E, 1 cp) | `'SS'` (2 cp) | matches Trino |
| `upper('straße strauß')` | `'STRAẞE STRAUẞ'` | `'STRASSE STRAUSS'` | matches Trino |
| `reverse('cafe' + U+0301)` | grapheme-aware (combining mark stays glued to `e`) | code-point-only (mark moves to front) | matches Trino |
| `reverse('👨‍👩‍👧')` (ZWJ family) | unchanged (one cluster) | reversed across ZWJ boundaries | matches Trino |
| `regexp_replace(s, p, r)` | first match only | global by default | macro forces `'g'` flag |
| `day_of_week(d)` | `dayofweek` = 0..6 with Sun=0 | ISO 1..7 with Mon=1 | macro uses `isodow` |

The full divergence catalog and its empirical-verification corpus live in
[`REPORT-string-unicode-audit.md`](https://github.com/brikk/ducklake-integrations/blob/main/jvm/trino-ducklake/dev-docs/REPORT-string-unicode-audit.md)
on the parent connector repo.

## Function inventory

`trino_meta()` is the source of truth — 92 entries across 8 categories. A
representative slice (full list lives in
[`src/macro_definitions.cpp`](src/macro_definitions.cpp) under
`kTrinoMacros[]` + `kTrinoTableMacros[]`):

| Category | Functions |
|---|---|
| String (native, ICU) | `trino_lower/1`, `trino_upper/1`, `trino_reverse/1`, `trino_trim/1`, `trino_ltrim/1`, `trino_rtrim/1`, `trino_normalize/{1,2}` |
| String (macro) | `trino_length/1`, `trino_substring/{2,3}`, `trino_replace/3`, `trino_strpos/2`, `trino_starts_with/2`, `trino_lpad/3`, `trino_rpad/3`, `trino_concat_ws/{2..5}`, `trino_translate/3`, `trino_chr/1`, `trino_bit_length/1` |
| Numeric | `trino_abs`, `trino_ceil`, `trino_floor`, `trino_mod`, `trino_power`, `trino_sqrt`, `trino_exp`, `trino_ln`, `trino_log2`, `trino_log10`, `trino_sign`, `trino_pi/0`, `trino_truncate`, `trino_sin/cos/tan/asin/acos/atan/atan2`, `trino_sinh/cosh/tanh`, `trino_degrees/radians`, `trino_cbrt` |
| Bitwise | `trino_bitwise_and/or/not/xor`, `trino_bitwise_left_shift`, `trino_bitwise_right_shift` |
| Regex (RE2 on both sides) | `trino_regexp_like/2`, `trino_regexp_extract/{2,3}`, `trino_regexp_replace/{2,3}` |
| Encoding | `trino_url_encode/decode`, `trino_to_hex` / `trino_from_hex`, `trino_to_base64` / `trino_from_base64` |
| Distance | `trino_levenshtein_distance/2`, `trino_hamming_distance/2` |
| Hash (VARBINARY-wrapped) | `trino_md5`, `trino_sha1`, `trino_sha256` |
| Date | `trino_year/month/day/quarter`, `trino_date_trunc/2`, `trino_date_diff/3`, `trino_day_of_week/year`, `trino_last_day_of_month`, `trino_week/week_of_year`, `trino_year_of_week`, `trino_yow`, `trino_hour/minute/second/millisecond`, `trino_to_unixtime`, `trino_from_unixtime`, `trino_with_timezone` |
| Conditional | `trino_if/{2,3}` |

The native string functions all use the statically-linked ICU and match
Java's Unicode semantics exactly:

- `trino_lower` / `trino_upper`: full case folding via `u_strToLower` /
  `u_strToUpper` with root locale (Turkish `İ` → `'i'` + U+0307, German
  `ß` → `'SS'`).
- `trino_reverse`: code-point reverse via `U8_PREV` (combining marks
  detach, ZWJ emoji reverse boundary-by-boundary).
- `trino_trim` / `trino_ltrim` / `trino_rtrim`: skip code points where
  `u_isWhitespace` is true (Java's `Character.isWhitespace` — NBSP /
  U+2007 / U+202F intentionally NOT stripped).
- `trino_normalize/1`: `icu::Normalizer2::getNFCInstance()` — vendored
  ICU ships only NFC's static data, so this is the only form registered.
  The 2-arg form (`'NFC'` / `'NFD'` / `'NFKC'` / `'NFKD'` selector) was
  pruned with the vendored-ICU migration; the connector's pushable set
  matches.

`trino_with_timezone` requires DuckDB's bundled `icu` extension for
`timezone()`; the load sequence does `INSTALL icu; LOAD icu;` best-effort,
so a sandboxed env without that extension installs fine and only fails if
`trino_with_timezone` is actually called.

## Installation

```sql
INSTALL trino_parity FROM '<repository-url-or-local-build-path>';
LOAD trino_parity;

SELECT trino_lower('İSTANBUL');
-- 'i' + U+0307 + 'stanbul' — matches Trino, not DuckDB's bare lower()

SELECT * FROM trino_meta();
-- 92 rows: name, arity, category
```

Until this extension is published to the
[community-extensions](https://github.com/duckdb/community-extensions) catalog,
loading happens via direct path:

```sql
LOAD '/absolute/path/to/trino_parity.duckdb_extension';
```

with `allow_unsigned_extensions=true` set at DuckDB startup.

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

The connector's Quack server runs as a Linux testcontainer; on a macOS dev box
a host-built (darwin-arm64) `.duckdb_extension` cannot be loaded there. Two
make targets build Linux variants inside a Docker container:

```bash
make linux-arm64    # native on Apple Silicon; ~10-15 min first run, seconds thereafter
make linux-amd64    # slower under Rosetta/qemu on Apple Silicon
make all-platforms  # both
```

Output layout (one .duckdb_extension per platform, all optional, all
independently consumable by the trino-ducklake plugin jar's gradle bundling):

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

68 sqllogic assertions covering the Unicode divergence fixtures (Turkish İ,
German ß, decomposed café, ZWJ emoji families, CJK), the trim whitespace set
(Java vs. ICU coverage), NFC normalization, one spot check per macro
category, and `trino_meta()` shape pins (row count, distinct categories,
multi-arity listings).

The full cross-engine semantic suite lives in the parent connector repo
([`TestTrinoFunctionAliases`](https://github.com/brikk/ducklake-integrations/tree/main/jvm/trino-ducklake/src))
and is what catches drift between the macros here and Trino's documented
behaviour.

## Architecture

Four source files:

- `src/string_functions.cpp` — native C++ scalar functions backed by
  the statically-linked vendored ICU: `trino_lower`, `trino_upper`, `trino_reverse`,
  `trino_trim`, `trino_ltrim`, `trino_rtrim`, `trino_normalize/{1,2}`.
  These are the places where DuckDB's built-ins diverge from Trino on
  real-world Unicode input; rewriting in C++ via ICU pins exact Java
  semantics.
- `src/macro_definitions.cpp` — `DefaultMacro[] kTrinoMacros` and
  `DefaultTableMacro[] kTrinoTableMacros` arrays. Each entry maps a
  Trino call shape to its DuckDB body; multi-overload macros sit as
  consecutive same-name entries. This is the source of truth for the
  macro layer — the historical `.sql` file is gone.
- `src/alias_macros_loader.cpp` — `RegisterAliasMacros(loader)` iterates
  the arrays and registers each via
  `DefaultFunctionGenerator::CreateInternalMacroInfo` /
  `DefaultTableFunctionGenerator::CreateTableMacroInfo` (the same path
  DuckDB's bundled `json` extension uses). No SQL parse loop. Also runs
  a small `INSTALL icu; LOAD icu;` best-effort so
  `trino_with_timezone` can resolve DuckDB's `timezone()`.
- `src/trino_parity_extension.cpp` — entry point. Registers the native
  functions first, then the macros.

ICU is vendored under `third_party/icu/` — a snapshot of DuckDB's bundled
ICU (`common` + `i18n` + `stubdata`). Statically linked into the loadable
extension binary (adds ~30MB) so Unicode behaviour is independent of the
host DuckDB build. The vendored snapshot ships only the NFC normalization
data; NFD/NFKC/NFKD are intentionally out of scope, and the connector's
pushable set tracks accordingly.

## Future work

See [`TODO.md`](TODO.md). Headline items:

- Publishing to the
  [community-extensions](https://github.com/duckdb/community-extensions)
  catalog, enabling `INSTALL trino_parity FROM community;` for operators.
- Consolidating the remaining macro entries that wrap DuckDB built-ins
  (`from_hex` / `unhex` etc.) — clean-up, not correctness.

## License

MIT, matching the upstream DuckDB extension template.
