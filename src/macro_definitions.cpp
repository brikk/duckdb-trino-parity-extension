// Native DefaultMacro[] / DefaultTableMacro[] definitions for the
// trino_<name> alias layer. Each entry is registered at extension LOAD time
// via DefaultFunctionGenerator::CreateInternalMacroInfo (this is the same
// path DuckDB's bundled `json` / `icu` extensions use, and the pattern that
// Query-farm/clickhouse-sql follows). No SQL parse loop at load time —
// macros are materialized from these literals directly into the catalog.
//
// Source of truth: this file. The previously-vendored trino_function_aliases.sql
// has been deleted; the explanatory comments live next to each entry below.
//
// Multi-arity macros (substring, concat_ws, regexp_extract, regexp_replace,
// if) appear as consecutive entries with the same name. DuckDB's CreateMacro
// pipeline treats matching adjacent names as overloads of the same macro.
//
// Schema is `main` for everything (DEFAULT_SCHEMA). Function resolution from
// any attached catalog walks the search path and finds them.
//
// trino_lower / trino_upper / trino_reverse are NOT here — they are native
// C++ scalar functions registered separately in string_functions.cpp via
// statically-linked ICU. trino_meta() still lists them as pushable.

#include "macro_definitions.hpp"

#include "duckdb/common/constants.hpp"

