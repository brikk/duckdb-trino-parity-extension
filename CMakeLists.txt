cmake_minimum_required(VERSION 3.8)

set(TARGET_NAME trino_parity)

# Pinned ICU 76.1 / Unicode 16.0 sources and embedded property/case/NFC data.
# No system ICU, external data files, or build/load-time downloads are needed.
# NFC and NFD share norm2_nfc_data.h; the SQL API intentionally exposes only
# trino_normalize/1 (NFC). NFKC/NFKD data is not bundled. See third_party/icu/README.md.
# Use the same local configuration for ICU and its consumers, and prefer these
# headers over any ICU headers inherited from DuckDB or the build environment.
add_definitions(-DUCONFIG_USE_LOCAL=1)
include_directories(BEFORE third_party/icu third_party/icu/common third_party/icu/i18n)
# Vendored single-file hash primitives backing the native trino_xxhash64 /
# trino_sha512 / trino_hmac_sha256 scalar functions (see
# third_party/hash/THIRD_PARTY_NOTICES.md): xxHash (BSD-2, header-only) and
# WjCryptLib SHA-256/512 (public domain). Replaces the former runtime
# dependency on the `hashfuncs` / `crypto` community extensions.
include_directories(third_party/hash)
add_subdirectory(third_party)

set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})

# Override DuckDB's cached C++11 default in this directory only. ICU 76 public
# headers require C++17 in the extension consumers as well as the ICU objects.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(src/include)

# Only trino_meta() is a table macro; all ten scalar functions are native.
set(EXTENSION_SOURCES
    src/trino_parity_extension.cpp
    src/string_functions.cpp
    src/hash_functions.cpp
    src/alias_macros_loader.cpp
    src/macro_definitions.cpp
    third_party/hash/WjCryptLib_Sha256.c
    third_party/hash/WjCryptLib_Sha512.c
    ${ICU_LIBRARY_FILES})

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
