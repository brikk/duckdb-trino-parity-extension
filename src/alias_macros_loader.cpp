#include "macro_definitions.hpp"
#include "trino_alias_sql.hpp"

#include "duckdb.hpp"
#include "duckdb/catalog/default/default_functions.hpp"
#include "duckdb/catalog/default/default_table_functions.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

#include <cstring>

namespace duckdb {

namespace {

// Iterate the sentinel-terminated DefaultMacro array, batching consecutive
// entries with the same name into a single CreateMacroInfo so that DuckDB's
// catalog sees them as overloads of one macro (matches the lookup pattern in
// DefaultFunctionGenerator). The scalar-macro array is currently empty (all
// divergence-fixing functions are native C++), so this registers nothing today
// — the loop is kept so future macros can be added by editing the array alone.
void RegisterScalarMacros(ExtensionLoader &loader, const DefaultMacro *macros) {
	for (idx_t i = 0; macros[i].name != nullptr;) {
		idx_t count = 1;
		while (macros[i + count].name != nullptr && std::strcmp(macros[i].name, macros[i + count].name) == 0) {
			++count;
		}
		auto info = DefaultFunctionGenerator::CreateInternalMacroInfo(array_ptr<const DefaultMacro>(macros + i, count));
		loader.RegisterFunction(*info);
		i += count;
	}
}

void RegisterTableMacros(ExtensionLoader &loader, const DefaultTableMacro *macros) {
	for (idx_t i = 0; macros[i].name != nullptr; ++i) {
		auto info = DefaultTableFunctionGenerator::CreateTableMacroInfo(macros[i]);
		loader.RegisterFunction(*info);
	}
}

} // namespace

void RegisterAliasMacros(ExtensionLoader &loader) {
	RegisterScalarMacros(loader, kTrinoMacros);
	RegisterTableMacros(loader, kTrinoTableMacros);
}

} // namespace duckdb
