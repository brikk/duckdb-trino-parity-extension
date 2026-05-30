#define DUCKDB_EXTENSION_MAIN

#include "trino_parity_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>

// OpenSSL linked through vcpkg
#include <openssl/opensslv.h>

namespace duckdb {

inline void TrinoParityScalarFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &name_vector = args.data[0];
	UnaryExecutor::Execute<string_t, string_t>(name_vector, result, args.size(), [&](string_t name) {
		return StringVector::AddString(result, "...........🦆 " + name.GetString());
	});
}

inline void TrinoParityOpenSSLVersionScalarFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &name_vector = args.data[0];
	UnaryExecutor::Execute<string_t, string_t>(name_vector, result, args.size(), [&](string_t name) {
		return StringVector::AddString(result, "TrinoParity " + name.GetString() + ", my linked OpenSSL version is " +
		                                           OPENSSL_VERSION_TEXT);
	});
}

static void LoadInternal(ExtensionLoader &loader) {
	// Register a scalar function
	auto trino_parity_scalar_function =
	    ScalarFunction("trino_parity", {LogicalType::VARCHAR}, LogicalType::VARCHAR, TrinoParityScalarFun);

	loader.RegisterFunction(trino_parity_scalar_function);

	// Register another scalar function
	auto trino_parity_openssl_version_scalar_function = ScalarFunction("trino_parity_openssl_version", {LogicalType::VARCHAR},
	                                                             LogicalType::VARCHAR, TrinoParityOpenSSLVersionScalarFun);
	loader.RegisterFunction(trino_parity_openssl_version_scalar_function);
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
