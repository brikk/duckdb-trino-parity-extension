# EV-E3: Unicode 16 Validation

Development version 0.5.0 upgrades ICU 66.1 / Unicode 13 to ICU 76.1 /
Unicode 16.0 without changing the ten scalar functions or `trino_meta()`.
The API still exposes NFC only. This report records local validation, not
community publication or a completed cross-platform CI matrix.

## Reference Inputs

- ICU: official `release-76-1`, commit `8eca245c7484ac6cc179e3e5f7c1ea7680810f39`.
  Archive checksum, license, import script and isolation settings are documented
  in [the vendor README](../third_party/icu/README.md).
- JDK: OpenJDK 25 GA `25+36-3489`, targeting the `Character` and `Normalizer`
  behavior used by Trino 483. This does not substitute for running the actual
  connector integration suite.
- UCD: Unicode 16.0.0 `NormalizationTest.txt`, SHA-256
  `d811971453e7075e1ad56fb1b301eece5aa80757b81f6156e74a1bfb3ae5ceb1`.
- Runtime: stock Python `duckdb==1.5.5`, Linux x86_64, explicitly loading the
  newly built local extension. Autoinstall and autoload are disabled.
- Stable build source: DuckDB v1.5.5,
  `d8cdaa33fda8df955cc76ef58a280f68f4cd43fa`.

## Results

| Check | Result |
|---|---|
| Loadable extension and native SQL test runner, two build jobs | Built successfully |
| SQL regression suite | 94 assertions passed in two test files |
| Unicode 14/16 regression queries | All 12 passed |
| Lower/upper against JDK over all valid scalar values | 1,112,064 scalars, zero mismatches |
| Trim/ltrim/rtrim, singleton and string-edge positions, against JDK | Zero mismatches |
| NFC on singletons against JDK and Unicode conformance rules | Zero mismatches |
| NFC corpus against Unicode 16 and JDK | 99,825 column checks, zero mismatches |
| Python checker self-tests | Six passed |
| Vendor integrity | 965 imported files match the pinned archive byte-for-byte |
| Dynamic dependencies of Linux artifact | No system ICU library dependency |
| Coexistence with stock DuckDB's `icu` loaded | Both extensions load; Glagolitic and sharp-s checks pass; catalog stays at ten rows |

The scalar enumeration excludes surrogate code points and includes unassigned
values and noncharacters. The NFC corpus covers canonical ordering, blocked
composition, composition exclusions, recursive compositions, Hangul, and
new Unicode 16 characters. Whole strings are normalized by both implementations;
expected strings are not assembled from singleton results.

Regression canaries include Glagolitic U+2C2F/U+2C5F, all 35 Vithkuqi pairs,
Unicode 16 Latin/Cyrillic pairs, and new Todhri, Tulu-Tigalari, Gurung Khema and
Kirat Rai canonical compositions. Java whitespace tests explicitly exclude
U+0085/U+00A0/U+2007/U+202F and include U+001C-U+001F.

## Reproduce

Build with the checked-in DuckDB 1.5.5 submodule and mise toolchain:

```sh
mise install
CMAKE_BUILD_PARALLEL_LEVEL=2 mise exec -- make release
DUCKDB_TEST_MAX_THREADS=2 build/release/test/unittest 'test/*'
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s test -p 'test_unicode*.py'
```

Install `duckdb==1.5.5` in the Python environment used for the oracle. Download
and checksum-verify the pinned JDK archive and normalization corpus listed by
`python3 scripts/check_unicode.py --help`. They are test-only dependencies;
building/loading the extension does not require Java or network access.

```sh
TMPDIR=/tmp/opencode python3 scripts/check_unicode.py \
  --extension build/release/extension/trino_parity/trino_parity.duckdb_extension \
  --java /tmp/opencode/jdk-25/bin/java \
  --normalization-test /tmp/opencode/NormalizationTest-16.0.0.txt
```

The checker prints the binary hash, DuckDB version/platform, JDK runtime, and
reference-data hashes. It exits nonzero on a mismatch or dependency/load error.
Use `--canary-only` with an old 1.5.5-compatible binary as a negative control;
the old Unicode data must fail rather than silently passing against a cached
community artifact.

## DuckDB 2 Preview

A separate source checkout of `v2.0-cyanoptera` was pinned to
`e3946f2327a3cc622e1ec7fe71d51de49f93e61d`. The same extension source built
successfully with these flags and two jobs:

```sh
mise exec -- cmake -S /path/to/pinned-duckdb-2-source -B build/duckdb-2-preview \
  -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DDUCKDB_EXTENSION_CONFIGS="$PWD/extension_config.cmake" \
  -DBUILD_EXTENSIONS_ONLY=ON -DEXTENSION_STATIC_BUILD=OFF \
  -DDUCKDB_RELEASE_LINK_JOBS=1 -DDUCKDB_EXPLICIT_PLATFORM=linux_amd64
mise exec -- cmake --build build/duckdb-2-preview \
  --target trino_parity_loadable_extension --parallel 2
```

This checks source compilation and dynamic-symbol linking against the preview
headers. It does **not** validate loading/execution in the preview or the
production static-link distribution configuration. No stable-ABI migration was
needed for this check; the production submodule/workflow still target 1.5.5.
Full preview runtime checks and the Windows/macOS/Wasm matrix remain release
follow-ups, not reasons to block the independent Unicode-data fix.
