#include "string_functions.hpp"

#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"

#include <unicode/locid.h>
#include <unicode/normalizer2.h>
#include <unicode/uchar.h>
#include <unicode/unistr.h>
#include <unicode/utf8.h>

#include <cstdint>
#include <string>

namespace duckdb {

namespace {

// Case mapping model
// ------------------
// Trino's lower()/upper() are NOT Java String.toLowerCase/toUpperCase(Locale.ROOT).
// io.trino.operator.scalar.StringFunctions delegates to airlift's
// SliceUtf8.toLowerCase/toUpperCase, which walk the string one code point at a
// time and apply Character.toLowerCase(int) / Character.toUpperCase(int) — the
// SIMPLE, 1:1, context-free case mapping from UnicodeData.txt. Consequences
// (all verified against Trino 483 on 2026-09-02):
//   upper('ß')       = 'ß'   (U+00DF has no simple uppercase; NOT 'SS', NOT U+1E9E)
//   lower('İ')       = 'i'   (U+0130 -> U+0069; NOT 'i' + U+0307)
//   lower('ΟΔΥΣΣΕΥΣ') = 'οδυσσευσ' (no final-sigma rule; every Σ -> σ)
//   upper('ﬁ')       = 'ﬁ'   (U+FB01 ligature has no simple uppercase)
//   upper('ᾀ')       = 'ᾈ'   (U+1F80 -> U+1F88, a 1:1 mapping, NOT 'ἈΙ')
// ICU 76.1's per-code-point mappings are checked exhaustively against pinned
// JDK 25 by scripts/check_unicode.py; Unicode-version equality alone is not
// a parity guarantee. Full string case
// mapping (UnicodeString::toLower/toUpper, u_strToLower/u_strToUpper) applies
// SpecialCasing.txt 1:N and contextual rules and diverges on every line above.
//
// Why not DuckDB's built-in lower()/upper()? utf8proc also does simple mapping
// but special-cases upper('ß') = 'ẞ' (U+1E9E), which Trino does not.
//
// Invalid UTF-8 cannot reach here (DuckDB validates VARCHAR); if U8_NEXT ever
// reports a negative code point we copy the offending bytes through unchanged.

inline UChar32 SimpleToLower(UChar32 c) {
	return u_tolower(c);
}

inline UChar32 SimpleToUpper(UChar32 c) {
	return u_toupper(c);
}

template <UChar32 (*MapCodePoint)(UChar32)>
inline std::string MapCodePoints(const char *data, idx_t size) {
	const auto *src = reinterpret_cast<const uint8_t *>(data);
	int32_t len = static_cast<int32_t>(size);
	std::string out;
	out.reserve(static_cast<size_t>(len));
	int32_t i = 0;
	while (i < len) {
		int32_t prev = i;
		UChar32 c;
		U8_NEXT(src, i, len, c);
		if (c < 0) {
			out.append(reinterpret_cast<const char *>(src + prev), static_cast<size_t>(i - prev));
			continue;
		}
		UChar32 mapped = MapCodePoint(c);
		if (mapped == c) {
			out.append(reinterpret_cast<const char *>(src + prev), static_cast<size_t>(i - prev));
			continue;
		}
		uint8_t buf[U8_MAX_LENGTH];
		int32_t n = 0;
		UBool is_error = false;
		U8_APPEND(buf, n, U8_MAX_LENGTH, mapped, is_error);
		if (is_error) {
			// Unreachable for valid scalar values; fall back to the unmapped bytes.
			out.append(reinterpret_cast<const char *>(src + prev), static_cast<size_t>(i - prev));
			continue;
		}
		out.append(reinterpret_cast<const char *>(buf), static_cast<size_t>(n));
	}
	return out;
}

void TrinoLowerFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &input = args.data[0];
	UnaryExecutor::Execute<string_t, string_t>(input, result, args.size(), [&](string_t s) {
		return StringVector::AddString(result, MapCodePoints<SimpleToLower>(s.GetData(), s.GetSize()));
	});
}

void TrinoUpperFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &input = args.data[0];
	UnaryExecutor::Execute<string_t, string_t>(input, result, args.size(), [&](string_t s) {
		return StringVector::AddString(result, MapCodePoints<SimpleToUpper>(s.GetData(), s.GetSize()));
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

// Trim family: strip Java-whitespace code points from both/left/right ends.
// Trino's TRIM uses Java's String.strip() semantics, which is
// Character.isWhitespace — broader than DuckDB's bare trim (only strips ASCII
// space + EM SPACE) and narrower than Unicode's White_Space property
// (excludes NBSP, FIGURE SPACE, NARROW NBSP — Java treats those as
// "non-breaking" and NOT whitespace).
//
// ICU's u_isWhitespace() implements exactly Java's Character.isWhitespace
// (documented in icu/uchar.h). Walking the UTF-8 via U8_NEXT / U8_PREV
// gives the right code-point granularity — no risk of cutting mid-codepoint.

// Find first byte offset whose code point is NOT whitespace.
inline int32_t SkipLeadingWhitespace(const uint8_t *src, int32_t len) {
	int32_t i = 0;
	while (i < len) {
		int32_t prev = i;
		UChar32 c;
		U8_NEXT(src, i, len, c);
		if (c < 0 || !u_isWhitespace(c)) {
			return prev;
		}
	}
	return len;
}

// Find byte offset (exclusive end) just past the last non-whitespace code point.
inline int32_t SkipTrailingWhitespace(const uint8_t *src, int32_t start, int32_t len) {
	int32_t i = len;
	while (i > start) {
		int32_t prev = i;
		UChar32 c;
		U8_PREV(src, start, i, c);
		if (c < 0 || !u_isWhitespace(c)) {
			return prev;
		}
	}
	return start;
}

void TrinoTrimFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &input = args.data[0];
	UnaryExecutor::Execute<string_t, string_t>(input, result, args.size(), [&](string_t s) {
		const auto *src = reinterpret_cast<const uint8_t *>(s.GetData());
		int32_t len = static_cast<int32_t>(s.GetSize());
		int32_t start = SkipLeadingWhitespace(src, len);
		int32_t end = SkipTrailingWhitespace(src, start, len);
		return StringVector::AddString(result, reinterpret_cast<const char *>(src + start),
		                               static_cast<idx_t>(end - start));
	});
}

void TrinoLtrimFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &input = args.data[0];
	UnaryExecutor::Execute<string_t, string_t>(input, result, args.size(), [&](string_t s) {
		const auto *src = reinterpret_cast<const uint8_t *>(s.GetData());
		int32_t len = static_cast<int32_t>(s.GetSize());
		int32_t start = SkipLeadingWhitespace(src, len);
		return StringVector::AddString(result, reinterpret_cast<const char *>(src + start),
		                               static_cast<idx_t>(len - start));
	});
}

void TrinoRtrimFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &input = args.data[0];
	UnaryExecutor::Execute<string_t, string_t>(input, result, args.size(), [&](string_t s) {
		const auto *src = reinterpret_cast<const uint8_t *>(s.GetData());
		int32_t len = static_cast<int32_t>(s.GetSize());
		int32_t end = SkipTrailingWhitespace(src, 0, len);
		return StringVector::AddString(result, reinterpret_cast<const char *>(src), static_cast<idx_t>(end));
	});
}

