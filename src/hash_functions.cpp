#include "hash_functions.hpp"

#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/types/string_type.hpp"
#include "duckdb/function/scalar_function.hpp"

// Vendored single-file SHA primitives (public domain, WaterJuice/WjCryptLib).
// Compiled as C; declared with C linkage here so the C++ call sites resolve
// against the C definitions.
extern "C" {
#include "WjCryptLib_Sha256.h"
#include "WjCryptLib_Sha512.h"
}

// Vendored xxHash (BSD-2, Cyan4973/xxHash), header-only. XXH_INLINE_ALL makes
// the whole implementation static-inline in this translation unit — no separate
// object file, no exported symbols.
#define XXH_INLINE_ALL
#include "xxhash.h"

#include <cstdint>
#include <cstring>

namespace duckdb {

namespace {

// xxhash64: Trino's xxhash64(varbinary) returns the canonical xxHash64 hash
// (seed 0) serialized as 8 BIG-ENDIAN bytes — VarbinaryFunctions.xxhash64 does
// `setLong(reverseBytes(H))`, i.e. big-endian of H. DuckDB's hashfuncs xxh64()
// is the same algorithm; here we call the upstream XXH64 directly and emit
// big-endian bytes to match Trino byte-for-byte.
void TrinoXxhash64Fun(DataChunk &args, ExpressionState &state, Vector &result) {
	UnaryExecutor::Execute<string_t, string_t>(args.data[0], result, args.size(), [&](string_t input) {
		const XXH64_hash_t h = XXH64(input.GetData(), input.GetSize(), 0);
		uint8_t out[8];
		for (int i = 0; i < 8; i++) {
			out[i] = static_cast<uint8_t>(h >> (56 - 8 * i)); // big-endian
		}
		return StringVector::AddStringOrBlob(result, reinterpret_cast<const char *>(out), 8);
	});
}

// sha512: standard SHA-512 over the raw bytes; 64-byte digest matching Trino's
// sha512(varbinary) -> varbinary.
void TrinoSha512Fun(DataChunk &args, ExpressionState &state, Vector &result) {
	UnaryExecutor::Execute<string_t, string_t>(args.data[0], result, args.size(), [&](string_t input) {
		SHA512_HASH digest;
		Sha512Calculate(input.GetData(), static_cast<uint32_t>(input.GetSize()), &digest);
		return StringVector::AddStringOrBlob(result, reinterpret_cast<const char *>(digest.bytes), SHA512_HASH_SIZE);
	});
}

// HMAC-SHA256 (RFC 2104) over raw bytes. SHA-256 block size is 64 bytes.
void ComputeHmacSha256(const uint8_t *key, size_t key_len, const uint8_t *msg, size_t msg_len, uint8_t out[32]) {
	constexpr size_t kBlock = 64;
	uint8_t k0[kBlock];
	memset(k0, 0, kBlock);
	if (key_len > kBlock) {
		SHA256_HASH key_hash;
		Sha256Calculate(key, static_cast<uint32_t>(key_len), &key_hash);
		memcpy(k0, key_hash.bytes, SHA256_HASH_SIZE);
	} else {
		memcpy(k0, key, key_len);
	}

	uint8_t ipad[kBlock];
	uint8_t opad[kBlock];
	for (size_t i = 0; i < kBlock; i++) {
		ipad[i] = static_cast<uint8_t>(k0[i] ^ 0x36);
		opad[i] = static_cast<uint8_t>(k0[i] ^ 0x5c);
	}

	SHA256_HASH inner;
	{
		Sha256Context ctx;
		Sha256Initialise(&ctx);
		Sha256Update(&ctx, ipad, static_cast<uint32_t>(kBlock));
		Sha256Update(&ctx, msg, static_cast<uint32_t>(msg_len));
		Sha256Finalise(&ctx, &inner);
	}
	{
		Sha256Context ctx;
		SHA256_HASH outer;
		Sha256Initialise(&ctx);
		Sha256Update(&ctx, opad, static_cast<uint32_t>(kBlock));
		Sha256Update(&ctx, inner.bytes, SHA256_HASH_SIZE);
		Sha256Finalise(&ctx, &outer);
		memcpy(out, outer.bytes, SHA256_HASH_SIZE);
	}
}

// hmac_sha256: Trino's hmac_sha256(data, key) — data first, key second, both
// VARBINARY. Computed over raw bytes (NOT routed through any VARCHAR cast), so
// arbitrary binary keys/messages hash correctly — the reason the macro path
// over crypto_hmac (VARCHAR-only) was not pushable.
void TrinoHmacSha256Fun(DataChunk &args, ExpressionState &state, Vector &result) {
	BinaryExecutor::Execute<string_t, string_t, string_t>(
	    args.data[0], args.data[1], result, args.size(), [&](string_t data, string_t key) {
		    uint8_t out[SHA256_HASH_SIZE];
		    ComputeHmacSha256(reinterpret_cast<const uint8_t *>(key.GetData()), key.GetSize(),
		                      reinterpret_cast<const uint8_t *>(data.GetData()), data.GetSize(), out);
		    return StringVector::AddStringOrBlob(result, reinterpret_cast<const char *>(out), SHA256_HASH_SIZE);
	    });
}

} // namespace

void RegisterHashFunctions(ExtensionLoader &loader) {
	loader.RegisterFunction(
	    ScalarFunction("trino_xxhash64", {LogicalType::BLOB}, LogicalType::BLOB, TrinoXxhash64Fun));
	loader.RegisterFunction(ScalarFunction("trino_sha512", {LogicalType::BLOB}, LogicalType::BLOB, TrinoSha512Fun));
	loader.RegisterFunction(ScalarFunction("trino_hmac_sha256", {LogicalType::BLOB, LogicalType::BLOB},
	                                       LogicalType::BLOB, TrinoHmacSha256Fun));
}

} // namespace duckdb
