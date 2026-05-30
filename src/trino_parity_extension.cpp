#define DUCKDB_EXTENSION_MAIN

#include "trino_parity_extension.hpp"
#include "string_functions.hpp"

#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"

namespace duckdb {

static void LoadInternal(ExtensionLoader &loader) {
	RegisterStringFunctions(loader);
}

void TrinoParityExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}

std::string TrinoParityExtension::Name() {
	return "trino_parity";
}

std::string TrinoParityExtension::Version() const {
#ifdef EXT_VERSION_TRINO_PARITY
	return EXT_VERSION_TRINO_PARITY;
#else
	return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(trino_parity, loader) {
	duckdb::LoadInternal(loader);
}
}