// Trino's normalize(string[, form]) where form ∈ {NFC, NFD, NFKC, NFKD}.
// The vendored canonical normalization data serves both NFC and NFD, but our
// public API deliberately exposes only the 1-arg NFC form. NFKC/NFKD require
// compatibility data that is not bundled; no form-selector overload is exposed.

inline std::string NormalizeWith(const icu::Normalizer2 &norm, const char *data, idx_t size) {
	icu::UnicodeString in = icu::UnicodeString::fromUTF8(icu::StringPiece(data, size));
	UErrorCode err = U_ZERO_ERROR;
	icu::UnicodeString out_us = norm.normalize(in, err);
	if (U_FAILURE(err)) {
		throw InvalidInputException(StringUtil::Format("trino_normalize: ICU normalize failed (%s)", u_errorName(err)));
	}
	std::string out;
	out_us.toUTF8String(out);
	return out;
}

// 1-arg form: defaults to NFC, matching Trino's documented default.
void TrinoNormalizeFun(DataChunk &args, ExpressionState &state, Vector &result) {
	UErrorCode bootstrap_err = U_ZERO_ERROR;
	const icu::Normalizer2 *nfc = icu::Normalizer2::getNFCInstance(bootstrap_err);
	if (U_FAILURE(bootstrap_err) || nfc == nullptr) {
		throw IOException("trino_normalize: ICU NFC instance unavailable");
	}
	auto &input = args.data[0];
	UnaryExecutor::Execute<string_t, string_t>(input, result, args.size(), [&](string_t s) {
		std::string out = NormalizeWith(*nfc, s.GetData(), s.GetSize());
		return StringVector::AddString(result, out);
	});
}

} // namespace

void RegisterStringFunctions(ExtensionLoader &loader) {
	loader.RegisterFunction(ScalarFunction("trino_lower", {LogicalType::VARCHAR}, LogicalType::VARCHAR, TrinoLowerFun));
	loader.RegisterFunction(ScalarFunction("trino_upper", {LogicalType::VARCHAR}, LogicalType::VARCHAR, TrinoUpperFun));
	loader.RegisterFunction(
	    ScalarFunction("trino_reverse", {LogicalType::VARCHAR}, LogicalType::VARCHAR, TrinoReverseFun));
	loader.RegisterFunction(ScalarFunction("trino_trim", {LogicalType::VARCHAR}, LogicalType::VARCHAR, TrinoTrimFun));
	loader.RegisterFunction(ScalarFunction("trino_ltrim", {LogicalType::VARCHAR}, LogicalType::VARCHAR, TrinoLtrimFun));
	loader.RegisterFunction(ScalarFunction("trino_rtrim", {LogicalType::VARCHAR}, LogicalType::VARCHAR, TrinoRtrimFun));
	loader.RegisterFunction(
	    ScalarFunction("trino_normalize", {LogicalType::VARCHAR}, LogicalType::VARCHAR, TrinoNormalizeFun));
}

} // namespace duckdb
