#pragma once

#include "duckdb/catalog/default/default_functions.hpp"
#include "duckdb/catalog/default/default_table_functions.hpp"

namespace duckdb {

// Scalar / helper macros. Sentinel-terminated (entry with name == nullptr).
extern const DefaultMacro kTrinoMacros[];

// Table macros (currently just trino_meta()). Sentinel-terminated.
extern const DefaultTableMacro kTrinoTableMacros[];

} // namespace duckdb
