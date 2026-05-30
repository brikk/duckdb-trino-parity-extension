#include "macro_definitions.hpp"
#include "trino_alias_sql.hpp"

#include "duckdb.hpp"
#include "duckdb/catalog/default/default_functions.hpp"
#include "duckdb/catalog/default/default_table_functions.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/printer.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

#include <cstring>

namespace duckdb {

namespace {

// Iterate the sentinel-terminated DefaultMacro array, batching consecutive
// entries with the same name into a single CreateMacroInfo so that DuckDB's
// catalog sees them as overloads of one macro (matches the lookup pattern in
// DefaultFunctionGenerator).
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

// Best-effort INSTALL/LOAD of DuckDB's bundled icu extension. trino_with_timezone
// resolves to DuckDB's timezone(zone, ts) which lives in icu; without it that
// macro would fail at call time. In sandboxed environments with no network and
// no on-disk extension cache, INSTALL fails silently — every other macro still
// works, and trino_with_timezone is the only thing affected.
void EnsureIcu(DatabaseInstance &db) {
	Connection con(db);
	auto install = con.Query("INSTALL icu");
	if (install->HasError()) {
		Printer::PrintF("[trino_parity] INSTALL icu failed, continuing without it — %s\n", install->GetError());
		return;
	}
	auto load = con.Query("LOAD icu");
	if (load->HasError()) {
		Printer::PrintF("[trino_parity] LOAD icu failed, continuing without it — %s\n", load->GetError());
	}
}

} // namespace

void RegisterAliasMacros(ExtensionLoader &loader) {
	auto &db = loader.GetDatabaseInstance();

	RegisterScalarMacros(loader, kTrinoMacros);
	RegisterTableMacros(loader, kTrinoTableMacros);

	EnsureIcu(db);
}

} // namespace duckdb
