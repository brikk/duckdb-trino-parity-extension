#pragma once

#include "duckdb/catalog/default/default_table_functions.hpp"

namespace duckdb {

// Table macros (currently just trino_meta()). Sentinel-terminated.
extern const DefaultTableMacro kTrinoTableMacros[];

} // namespace duckdb
