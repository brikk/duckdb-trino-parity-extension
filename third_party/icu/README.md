# Vendored ICU 76.1 / Unicode 16.0

## Upstream Pin

- Project: https://github.com/unicode-org/icu
- Release/tag: `release-76-1` (ICU4C 76.1)
- Tag commit: `8eca245c7484ac6cc179e3e5f7c1ea7680810f39`
- Archive: https://github.com/unicode-org/icu/releases/download/release-76-1/icu4c-76_1-src.tgz
- SHA-256: `dfacb46bfe4747410472ce3e1144bf28a102feeaa4e3875bac9b4c6cf30f4f3e`
- License: upstream `LICENSE`, including Unicode License V3 and third-party notices.

All files under `common/`, `i18n/`, and `stubdata/`, and `LICENSE`, are copied
byte-for-byte from that archive. No local algorithm or data patches are applied.
Only `.cpp`, `.h`, and `sources.txt` files are imported from the three source
directories. Tools, tests, build-system files, and the full ICU data archive are
not vendored.

## Source And Data Arrangement

CMake builds all 201 common and 254 i18n translation units from the upstream
`sources.txt` manifests, plus `stubdata/stubdata.cpp`, into private, PIC object
libraries. ICU 76 requires C++17; the extension sets it locally for its consumers
as well, without changing DuckDB's cached default (CMake 3.8 or newer is needed).
These are ordinary translation units even when
DuckDB enables unity builds; the previous snapshot's unity-specific identifier
renames, commented-out declarations, and source edits are no longer needed.

`uconfig_local.h` is loaded through upstream's `UCONFIG_USE_LOCAL` hook for both
ICU and extension consumers. It preserves the previous exclusions for conversion,
break iteration, IDNA, transliteration, regular expressions, and services.
Implementation macros are now scoped to their own common/i18n targets rather
than defining `U_COMMON_IMPLEMENTATION` in every consumer.

Upstream symbol renaming is enabled, unlike the old commented-out `urename.h`.
The custom `_trino_parity` suffix isolates C symbols (for example,
`u_tolower_76_trino_parity`), the C++ namespace (`icu_76_trino_parity`, still
accessible as `icu`), and the stub data symbol (`icudt_trino_parity76_dat`).
Target names are extension-specific too, so DuckDB's bundled ICU can coexist.
The global `UCaseMap` C++ helper is also renamed through the local configuration
because it is outside upstream's renaming table and can collide in static links.
Object libraries also use hidden visibility for internal helper symbols that
are not covered by ICU's public renaming table.

The Unicode 16 case and character-property tables (`ucase_props_data.h`,
`uchar_props_data.h`, and associated upstream tables) are compiled into common.
The extension continues to use `u_tolower`/`u_toupper` for simple one-code-point
case mapping and `u_isWhitespace` for Java whitespace, not locale-sensitive full
string case mapping or the Unicode White_Space property.

Upstream `normalizer2.cpp` compiles `norm2_nfc_data.h` directly via
`NORM2_HARDCODE_NFC_DATA=1`. **NFC and NFD share this canonical normalization
data**; NFD does not require another table. The extension's public SQL API is
still deliberately NFC-only (`trino_normalize/1`); no two-argument overload is
introduced. NFKC/NFKD need compatibility normalization data that is not bundled.

The old file named `stubdata.cpp` actually contained a generated DuckDB resource
bundle. It is replaced by ICU's real, empty, aligned stub data header. No locale,
collation, timezone, or compatibility-normalization resource bundle is included;
those ICU facilities are not part of this extension's public API. Compiling the
complete library source lists does not make data-dependent facilities available.

`U_STATIC_IMPLEMENTATION`, `UCONFIG_NO_FILE_IO=1`, and `U_ENABLE_DYLOAD=0` ensure
there is no system ICU linkage or external ICU data/plugin loading. The supported
string operations are self-contained. CMake and extension installation/loading
do not download anything or invoke the vendoring script.

## Reproduce The Import

From the repository root, explicitly download the pinned archive and run the
maintainer-only importer (Python 3 standard library; no system ICU or ICU tools):

```sh
curl -fL -o /tmp/opencode/icu4c-76_1-src.tgz \
  https://github.com/unicode-org/icu/releases/download/release-76-1/icu4c-76_1-src.tgz
python3 third_party/icu/vendor.py /tmp/opencode/icu4c-76_1-src.tgz
python3 third_party/icu/vendor.py --check /tmp/opencode/icu4c-76_1-src.tgz
```

The importer verifies the archive hash before changing anything and replaces
only the three imported directories and `LICENSE`. Keep local configuration and
build files outside those directories. `--check` verifies file contents and the
exact imported file set without writing. A normal offline build needs neither
Python nor the archive; all required sources and generated Unicode tables are
already checked in.
