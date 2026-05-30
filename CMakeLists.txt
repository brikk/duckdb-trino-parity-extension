cmake_minimum_required(VERSION 3.5)

set(TARGET_NAME trino_parity)

# ICU provides the Unicode case-folding and UTF-8 iteration primitives used
# by trino_lower / trino_upper / trino_reverse. Linked statically via vcpkg
# so the extension is self-contained at LOAD time. `data` carries the
# Unicode tables (~30MB); without it the link errors on _icudt<ver>_dat.
find_package(ICU REQUIRED COMPONENTS uc i18n data)

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
    src/macro_definitions.cpp)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

target_link_libraries(${EXTENSION_NAME} ICU::i18n ICU::uc ICU::data)
target_link_libraries(${LOADABLE_EXTENSION_NAME} ICU::i18n ICU::uc ICU::data)

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