namespace duckdb {

// clang-format off
const DefaultMacro kTrinoMacros[] = {
    // ---- Strings ----

    // length(varchar) -> bigint. Code-point count, NOT bytes — aligned in both engines.
    {DEFAULT_SCHEMA, "trino_length", {"s", nullptr}, {{nullptr, nullptr}}, "length(s)"},

    // trim family is implemented natively in src/string_functions.cpp via
    // ICU's u_isWhitespace (Java-aligned Character.isWhitespace set including
    // U+1680, the U+2000-U+200A range minus U+2007, U+2028/U+2029, U+205F,
    // U+3000; explicitly NOT NBSP/FIGURE/NARROW). trino_normalize{/1,/2} is
    // also native (Normalizer2 instances). All four show up in trino_meta()
    // below alongside the macro-backed entries — pushable is pushable, the
    // implementation surface is an internal detail.

    // substring — 1-based code-point indexing, aligned in both engines for positive args.
    {DEFAULT_SCHEMA, "trino_substring", {"s", "start", nullptr},          {{nullptr, nullptr}}, "substring(s, start)"},
    {DEFAULT_SCHEMA, "trino_substring", {"s", "start", "length", nullptr},{{nullptr, nullptr}}, "substring(s, start, length)"},

    {DEFAULT_SCHEMA, "trino_replace",     {"s", "search", "replacement", nullptr}, {{nullptr, nullptr}}, "replace(s, search, replacement)"},
    {DEFAULT_SCHEMA, "trino_strpos",      {"s", "sub", nullptr},                   {{nullptr, nullptr}}, "strpos(s, sub)"},
    {DEFAULT_SCHEMA, "trino_starts_with", {"s", "prefix", nullptr},                {{nullptr, nullptr}}, "starts_with(s, prefix)"},
    {DEFAULT_SCHEMA, "trino_lpad",        {"s", "size", "padstring", nullptr},     {{nullptr, nullptr}}, "lpad(s, size, padstring)"},
    {DEFAULT_SCHEMA, "trino_rpad",        {"s", "size", "padstring", nullptr},     {{nullptr, nullptr}}, "rpad(s, size, padstring)"},

    // concat_ws — DuckDB macros are fixed-arity; expose 2..5 args covering common
    // pushdown shapes. Extend with trino_meta if a real workload calls for more.
    {DEFAULT_SCHEMA, "trino_concat_ws", {"sep", "s1", nullptr},                               {{nullptr, nullptr}}, "concat_ws(sep, s1)"},
    {DEFAULT_SCHEMA, "trino_concat_ws", {"sep", "s1", "s2", nullptr},                         {{nullptr, nullptr}}, "concat_ws(sep, s1, s2)"},
    {DEFAULT_SCHEMA, "trino_concat_ws", {"sep", "s1", "s2", "s3", nullptr},                   {{nullptr, nullptr}}, "concat_ws(sep, s1, s2, s3)"},
    {DEFAULT_SCHEMA, "trino_concat_ws", {"sep", "s1", "s2", "s3", "s4", nullptr},             {{nullptr, nullptr}}, "concat_ws(sep, s1, s2, s3, s4)"},

    {DEFAULT_SCHEMA, "trino_translate", {"source", "src_chars", "dest_chars", nullptr}, {{nullptr, nullptr}}, "translate(source, src_chars, dest_chars)"},
    {DEFAULT_SCHEMA, "trino_chr",        {"n", nullptr}, {{nullptr, nullptr}}, "chr(n)"},
    {DEFAULT_SCHEMA, "trino_bit_length", {"s", nullptr}, {{nullptr, nullptr}}, "bit_length(s)"},

    // URL percent-encoding (RFC 3986). Aligned in both engines.
    {DEFAULT_SCHEMA, "trino_url_encode", {"s", nullptr}, {{nullptr, nullptr}}, "url_encode(s)"},
    {DEFAULT_SCHEMA, "trino_url_decode", {"s", nullptr}, {{nullptr, nullptr}}, "url_decode(s)"},

    // Hex / base64 encode + decode. Trino expects VARBINARY; the connector handles
    // BLOB ↔ VARBINARY at the type layer. Output bytes/chars are identical.
    {DEFAULT_SCHEMA, "trino_to_hex",    {"b", nullptr}, {{nullptr, nullptr}}, "hex(b)"},
    {DEFAULT_SCHEMA, "trino_from_hex",  {"s", nullptr}, {{nullptr, nullptr}}, "unhex(s)"},
    {DEFAULT_SCHEMA, "trino_to_base64", {"b", nullptr}, {{nullptr, nullptr}}, "to_base64(b)"},
    {DEFAULT_SCHEMA, "trino_from_base64",{"s", nullptr}, {{nullptr, nullptr}}, "from_base64(s)"},

    // Distance metrics — DuckDB names lack the _distance suffix; macro renames.
    {DEFAULT_SCHEMA, "trino_levenshtein_distance", {"s1", "s2", nullptr}, {{nullptr, nullptr}}, "levenshtein(s1, s2)"},
    {DEFAULT_SCHEMA, "trino_hamming_distance",     {"s1", "s2", nullptr}, {{nullptr, nullptr}}, "hamming(s1, s2)"},

    // ---- Numeric / math ----

    {DEFAULT_SCHEMA, "trino_abs",   {"x", nullptr}, {{nullptr, nullptr}}, "abs(x)"},
    {DEFAULT_SCHEMA, "trino_ceil",  {"x", nullptr}, {{nullptr, nullptr}}, "ceil(x)"},
    {DEFAULT_SCHEMA, "trino_floor", {"x", nullptr}, {{nullptr, nullptr}}, "floor(x)"},

    // mod: integer-semantics-aligned (truncated division, sign follows dividend).
    // Float mod diverges (Trino IEEE-remainder vs DuckDB fmod); translator gates
    // float mod from pushdown.
    {DEFAULT_SCHEMA, "trino_mod", {"n", "m", nullptr}, {{nullptr, nullptr}}, "mod(n, m)"},

    {DEFAULT_SCHEMA, "trino_power", {"x", "y", nullptr}, {{nullptr, nullptr}}, "power(x, y)"},
    {DEFAULT_SCHEMA, "trino_sqrt",  {"x", nullptr}, {{nullptr, nullptr}}, "sqrt(x)"},
    {DEFAULT_SCHEMA, "trino_exp",   {"x", nullptr}, {{nullptr, nullptr}}, "exp(x)"},
    {DEFAULT_SCHEMA, "trino_ln",    {"x", nullptr}, {{nullptr, nullptr}}, "ln(x)"},
    {DEFAULT_SCHEMA, "trino_log2",  {"x", nullptr}, {{nullptr, nullptr}}, "log2(x)"},
    // Explicit log10 macro — DuckDB's bare log(x) is also log10 (PostgreSQL convention)
    // but Trino's log(b,x) is base-b. We don't expose trino_log to avoid the collision.
    {DEFAULT_SCHEMA, "trino_log10", {"x", nullptr}, {{nullptr, nullptr}}, "log10(x)"},

    {DEFAULT_SCHEMA, "trino_sin",   {"x", nullptr}, {{nullptr, nullptr}}, "sin(x)"},
    {DEFAULT_SCHEMA, "trino_cos",   {"x", nullptr}, {{nullptr, nullptr}}, "cos(x)"},
    {DEFAULT_SCHEMA, "trino_tan",   {"x", nullptr}, {{nullptr, nullptr}}, "tan(x)"},
    {DEFAULT_SCHEMA, "trino_asin",  {"x", nullptr}, {{nullptr, nullptr}}, "asin(x)"},
    {DEFAULT_SCHEMA, "trino_acos",  {"x", nullptr}, {{nullptr, nullptr}}, "acos(x)"},
    {DEFAULT_SCHEMA, "trino_atan",  {"x", nullptr}, {{nullptr, nullptr}}, "atan(x)"},
    {DEFAULT_SCHEMA, "trino_atan2", {"y", "x", nullptr}, {{nullptr, nullptr}}, "atan2(y, x)"},
    {DEFAULT_SCHEMA, "trino_sinh",  {"x", nullptr}, {{nullptr, nullptr}}, "sinh(x)"},
    {DEFAULT_SCHEMA, "trino_cosh",  {"x", nullptr}, {{nullptr, nullptr}}, "cosh(x)"},
    {DEFAULT_SCHEMA, "trino_tanh",  {"x", nullptr}, {{nullptr, nullptr}}, "tanh(x)"},
    {DEFAULT_SCHEMA, "trino_degrees", {"x", nullptr}, {{nullptr, nullptr}}, "degrees(x)"},
    {DEFAULT_SCHEMA, "trino_radians", {"x", nullptr}, {{nullptr, nullptr}}, "radians(x)"},
    {DEFAULT_SCHEMA, "trino_cbrt",  {"x", nullptr}, {{nullptr, nullptr}}, "cbrt(x)"},

    // truncate — Trino name; DuckDB calls it trunc. Renamed via macro body.
    {DEFAULT_SCHEMA, "trino_truncate", {"x", nullptr}, {{nullptr, nullptr}}, "trunc(x)"},

    {DEFAULT_SCHEMA, "trino_sign", {"x", nullptr}, {{nullptr, nullptr}}, "sign(x)"},
    {DEFAULT_SCHEMA, "trino_pi",   {nullptr},      {{nullptr, nullptr}}, "pi()"},

    // bitwise_xor → DuckDB scalar xor(x,y). Other bitwise ops use operator form below.
    {DEFAULT_SCHEMA, "trino_bitwise_xor", {"x", "y", nullptr}, {{nullptr, nullptr}}, "xor(x, y)"},

    // Bitwise function-form → operator-form. Macros bridge the shape.
    // ⚠️ trino_bitwise_right_shift on negative integers: signed/unsigned semantics
    // CAN differ between engines. Safe for typical positive-integer use only.
    {DEFAULT_SCHEMA, "trino_bitwise_and",         {"x", "y", nullptr}, {{nullptr, nullptr}}, "x & y"},
    {DEFAULT_SCHEMA, "trino_bitwise_or",          {"x", "y", nullptr}, {{nullptr, nullptr}}, "x | y"},
    {DEFAULT_SCHEMA, "trino_bitwise_not",         {"x", nullptr},      {{nullptr, nullptr}}, "~x"},
    {DEFAULT_SCHEMA, "trino_bitwise_left_shift",  {"v", "s", nullptr}, {{nullptr, nullptr}}, "v << s"},
    {DEFAULT_SCHEMA, "trino_bitwise_right_shift", {"v", "s", nullptr}, {{nullptr, nullptr}}, "v >> s"},

    // ---- Regex (RE2 on both sides) ----

    // regexp_like → DuckDB's regexp_matches. Rename, semantics aligned with no options.
    {DEFAULT_SCHEMA, "trino_regexp_like", {"s", "pattern", nullptr}, {{nullptr, nullptr}}, "regexp_matches(s, pattern)"},

    // `group` is a DuckDB reserved word; use `group_index` for the parameter name.
    {DEFAULT_SCHEMA, "trino_regexp_extract", {"s", "pattern", nullptr},                {{nullptr, nullptr}}, "regexp_extract(s, pattern)"},
    {DEFAULT_SCHEMA, "trino_regexp_extract", {"s", "pattern", "group_index", nullptr}, {{nullptr, nullptr}}, "regexp_extract(s, pattern, group_index)"},

    // regexp_replace: Trino replaces ALL matches by default; DuckDB defaults to first.
    // Pass the 'g' flag to align. 2-arg form: Trino removes matches → '' replacement.
    {DEFAULT_SCHEMA, "trino_regexp_replace", {"s", "pattern", nullptr},                {{nullptr, nullptr}}, "regexp_replace(s, pattern, '', 'g')"},
    {DEFAULT_SCHEMA, "trino_regexp_replace", {"s", "pattern", "replacement", nullptr}, {{nullptr, nullptr}}, "regexp_replace(s, pattern, replacement, 'g')"},

    // ---- Cryptographic hashes (md5 / sha1 / sha256) ----

    // DuckDB md5/sha1/sha256 return hex VARCHAR; Trino returns VARBINARY. Wrap with
    // unhex() to produce BLOB matching Trino's wire shape.
    {DEFAULT_SCHEMA, "trino_md5",    {"b", nullptr}, {{nullptr, nullptr}}, "unhex(md5(b))"},
    {DEFAULT_SCHEMA, "trino_sha1",   {"b", nullptr}, {{nullptr, nullptr}}, "unhex(sha1(b))"},
    {DEFAULT_SCHEMA, "trino_sha256", {"b", nullptr}, {{nullptr, nullptr}}, "unhex(sha256(b))"},

    // ---- Conditional + dates ----

    // if(cond, t[, f]) — Trino's `if` function. 2-arg form returns NULL when false.
    {DEFAULT_SCHEMA, "trino_if", {"cond", "t", nullptr},      {{nullptr, nullptr}}, "if(cond, t, NULL)"},
    {DEFAULT_SCHEMA, "trino_if", {"cond", "t", "f", nullptr}, {{nullptr, nullptr}}, "if(cond, t, f)"},

    // date_trunc(unit, x) — aligned for second/minute/hour/day/week/month/quarter/year.
    // ⚠️ Return-type caveat: DuckDB always returns TIMESTAMP even on DATE input; Trino
    // preserves DATE → DATE for unit ≥ day. Auto-cast keeps numeric comparisons aligned.
    {DEFAULT_SCHEMA, "trino_date_trunc", {"unit", "x", nullptr},          {{nullptr, nullptr}}, "date_trunc(unit, x)"},
    {DEFAULT_SCHEMA, "trino_date_diff",  {"unit", "t1", "t2", nullptr},   {{nullptr, nullptr}}, "date_diff(unit, t1, t2)"},

    {DEFAULT_SCHEMA, "trino_year",    {"x", nullptr}, {{nullptr, nullptr}}, "year(x)"},
    {DEFAULT_SCHEMA, "trino_month",   {"x", nullptr}, {{nullptr, nullptr}}, "month(x)"},
    {DEFAULT_SCHEMA, "trino_day",     {"x", nullptr}, {{nullptr, nullptr}}, "day(x)"},
    {DEFAULT_SCHEMA, "trino_quarter", {"x", nullptr}, {{nullptr, nullptr}}, "quarter(x)"},

    // day_of_week: Trino ISO (Mon=1..Sun=7). DuckDB's bare dayofweek() is Sun=0..Sat=6.
    // isodow() gives the ISO 1..7 numbering that aligns with Trino.
    {DEFAULT_SCHEMA, "trino_day_of_week",       {"d", nullptr}, {{nullptr, nullptr}}, "isodow(d)"},
    {DEFAULT_SCHEMA, "trino_day_of_year",       {"d", nullptr}, {{nullptr, nullptr}}, "dayofyear(d)"},
    {DEFAULT_SCHEMA, "trino_last_day_of_month", {"d", nullptr}, {{nullptr, nullptr}}, "last_day(d)"},

    // week / week_of_year: DuckDB's bare week(d) is already ISO-aligned.
    {DEFAULT_SCHEMA, "trino_week",         {"d", nullptr}, {{nullptr, nullptr}}, "week(d)"},
    {DEFAULT_SCHEMA, "trino_week_of_year", {"d", nullptr}, {{nullptr, nullptr}}, "week(d)"},

    // year_of_week / yow: ISO-week-numbering year, NOT calendar year.
    {DEFAULT_SCHEMA, "trino_year_of_week", {"d", nullptr}, {{nullptr, nullptr}}, "CAST(extract('isoyear' FROM d) AS BIGINT)"},
    {DEFAULT_SCHEMA, "trino_yow",          {"d", nullptr}, {{nullptr, nullptr}}, "CAST(extract('isoyear' FROM d) AS BIGINT)"},

    {DEFAULT_SCHEMA, "trino_hour",   {"t", nullptr}, {{nullptr, nullptr}}, "hour(t)"},
    {DEFAULT_SCHEMA, "trino_minute", {"t", nullptr}, {{nullptr, nullptr}}, "minute(t)"},
    {DEFAULT_SCHEMA, "trino_second", {"t", nullptr}, {{nullptr, nullptr}}, "second(t)"},

    // millisecond: Trino returns millis-OF-SECOND (0..999), NOT epoch millis.
    {DEFAULT_SCHEMA, "trino_millisecond", {"t", nullptr}, {{nullptr, nullptr}}, "CAST(extract('millisecond' FROM t) AS BIGINT)"},

    // to_unixtime: seconds since 1970-01-01 UTC as DOUBLE.
    {DEFAULT_SCHEMA, "trino_to_unixtime",  {"t", nullptr}, {{nullptr, nullptr}}, "CAST(epoch(t) AS DOUBLE)"},

    // from_unixtime(double) → TIMESTAMP(3) WITH TIME ZONE in Trino. Same absolute
    // instant in both engines; session-zone rendering handled connector-side.
    {DEFAULT_SCHEMA, "trino_from_unixtime", {"d", nullptr}, {{nullptr, nullptr}}, "to_timestamp(d)"},

    // with_timezone(timestamp, varchar). ARG-ORDER FLIP: Trino is (ts, zone);
    // DuckDB is (zone, ts). Requires DuckDB's icu extension to be loaded for the
    // timezone() function — our LoadInternal does INSTALL/LOAD icu best-effort.
    {DEFAULT_SCHEMA, "trino_with_timezone", {"t", "zone", nullptr}, {{nullptr, nullptr}}, "timezone(zone, t)"},

    // Sentinel — must end the array.
    {nullptr, nullptr, {nullptr}, {{nullptr, nullptr}}, nullptr},
};

// ---- Table macros ----

// trino_meta(): authoritative catalog of pushable (name, arity, category) entries.
// The connector's DuckDbExpressionTranslator.PUSHABLE_FUNCTIONS set must mirror
// this exactly; testJavaPushableSetMatchesDuckDbMeta enforces parity.
//
// lower/upper/reverse stay listed — they ARE pushable, just implemented as native
// C++ scalar functions in string_functions.cpp rather than as macros above.
const DefaultTableMacro kTrinoTableMacros[] = {
    {DEFAULT_SCHEMA, "trino_meta", {nullptr}, {{nullptr, nullptr}},
     R"sql(
SELECT * FROM (
    VALUES
        ('lower',                 1, 'string'),
        ('upper',                 1, 'string'),
        ('length',                1, 'string'),
        ('reverse',               1, 'string'),
        ('trim',                  1, 'string'),
        ('ltrim',                 1, 'string'),
        ('rtrim',                 1, 'string'),
        ('substring',             2, 'string'),
        ('substring',             3, 'string'),
        ('replace',               3, 'string'),
        ('strpos',                2, 'string'),
        ('starts_with',           2, 'string'),
        ('lpad',                  3, 'string'),
        ('rpad',                  3, 'string'),
        ('concat_ws',             2, 'string'),
        ('concat_ws',             3, 'string'),
        ('concat_ws',             4, 'string'),
        ('concat_ws',             5, 'string'),
        ('abs',                   1, 'numeric'),
        ('ceil',                  1, 'numeric'),
        ('floor',                 1, 'numeric'),
        ('mod',                   2, 'numeric'),
        ('power',                 2, 'numeric'),
        ('sqrt',                  1, 'numeric'),
        ('exp',                   1, 'numeric'),
        ('ln',                    1, 'numeric'),
        ('log2',                  1, 'numeric'),
        ('log10',                 1, 'numeric'),
        ('sin',                   1, 'numeric'),
        ('cos',                   1, 'numeric'),
        ('tan',                   1, 'numeric'),
        ('asin',                  1, 'numeric'),
        ('acos',                  1, 'numeric'),
        ('atan',                  1, 'numeric'),
        ('atan2',                 2, 'numeric'),
        ('sinh',                  1, 'numeric'),
        ('cosh',                  1, 'numeric'),
        ('tanh',                  1, 'numeric'),
        ('degrees',               1, 'numeric'),
        ('radians',               1, 'numeric'),
        ('cbrt',                  1, 'numeric'),
        ('truncate',              1, 'numeric'),
        ('sign',                  1, 'numeric'),
        ('pi',                    0, 'numeric'),
        ('bitwise_xor',           2, 'numeric'),
        ('bitwise_and',           2, 'numeric'),
        ('bitwise_or',            2, 'numeric'),
        ('bitwise_not',           1, 'numeric'),
        ('bitwise_left_shift',    2, 'numeric'),
        ('bitwise_right_shift',   2, 'numeric'),
        ('translate',             3, 'string'),
        ('chr',                   1, 'string'),
        ('bit_length',            1, 'string'),
        -- normalize/2 (with form arg) is NOT pushable: the vendored ICU only
        -- ships NFC normalization data, so NFD/NFKC/NFKD aren't supported
        -- by the native function. Trino evaluates the 2-arg form above-the-scan.
        ('normalize',             1, 'string'),
        ('regexp_like',           2, 'regex'),
        ('regexp_extract',        2, 'regex'),
        ('regexp_extract',        3, 'regex'),
        ('regexp_replace',        2, 'regex'),
        ('regexp_replace',        3, 'regex'),
        ('url_encode',            1, 'encoding'),
        ('url_decode',            1, 'encoding'),
        ('to_hex',                1, 'encoding'),
        ('from_hex',              1, 'encoding'),
        ('to_base64',             1, 'encoding'),
        ('from_base64',           1, 'encoding'),
        ('levenshtein_distance',  2, 'distance'),
        ('hamming_distance',      2, 'distance'),
        ('md5',                   1, 'hash'),
        ('sha1',                  1, 'hash'),
        ('sha256',                1, 'hash'),
        ('year',                  1, 'date'),
        ('month',                 1, 'date'),
        ('day',                   1, 'date'),
        ('quarter',               1, 'date'),
        ('date_trunc',            2, 'date'),
        ('date_diff',             3, 'date'),
        ('day_of_week',           1, 'date'),
        ('day_of_year',           1, 'date'),
        ('last_day_of_month',     1, 'date'),
        ('week',                  1, 'date'),
        ('week_of_year',          1, 'date'),
        ('year_of_week',          1, 'date'),
        ('yow',                   1, 'date'),
        ('hour',                  1, 'date'),
        ('minute',                1, 'date'),
        ('second',                1, 'date'),
        ('millisecond',           1, 'date'),
        ('to_unixtime',           1, 'date'),
        ('from_unixtime',         1, 'date'),
        ('with_timezone',         2, 'date'),
        ('if',                    2, 'conditional'),
        ('if',                    3, 'conditional')
) AS t(trino_name, arg_count, category)
)sql"},

    // Sentinel.
    {nullptr, nullptr, {nullptr}, {{nullptr, nullptr}}, nullptr},
};
// clang-format on

} // namespace duckdb
