#pragma once

namespace duckdb {

// Registers the trino_meta() table macro on the DatabaseInstance. The ten
// trino_<name> functions are native scalars registered separately. The
// historical function/header name survives from the pre-0.2 macro catalog.
void RegisterAliasMacros(class ExtensionLoader &loader);

} // namespace duckdb
