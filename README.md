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

`trino_meta()` is the source of truth — 91 entries across 8 categories. A
representative slice (see [`src/trino_function_aliases.sql`](src/trino_function_aliases.sql)
for the full list):

| Category | Functions |
|---|---|
| String (native, ICU) | `trino_lower/1`, `trino_upper/1`, `trino_reverse/1` |
| String (macro) | `trino_length/1`, `trino_substring/{2,3}`, `trino_trim/1`, `trino_ltrim/1`, `trino_rtrim/1`, `trino_replace/3`, `trino_strpos/2`, `trino_starts_with/2`, `trino_lpad/3`, `trino_rpad/3`, `trino_concat_ws/{2..5}`, `trino_translate/3`, `trino_chr/1`, `trino_bit_length/1` |
| Numeric | `trino_abs`, `trino_ceil`, `trino_floor`, `trino_mod`, `trino_power`, `trino_sqrt`, `trino_exp`, `trino_ln`, `trino_log2`, `trino_log10`, `trino_sign`, `trino_pi/0`, `trino_truncate`, `trino_sin/cos/tan/asin/acos/atan/atan2`, `trino_sinh/cosh/tanh`, `trino_degrees/radians`, `trino_cbrt` |
| Bitwise | `trino_bitwise_and/or/not/xor`, `trino_bitwise_left_shift`, `trino_bitwise_right_shift` |
| Regex (RE2 on both sides) | `trino_regexp_like/2`, `trino_regexp_extract/{2,3}`, `trino_regexp_replace/{2,3}` |
| Encoding | `trino_url_encode/decode`, `trino_to_hex` / `trino_from_hex`, `trino_to_base64` / `trino_from_base64` |
| Distance | `trino_levenshtein_distance/2`, `trino_hamming_distance/2` |
| Hash (VARBINARY-wrapped) | `trino_md5`, `trino_sha1`, `trino_sha256` |
| Date | `trino_year/month/day/quarter`, `trino_date_trunc/2`, `trino_date_diff/3`, `trino_day_of_week/year`, `trino_last_day_of_month`, `trino_week/week_of_year`, `trino_year_of_week`, `trino_yow`, `trino_hour/minute/second/millisecond`, `trino_to_unixtime`, `trino_from_unixtime`, `trino_with_timezone` |
| Conditional | `trino_if/{2,3}` |

`trino_with_timezone` requires DuckDB's bundled `icu` extension for `timezone()`;
the load sequence does `INSTALL icu; LOAD icu;` best-effort, so a sandboxed env
without that extension installs fine and only fails if `trino_with_timezone` is
actually called.

## Installation

```sql
INSTALL trino_parity FROM '<repository-url-or-local-build-path>';
LOAD trino_parity;

SELECT trino_lower('İSTANBUL');
-- 'i' + U+0307 + 'stanbul' — matches Trino, not DuckDB's bare lower()

SELECT * FROM trino_meta();
-- 91 rows: name, arity, category
```

Until this extension is published to the
[community-extensions](https://github.com/duckdb/community-extensions) catalog,
loading happens via direct path:

```sql
LOAD '/absolute/path/to/trino_parity.duckdb_extension';
```

with `allow_unsigned_extensions=true` set at DuckDB startup.

## Building

Requires ninja, ccache, and vcpkg (template-pinned commit). One-time bootstrap:

```bash
brew install ninja ccache
git clone https://github.com/Microsoft/vcpkg.git ~/vcpkg
cd ~/vcpkg && git checkout ce613c41372b23b1f51333815feb3edd87ef8a8b
sh ./scripts/bootstrap.sh -disableMetrics
export VCPKG_TOOLCHAIN_PATH=$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake
```

Build:

```bash
git clone --recurse-submodules https://github.com/brikk/duckdb-trino-parity-extension.git
cd duckdb-trino-parity-extension
GEN=ninja make
```

First build is ~30 minutes (DuckDB + statically-linked ICU). Subsequent builds
are seconds with ccache.

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

GitHub Actions builds the cross-platform matrix on every push to `main`. To pull
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

50 assertions covering the Unicode divergence fixtures (Turkish İ, German ß,
decomposed café, ZWJ emoji families, CJK), one spot check per macro category,
and `trino_meta()` shape pins (row count, distinct categories, multi-arity
listings).

The full cross-engine semantic suite lives in the parent connector repo
([`TestTrinoFunctionAliases`](https://github.com/brikk/ducklake-integrations/tree/main/jvm/trino-ducklake/src))
and is what catches drift between the macros here and Trino's documented
behaviour.

## Architecture

Three source files plus the build-time SQL embed:

- `src/string_functions.cpp` — native C++ scalar functions backed by statically
  linked ICU. Currently `trino_lower`, `trino_upper`, `trino_reverse`. Future:
  `trino_normalize/{1,2}` for NFC/NFD/NFKC/NFKD, possibly the trim family.
- `src/trino_function_aliases.sql` — source of truth for the macro layer.
  Edit this file; the build pipeline embeds it via CMake `configure_file`
  into a generated `.cpp` that defines `kTrinoAliasSql` as a raw string.
- `src/alias_macros_loader.cpp` — runs the embedded SQL at `LoadInternal`
  time. Statements are split on top-level semicolons by a small state machine
  that handles `--` line comments and single-quoted literals. `INSTALL` /
  `LOAD` statements are treated as best-effort (Printer::Warning on failure,
  continue); all other failures propagate as `IOException` and abort the
  extension load.
- `src/trino_parity_extension.cpp` — entry point. Registers the native
  functions first, then runs the alias SQL.

ICU is statically linked via vcpkg (ICU 74 components: `uc`, `i18n`, `data`).
This adds ~30MB to the loadable extension binary; the trade-off buys
deterministic Unicode behaviour independent of the host DuckDB build.

## Future work

See [`TODO.md`](TODO.md). Highlights:

- Migrate macros from `Connection::Query` to DuckDB's native `DefaultMacro[]` +
  `DefaultFunctionGenerator::CreateInternalMacroInfo()` helper. This is the
  more idiomatic pattern used by extensions like Query-farm's
  [clickhouse-sql](https://github.com/Query-farm/clickhouse-sql); equivalent
  output, skips the SQL parse loop at load.
- Add `trino_normalize/{1,2}` (NFC/NFD/NFKC/NFKD) — DuckDB only ships `nfc_normalize`.
- Promote `trino_trim`/`ltrim`/`rtrim` from macros (which hand-roll the Java
  `Character.isWhitespace` set as a character argument) to native C++ for
  efficiency and to avoid drift if Java's whitespace set ever changes.

## License

MIT, matching the upstream DuckDB extension template.
