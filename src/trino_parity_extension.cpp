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
	// Native ICU-backed functions first (lower / upper / reverse), so they
	// already exist as catalog entries when the alias SQL runs and any future
	// macros that try to overwrite them get a clear conflict error.
	RegisterStringFunctions(loader);
	// Native hash functions (trino_xxhash64 / trino_sha512 / trino_hmac_sha256),
	// self-contained over vendored primitives — no community-extension dependency.
	RegisterHashFunctions(loader);
	// SQL alias macros (trino_length, trino_substring, ..., trino_meta).
	// Sourced from src/trino_function_aliases.sql, embedded at build time.
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
