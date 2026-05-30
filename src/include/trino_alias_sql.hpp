#pragma once

namespace duckdb {

// Embedded contents of src/trino_function_aliases.sql, injected at build time
// by CMake configure_file. Defined in the generated alias_macros.cpp.
extern const char *const kTrinoAliasSql;

void RegisterAliasMacros(class ExtensionLoader &loader);

} // namespace duckdb
