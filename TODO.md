# TODO

This extension exposes DuckDB scalar (and eventually table) functions whose
semantics are exact-equivalent to the matching Trino built-ins, so that
predicate pushdown from Trino to DuckDB through the
[ducklake-integrations](https://github.com/brikk/ducklake-integrations)
connector is lossless on non-ASCII / corner-case input where DuckDB's own
built-ins diverge.

Three top-level work items, in rough dependency order.

## 1. Move SQL macro aliases into the C++ extension

Today the Trino connector ships
`jvm/trino-ducklake/src/main/resources/dev/brikk/ducklake/trino/plugin/trino-function-aliases.sql`
which is a long series of `CREATE OR REPLACE MACRO trino_<name>(...) AS ...;`
statements re-executed on every attach by `TrinoFunctionAliases.java`, plus a
`trino_meta()` SQL table macro that's the source-of-truth for the Java-side
`DuckDbExpressionTranslator.PUSHABLE_FUNCTIONS` set (parity enforced by
`TestTrinoFunctionAliases#testJavaPushableSetMatchesDuckDbMeta`).

Rewrite as native registrations here:

- Each `trino_<name>` macro → a `ScalarFunction` registered in `LoadInternal`.
  Most are one-line bodies that wrap an existing DuckDB function — those can
  stay as macro-ish wrappers via `MacroFunction` registration, or be inlined
  as a direct `ScalarFunction` calling into the existing DuckDB scalar.
- `trino_meta()` → a `TableFunction` returning the same `(name, arity, category)`
  shape. Consumed by Java parity tests.
- The connector's attach path changes from "run alias SQL" to
  `INSTALL trino_parity; LOAD trino_parity;` (best-effort, mirroring the
  current ICU pattern in `TrinoFunctionAliases.applyIcuLoad`).

Wins: no SQL parse cost on every attach, one shipped artifact instead of
in-tree SQL + classpath resource, easier to add functions that need C++
(item 2 below).

Parity contract: do not break
`TestTrinoFunctionAliases#testJavaPushableSetMatchesDuckDbMeta`. The Java
side reads `trino_meta()`; this extension's table function must produce
identical rows for the migrated set during the transition. Migrate in
batches by category (string, numeric, regex, encoding, distance, hash,
date, conditional) so the parity test stays green per-batch.

Open question: do we keep `trino-function-aliases.sql` as a fallback for the
case where the extension fails to load, or hard-require the extension?
Recommend hard-require once the extension is published — the SQL fallback
adds two code paths that drift.

## 2. Add the missing string functions (placeholder elimination)

The Trino-side translator currently ships three macros as `-- @placeholder`
because DuckDB's built-ins diverge from Trino on non-ASCII input:

| Function | Divergence | ICU call |
|---|---|---|
| `trino_lower(s)` | DuckDB simple case folding. `lower('İ')` → `'i'` (DuckDB) vs `'i' + U+0307'` (Trino). | `u_strToLower` with root locale. |
| `trino_upper(s)` | Same. `upper('ß')` → `'ẞ'` (U+1E9E, DuckDB) vs `'SS'` (Trino, full case folding expands single → two code points). | `u_strToUpper` with root locale. |
| `trino_reverse(s)` | DuckDB is grapheme-cluster-aware; Trino is code-point-only. Diverges on combining marks (`'cafe' + U+0301'`) and ZWJ emoji sequences. | Iterate UTF-8 via `U8_NEXT`, push spans onto a buffer in reverse, re-encode with `U8_APPEND`. **Do not** use `u_strReverse` — it operates on UTF-16 code units, wrong granularity. |

Full divergence catalog (verified empirically on Unicode corpus including
Turkish İ, German ß, Greek sigma, CJK, single emoji, ZWJ family, combining
marks, mixed-script):
`ducklake-integrations/jvm/trino-ducklake/dev-docs/REPORT-string-unicode-audit.md`.

Likely additions in the same round:

- `trino_normalize(s, form)` — Trino supports NFC/NFD/NFKC/NFKD; DuckDB
  only has `nfc_normalize`. ICU `unorm2_normalize` with the right
  `UNormalization2` instance per form. Currently not in the pushable
  set because of this gap.
- `trino_trim(s)` / `trino_ltrim(s)` / `trino_rtrim(s)` — aligned today via
  a macro that passes the full Java `Character.isWhitespace` set explicitly
  to DuckDB's `trim(s, chars)`. Could move to a single C++ function that
  matches Java's whitespace set directly. Not blocking; the macro version is
  correct.

ICU is already linked by DuckDB's bundled `icu` extension; reuse those
symbols rather than vendoring ICU into vcpkg. The C++ side should
`#include "unicode/unistr.h"` (or the C `u*` headers) and let CMake find
ICU via DuckDB's existing build. If that turns out not to be exposed,
fall back to adding `icu` to `vcpkg.json`.

Acceptance: drop the `-- @placeholder` tags in
`trino-function-aliases.sql`, change the macro bodies to call the extension
functions, add Trino-aligned semantic fixtures to
`TestTrinoFunctionAliases#semanticCases()` covering the documented
divergence corpus. After this round there are no more warn-on-emit
placeholders.

## 3. Test all this works

Test surface:

- Per-function SQL tests under `test/sql/` for the native ICU-backed
  functions, covering the Unicode corpus from REPORT-string-unicode-audit.md
  (Turkish İ, German ß, Greek capital/medial/final sigma, decomposed café,
  ZWJ family emoji, combining-mark sequences, CJK).
- A `trino_meta()` round-trip test: registered function set matches what
  the table function reports.
- A cross-engine test in the parent repo
  (`TestTrinoFunctionAliases#semanticCases()`) that runs each function
  through DuckDB and asserts against the Trino-spec expected value. This
  already exists for the macro-based aliases; extend it for the new
  native functions, then delete macro fixtures as functions migrate to
  native registration.
- The existing parity test
  (`TestTrinoFunctionAliases#testJavaPushableSetMatchesDuckDbMeta`) keeps
  the Java `PUSHABLE_FUNCTIONS` set in lockstep with this extension's
  `trino_meta()` — drift is a build break.

CI runs `make test` (DuckDB sqllogictests) via
`.github/workflows/MainDistributionPipeline.yml`. Cross-engine tests live
in the parent repo and run via Gradle.
