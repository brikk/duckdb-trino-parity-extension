// Native DefaultTableMacro[] definitions for the trino_parity extension.
// Registered at extension LOAD time via CreateTableMacroInfo.
//
// SCOPE (as of the passthrough shrink):
// The extension ships ONLY the functions where DuckDB's built-in genuinely
// diverges from Trino and a native C++ implementation is required:
//
//   trino_lower / trino_upper / trino_reverse / trino_trim / trino_ltrim /
//   trino_rtrim / trino_normalize    (ICU-backed, src/string_functions.cpp)
//   trino_xxhash64 / trino_sha512 / trino_hmac_sha256
//                                     (vendored primitives, src/hash_functions.cpp)
//
// Everything else Trino needs is either byte-for-byte identical to a DuckDB
// built-in (`length`, `abs`, `year`, …), a trivial rename (`truncate`→`trunc`),
// an operator (`&`, `<<`), or a one-line SQL rewrite (`regexp_replace(…, 'g')`,
// `isodow(…)`, `unhex(md5(…))`) — all of which the calling layer emits directly
// against DuckDB. Those ~85 functions used to ship here as passthrough macros;
// they were removed because they added a dependency surface without changing
// any behaviour. See docs/RESEARCH-trino-duckdb-function-mapping.md for the
// per-function classification.
//
// The only catalog object registered here is the trino_meta() table macro.

#include "macro_definitions.hpp"

#include "duckdb/common/constants.hpp"

namespace duckdb {

// clang-format off

// trino_meta(): authoritative catalog of the functions this extension provides.
// A calling layer probes it at startup to confirm the divergence-fixing
// functions are present. It lists ONLY the natively-implemented functions —
// the aligned/rename/operator/rewrite cases are the caller's responsibility and
// are not registered by this extension.
const DefaultTableMacro kTrinoTableMacros[] = {
    {DEFAULT_SCHEMA, "trino_meta", {nullptr}, {{nullptr, nullptr}},
     R"sql(
SELECT * FROM (
    VALUES
        ('lower',       1, 'string'),
        ('upper',       1, 'string'),
        ('reverse',     1, 'string'),
        ('trim',        1, 'string'),
        ('ltrim',       1, 'string'),
        ('rtrim',       1, 'string'),
        ('normalize',   1, 'string'),
        ('xxhash64',    1, 'hash'),
        ('sha512',      1, 'hash'),
        ('hmac_sha256', 2, 'hash')
) AS t(trino_name, arg_count, category)
)sql"},

    // Sentinel.
    {nullptr, nullptr, {nullptr}, {{nullptr, nullptr}}, nullptr},
};
// clang-format on

} // namespace duckdb
