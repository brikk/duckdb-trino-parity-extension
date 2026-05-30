#include "trino_alias_sql.hpp"

#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/printer.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

#include <cctype>
#include <string>
#include <vector>

namespace duckdb {

namespace {

// Split a SQL string on TOP-LEVEL semicolons. Respects single-line comments
// (`--` to end-of-line) and single-quoted string literals (with `''` escape).
// Does NOT handle block comments or double-quoted identifiers because the
// vendored trino_function_aliases.sql uses neither.
std::vector<std::string> SplitStatements(const char *sql) {
	std::vector<std::string> out;
	std::string current;
	enum class State { Code, LineComment, StringLit };
	State state = State::Code;

	for (const char *p = sql; *p != '\0'; ++p) {
		const char c = *p;
		switch (state) {
		case State::Code:
			if (c == '-' && p[1] == '-') {
				state = State::LineComment;
				current.push_back(c);
				current.push_back(p[1]);
				++p;
			} else if (c == '\'') {
				state = State::StringLit;
				current.push_back(c);
			} else if (c == ';') {
				StringUtil::Trim(current);
				if (!current.empty()) {
					out.push_back(std::move(current));
				}
				current.clear();
			} else {
				current.push_back(c);
			}
			break;
		case State::LineComment:
			current.push_back(c);
			if (c == '\n') {
				state = State::Code;
			}
			break;
		case State::StringLit:
			current.push_back(c);
			if (c == '\'') {
				// Doubled quote inside a string literal escapes itself.
				if (p[1] == '\'') {
					current.push_back(p[1]);
					++p;
				} else {
					state = State::Code;
				}
			}
			break;
		}
	}
	StringUtil::Trim(current);
	if (!current.empty()) {
		out.push_back(std::move(current));
	}
	return out;
}

// Return the first SQL keyword in stmt (after skipping leading whitespace and
// -- line comments), uppercased. Empty string if the statement is comment-only.
std::string FirstKeyword(const std::string &stmt) {
	size_t i = 0;
	while (i < stmt.size()) {
		const char c = stmt[i];
		if (std::isspace(static_cast<unsigned char>(c))) {
			++i;
		} else if (c == '-' && i + 1 < stmt.size() && stmt[i + 1] == '-') {
			while (i < stmt.size() && stmt[i] != '\n') {
				++i;
			}
		} else {
			break;
		}
	}
	std::string out;
	while (i < stmt.size() && std::isalpha(static_cast<unsigned char>(stmt[i]))) {
		out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(stmt[i]))));
		++i;
	}
	return out;
}

// INSTALL / LOAD <other-extension> are nice-to-have: ICU is wanted so that
// trino_with_timezone's underlying timezone(zone, ts) function resolves, but
// sandboxed test environments may have no network and no on-disk extension
// cache. Log and continue instead of failing the entire extension load.
bool IsBestEffort(const std::string &stmt) {
	const auto kw = FirstKeyword(stmt);
	return kw == "INSTALL" || kw == "LOAD";
}

} // namespace

void RegisterAliasMacros(ExtensionLoader &loader) {
	auto &db = loader.GetDatabaseInstance();
	Connection con(db);

	const auto statements = SplitStatements(kTrinoAliasSql);
	for (const auto &stmt : statements) {
		auto result = con.Query(stmt);
		if (!result->HasError()) {
			continue;
		}
		// Truncate the offending SQL for log-readability; full statement is
		// in the build output if anyone needs to dig deeper.
		const auto snippet = stmt.size() > 200 ? stmt.substr(0, 200) + "..." : stmt;
		if (IsBestEffort(stmt)) {
			Printer::PrintF(
			    "[trino_parity] best-effort statement failed, continuing.\n  SQL: %s\n  Error: %s\n",
			    snippet, result->GetError());
			continue;
		}
		throw IOException("trino_parity: alias-macro registration failed.\n"
		                  "  SQL: %s\n"
		                  "  Error: %s",
		                  snippet, result->GetError());
	}
}

} // namespace duckdb
