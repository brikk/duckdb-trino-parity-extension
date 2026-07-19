#define DUCKDB_EXTENSION_MAIN

#include "trino_parity_extension.hpp"
#include "hash_functions.hpp"
#include "string_functions.hpp"
#include "trino_alias_sql.hpp"

#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"

namespace duckdb {

static void LoadInternal(ExtensionLoader &loader) {
	// Native ICU-backed string functions (lower / upper / reverse / trim family /
	// normalize) — the cases where DuckDB's built-ins diverge from Trino on
	// Unicode input.
	RegisterStringFunctions(loader);
	// Native hash functions (trino_xxhash64 / trino_sha512 / trino_hmac_sha256),
	// self-contained over vendored primitives — no community-extension dependency.
	RegisterHashFunctions(loader);
	// The trino_meta() table macro cataloguing the functions above. (No scalar
	// alias macros are shipped; aligned Trino functions are the caller's job.)
	RegisterAliasMacros(loader);
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
