cmake_minimum_required(VERSION 3.5)

set(TARGET_NAME trino_parity)

# Vendored ICU sources from duckdb/extension/icu/third_party/icu/. We bundle
# the same minimal-ICU snapshot DuckDB ships in its own bundled `icu`
# extension: `common` + `i18n` + a stub data module. This avoids the vcpkg
# dependency entirely (and the build-environment fragility that comes with
# autotools-based ICU compiling under manylinux + gcc-toolset-14 in CI).
#
# Trade-off: stub data means only NFC normalization data is baked in (via
# norm2_nfc_data.h); NFD/NFKC/NFKD are NOT available. `trino_normalize/2`
# is intentionally NOT registered as a result — Trino still evaluates the
# non-NFC forms above-the-scan, just not pushed to DuckDB. trino_lower /
# trino_upper / trino_reverse / trim family / trino_normalize/1 (NFC)
# all work fine with the vendored ICU.
include_directories(third_party/icu/common)
include_directories(third_party/icu/i18n)
add_subdirectory(third_party)

set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})

set(CMAKE_CXX_STANDARD "17" CACHE STRING "C++ standard to enforce")
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(src/include)

# The trino_<name> macros and the trino_meta() table macro are defined as
# native DefaultMacro / DefaultTableMacro arrays in src/macro_definitions.cpp,
# registered via DefaultFunctionGenerator::CreateInternalMacroInfo at extension
# LOAD time. No SQL parse loop, no embedded resource file — same pattern as
# DuckDB's bundled `json` extension and Query-farm/clickhouse-sql.
set(EXTENSION_SOURCES
    src/trino_parity_extension.cpp
    src/string_functions.cpp
    src/alias_macros_loader.cpp
    src/macro_definitions.cpp
    ${ICU_LIBRARY_FILES})

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
