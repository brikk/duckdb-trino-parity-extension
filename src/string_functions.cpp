#include "string_functions.hpp"

#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"

#include <unicode/unistr.h>
#include <unicode/utf8.h>

#include <cstdint>
#include <string>

namespace duckdb {

namespace {

// Trino's lower() is Java String.toLowerCase(Locale.ROOT): full Unicode
// case folding. DuckDB's built-in lower() does simple case folding, which
// diverges on inputs like U+0130 ('İ', Turkish capital dotted I):
//   DuckDB lower('İ') = 'i'
//   Trino  lower('İ') = 'i' + U+0307   (i + COMBINING DOT ABOVE)
// ICU's u_strToLower with locale "" (root) matches the Java semantics.
void TrinoLowerFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &input = args.data[0];
	UnaryExecutor::Execute<string_t, string_t>(input, result, args.size(), [&](string_t s) {
		icu::UnicodeString us = icu::UnicodeString::fromUTF8(icu::StringPiece(s.GetData(), s.GetSize()));
		us.toLower("");
		std::string out;
		us.toUTF8String(out);
		return StringVector::AddString(result, out);
	});
}

// Trino's upper() is Java String.toUpperCase(Locale.ROOT). DuckDB's upper()
// does simple case folding; the most visible divergence is U+00DF ('ß'):
//   DuckDB upper('ß') = 'ẞ'   (U+1E9E, LATIN CAPITAL LETTER SHARP S)
//   Trino  upper('ß') = 'SS'  (full case folding expands single → two code points)
// ICU's u_strToUpper with root locale matches Java.
void TrinoUpperFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &input = args.data[0];
	UnaryExecutor::Execute<string_t, string_t>(input, result, args.size(), [&](string_t s) {
		icu::UnicodeString us = icu::UnicodeString::fromUTF8(icu::StringPiece(s.GetData(), s.GetSize()));
		us.toUpper("");
		std::string out;
		us.toUTF8String(out);
		return StringVector::AddString(result, out);
	});
}

// Trino's reverse() reverses code points. DuckDB's reverse() reverses
// grapheme clusters, which keeps combining-mark sequences and ZWJ emoji
// families intact and diverges from Trino:
//   input  'cafe' + U+0301  (decomposed café)
//     Trino  → U+0301 + 'e' + 'f' + 'a' + 'c'
//     DuckDB → 'e' + U+0301 + 'f' + 'a' + 'c'
//   input  '👨‍👩‍👧'  (man-ZWJ-woman-ZWJ-girl, 5 code points)
//     Trino  → 'girl-ZWJ-woman-ZWJ-man'
//     DuckDB → '👨‍👩‍👧'  (unchanged — treated as one cluster)
//
// Walk the UTF-8 backward via U8_PREV; for each code point, append its raw
// UTF-8 bytes (intact) to the output. Bytes within a code point stay in
// natural order; only the code-point sequence is reversed.
//
// Note: not using u_strReverse — that reverses UTF-16 code units, which is
// a different granularity again.
void TrinoReverseFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &input = args.data[0];
	UnaryExecutor::Execute<string_t, string_t>(input, result, args.size(), [&](string_t s) {
		const auto *src = reinterpret_cast<const uint8_t *>(s.GetData());
		int32_t len = static_cast<int32_t>(s.GetSize());
		std::string out;
		out.reserve(static_cast<size_t>(len));
		int32_t i = len;
		while (i > 0) {
			int32_t prev = i;
			UChar32 c;
			U8_PREV(src, 0, i, c);
			(void)c;
			out.append(reinterpret_cast<const char *>(src + i), static_cast<size_t>(prev - i));
		}
		return StringVector::AddString(result, out);
	});
}

} // namespace

void RegisterStringFunctions(ExtensionLoader &loader) {
	loader.RegisterFunction(
	    ScalarFunction("trino_lower", {LogicalType::VARCHAR}, LogicalType::VARCHAR, TrinoLowerFun));
	loader.RegisterFunction(
	    ScalarFunction("trino_upper", {LogicalType::VARCHAR}, LogicalType::VARCHAR, TrinoUpperFun));
	loader.RegisterFunction(
	    ScalarFunction("trino_reverse", {LogicalType::VARCHAR}, LogicalType::VARCHAR, TrinoReverseFun));
}

} // namespace duckdb
