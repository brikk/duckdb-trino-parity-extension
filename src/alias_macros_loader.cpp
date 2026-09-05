#include "macro_definitions.hpp"
#include "trino_alias_sql.hpp"

#include "duckdb.hpp"
#include "duckdb/catalog/default/default_table_functions.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

namespace {

void RegisterTableMacros(ExtensionLoader &loader, const DefaultTableMacro *macros) {
	for (idx_t i = 0; macros[i].name != nullptr; ++i) {
		auto info = DefaultTableFunctionGenerator::CreateTableMacroInfo(macros[i]);
		loader.RegisterFunction(*info);
	}
}

} // namespace

void RegisterAliasMacros(ExtensionLoader &loader) {
	RegisterTableMacros(loader, kTrinoTableMacros);
}

} // namespace duckdb
