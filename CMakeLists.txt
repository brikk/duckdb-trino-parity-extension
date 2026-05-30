cmake_minimum_required(VERSION 3.5)

set(TARGET_NAME trino_parity)

# ICU provides the Unicode case-folding and UTF-8 iteration primitives used
# by trino_lower / trino_upper / trino_reverse. Linked statically via vcpkg
# so the extension is self-contained at LOAD time.
find_package(ICU REQUIRED COMPONENTS uc i18n)

set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})

set(CMAKE_CXX_STANDARD "17" CACHE STRING "C++ standard to enforce")
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(src/include)

set(EXTENSION_SOURCES
    src/trino_parity_extension.cpp
    src/string_functions.cpp)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

target_link_libraries(${EXTENSION_NAME} ICU::uc ICU::i18n)
target_link_libraries(${LOADABLE_EXTENSION_NAME} ICU::uc ICU::i18n)

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
