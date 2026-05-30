#include "string_functions.hpp"

#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"

#include <unicode/locid.h>
#include <unicode/normalizer2.h>
#include <unicode/uchar.h>
#include <unicode/unistr.h>
#include <unicode/utf8.h>

#include <algorithm>
#include <cctype>
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
		us.toLower(icu::Locale::getRoot());
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
		us.toUpper(icu::Locale::getRoot());
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
// DuckDB only ships nfc_normalize, so the other three forms need ICU.
// icu::Normalizer2::getNF{C,D,KC,KD}Instance returns process-wide singletons
// — safe and cheap to fetch per row.

inline const icu::Normalizer2 *NormalizerForForm(const std::string &form_upper) {
	UErrorCode err = U_ZERO_ERROR;
	const icu::Normalizer2 *norm;
	if (form_upper == "NFC") {
		norm = icu::Normalizer2::getNFCInstance(err);
	} else if (form_upper == "NFD") {
		norm = icu::Normalizer2::getNFDInstance(err);
	} else if (form_upper == "NFKC") {
		norm = icu::Normalizer2::getNFKCInstance(err);
	} else if (form_upper == "NFKD") {
		norm = icu::Normalizer2::getNFKDInstance(err);
	} else {
		return nullptr;
	}
	if (U_FAILURE(err)) {
		return nullptr;
	}
	return norm;
}

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

// 2-arg form: accepts 'NFC' / 'NFD' / 'NFKC' / 'NFKD' (case-insensitive).
void TrinoNormalizeFormFun(DataChunk &args, ExpressionState &state, Vector &result) {
	BinaryExecutor::Execute<string_t, string_t, string_t>(
	    args.data[0], args.data[1], result, args.size(), [&](string_t s, string_t form) {
		    std::string form_upper = form.GetString();
		    std::transform(form_upper.begin(), form_upper.end(), form_upper.begin(),
		                   [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
		    const icu::Normalizer2 *norm = NormalizerForForm(form_upper);
		    if (norm == nullptr) {
			    throw InvalidInputException(StringUtil::Format(
			        "trino_normalize: unsupported form '%s' (expected NFC, NFD, NFKC, NFKD)", form.GetString()));
		    }
		    std::string out = NormalizeWith(*norm, s.GetData(), s.GetSize());
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
	loader.RegisterFunction(ScalarFunction("trino_normalize", {LogicalType::VARCHAR, LogicalType::VARCHAR},
	                                       LogicalType::VARCHAR, TrinoNormalizeFormFun));
}

} // namespace duckdb
