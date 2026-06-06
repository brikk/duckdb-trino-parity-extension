#pragma once

#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

// Native, self-contained hash scalar functions registered by the trino_parity
// extension:
//   trino_xxhash64(blob)        -> blob(8)   xxHash64, big-endian (matches Trino)
//   trino_sha512(blob)          -> blob(64)  SHA-512
//   trino_hmac_sha256(data,key) -> blob(32)  HMAC-SHA256 over raw bytes
//
// These replace the former macro layer that wrapped the `hashfuncs` (xxh64) and
// `crypto` (crypto_hash / crypto_hmac) community extensions. Implemented over
// vendored single-file primitives (see third_party/hash/) so there is no
// runtime dependency on those extensions — and none of their load-time
// telemetry. HMAC operates on raw VARBINARY bytes, which the VARCHAR-only
// `crypto_hmac` could not do (the macro path silently mangled non-UTF-8 input).
void RegisterHashFunctions(ExtensionLoader &loader);

} // namespace duckdb
