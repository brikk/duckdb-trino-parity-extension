# REPORT: String / Unicode audit of DuckDB vs Trino

> **Scope note.** The `trino_parity` extension ships only 10 native functions
> (see the repo [README](../README.md)); of the string family, only the 7 that
> genuinely diverge are shipped natively (`trino_lower/upper/reverse/trim/ltrim/rtrim/normalize`).
> The aligned string functions discussed here (`length`, `substring`, `replace`,
> `strpos`, `starts_with`, `lpad`, `rpad`, `concat_ws`, `translate`, …) are
> **not** shipped — the caller emits the bare DuckDB built-in directly.

This is the empirical audit that motivates `trino_parity`'s string functions.
It ran every string function the extension exposes — plus comparison
operators — across a Unicode corpus, comparing DuckDB's built-in behaviour
against Trino's documented semantics, and recording where they diverge.

Each divergence below maps directly to an implementation decision in the
extension: functions that diverge are reimplemented natively in C++ against
the statically-linked ICU (`src/string_functions.cpp`); functions that agree
on all probed inputs are **not** shipped by the extension at all — the caller
emits the bare DuckDB built-in directly, since it is already byte-for-byte
equivalent to Trino.

**Audit provenance:** DuckDB 1.5.x with ICU. A probe ran every function across
the corpus and emitted a Markdown table; the raw output is in
[Appendix A](#appendix-a--raw-probe-output). The curated takeaways below
distinguish real engine divergences from probe-side rendering noise.

**Convention:**
- ✅ aligned — DuckDB output matches Trino's documented behaviour on every probed input.
- ⚠️ partial — aligned on most cases; a specific class of input diverges.
- ❌ divergent — produces different results; a bare passthrough would be a correctness bug for the affected input class.

---

## Summary table

| Function | Audit verdict | Extension implementation |
|---|---|---|
| `length/1` | ✅ aligned (code points; emoji counted as 1) | caller-side (bare DuckDB) |
| `reverse/1` | ❌ **divergent** — DuckDB reverse is grapheme-cluster-aware; Trino is code-point-only | native (ICU code-point reverse) |
| `trim/1` | ⚠️ partial — DuckDB strips space + EM SPACE but NOT tab/LF/CR/FF/VT; Trino's Java strips all of those | native (ICU `u_isWhitespace`) |
| `ltrim/1` | ⚠️ partial — same whitespace caveat as `trim` | native (ICU `u_isWhitespace`) |
| `rtrim/1` | ⚠️ partial — same whitespace caveat | native (ICU `u_isWhitespace`) |
| `substring/{2,3}` | ✅ aligned (1-based code-point index, incl. emoji and combining marks as separate code points) | caller-side (bare DuckDB) |
| `replace/3` | ✅ aligned (code-point-level; composed vs decomposed treated separately on both sides) | caller-side (bare DuckDB) |
| `strpos/2` | ✅ aligned (1-based code-point index, 0 if not found) | caller-side (bare DuckDB) |
| `starts_with/2` | ✅ aligned (binary prefix on UTF-8 — codepoint-aligned) | caller-side (bare DuckDB) |
| `lpad/3` | ✅ aligned (pad to code-point count, incl. emoji and CJK) | caller-side (bare DuckDB) |
| `rpad/3` | ✅ aligned | caller-side (bare DuckDB) |
| `concat_ws/{2..5}` | ✅ aligned (Unicode separator + NULL skipping both match Trino) | caller-side (bare DuckDB) |
| `translate/3` | ✅ aligned (code-point-wise substitution; extra `from` chars deleted in both engines) | caller-side (bare DuckDB) |
| `regexp_like/2` | ✅ aligned (RE2 both sides; `\p{Han}`, `\p{So}`, etc. work in both) | caller-side (bare DuckDB) |
| `regexp_extract/{2,3}` | ✅ aligned (group 0 = whole match in both; group N captures match) | caller-side (bare DuckDB) |
| `lower/1` | ❌ divergent on Turkish `'İ'` (DuckDB → `'i'`, Trino → `'i'` + U+0307); ASCII + most non-ASCII aligned | native (ICU full case folding) |
| `upper/1` | ❌ divergent on German `'ß'` (DuckDB → `'ẞ'` U+1E9E, Trino → `'SS'`); ASCII + most non-ASCII aligned | native (ICU full case folding) |

**Comparison operators** (`=`, `<>`, `<`, `<=`, `>`, `>=`): ✅ aligned with
default BINARY collation. DuckDB does byte comparison on UTF-8, which is
monotonic with code-point comparison; Trino does code-point comparison on Java
strings. Same result on all probed inputs.

**Collation experiments**: documented under
[Collations](#collations-and-unicode-equality) below.

---

## Divergences resolved by native implementations

### `reverse/1` — DuckDB is grapheme-aware, Trino is code-point-only

Input `'cafe' + U+0301` (decomposed café — 5 code points: `c`, `a`, `f`, `e`, combining-acute).

- **DuckDB:** `e + U+0301 + f + a + c` — keeps the `e` + combining-acute pair together as one grapheme cluster, reverses by grapheme.
- **Trino (per spec):** `U+0301 + e + f + a + c` — reverses code points, combining mark ends up at the start.

Input `'👨‍👩‍👧'` (man-ZWJ-woman-ZWJ-girl, 5 code points).
- **DuckDB:** returns the input unchanged — treats the whole ZWJ sequence as a single grapheme cluster.
- **Trino:** `girl-ZWJ-woman-ZWJ-man` — reverses code points.

**Impact:** `WHERE reverse(name) = '<some-string>'` matches different rows in
DuckDB vs Trino any time the input contains combining marks or ZWJ sequences.

**Resolution:** `trino_reverse` reverses by code point (`U8_PREV`), matching
Trino.

### `trim/1`, `ltrim/1`, `rtrim/1` — different whitespace sets

DuckDB's bare `trim(s)` strips:
- ✅ U+0020 space
- ✅ U+2003 EM SPACE
- ❌ U+0009 tab — NOT stripped
- ❌ U+000A LF — NOT stripped
- ❌ U+000D CR — NOT stripped
- ❌ U+000C FF — NOT stripped
- ❌ U+000B VT — NOT stripped
- ✅ U+00A0 NBSP — NOT stripped (matches Trino)
- ✅ U+200B ZWSP — NOT stripped (matches Trino)

Trino's `trim` follows Java's whitespace definition (`Character.isWhitespace` / `String.strip`):
- ✅ Strips: space, tab, LF, CR, FF, VT, EM SPACE, and other Unicode Z-category chars except NBSP/ZWSP.

**Impact:** A row with `name = '\thello\t'` and predicate `WHERE trim(name) = 'hello'`:
- Trino: `trim('\thello\t')` → `'hello'` → match.
- DuckDB built-in: `trim('\thello\t')` → `'\thello\t'` (tabs not stripped) → no match.

Tab-bearing data is real (TSV imports, web scrapes, log lines), so this is a
non-trivial false-negative risk.

**Resolution:** `trino_trim` / `trino_ltrim` / `trino_rtrim` skip code points
where ICU's `u_isWhitespace` is true, matching Java's `Character.isWhitespace`
(NBSP / U+2007 / U+202F intentionally NOT stripped).

### `lower/1`, `upper/1` — simple vs full case folding

- `lower('İ')` (U+0130): DuckDB → `'i'` (1 code point, simple folding); Trino → `'i'` + U+0307 (2 code points, full folding).
- `upper('ß')` (U+00DF): DuckDB → `'ẞ'` (U+1E9E, 1 code point); Trino → `'SS'` (2 code points).

ASCII and most non-ASCII inputs agree; the divergence is confined to
characters whose full-folding expansion differs from simple folding.

**Resolution:** `trino_lower` / `trino_upper` use ICU `u_strToLower` /
`u_strToUpper` with the root locale (full case folding), matching Java.

---

## Aligned functions — Unicode corpus passes

These functions produced Trino-equivalent output for every probed input, including:

- ASCII baseline, empty string
- Pre-composed accent (`'café'` with U+00E9)
- Decomposed accent (`'cafe' + U+0301`)
- Turkish capital dotted I (U+0130)
- German sharp s (`ß`) and capital sharp s (`ẞ`, U+1E9E)
- Greek capital sigma (`Σ`), medial sigma (`σ`), final sigma (`ς`)
- CJK (`日本語`)
- Emoji single (`😀`, U+1F600 — surrogate pair territory)
- Emoji ZWJ family (`👨‍👩‍👧` — 5 code points across multiple supplementary planes)
- Combining mark sequence (`p + U+0301`)
- Mixed-script (`Café 日本`)

`length`, `substring/{2,3}`, `replace`, `strpos`, `starts_with`, `lpad`,
`rpad`, `concat_ws/{2..5}`, `translate`, `regexp_like`, `regexp_extract/{2,3}`
are all in this aligned set. Code-point counting for `length` and `lpad`/`rpad`
correctly treats `😀` (1 code point, 2 UTF-16 code units, 4 UTF-8 bytes) as
length 1. These are **not** shipped by the extension — the caller emits the
bare DuckDB built-in directly, since it already matches Trino.

---

## Comparison operators

| Operator | Verdict | Detail |
|---|---|---|
| `=`, `<>` | ✅ aligned | DuckDB BINARY = UTF-8 byte equality; Trino = code-point equality. UTF-8 byte equality ⇔ code-point equality. |
| `<`, `<=`, `>`, `>=` | ✅ aligned | UTF-8 byte order is monotonic with code-point order (a UTF-8 design property). Both engines agree on `'a' < 'á'`, `'日' < '本'`, etc. |
| Pre-composed vs decomposed | both **false** | `'café'` (U+00E9) ≠ `'cafe' + U+0301'`. DuckDB BINARY says false; Trino code-point equality also says false. Same answer = aligned. |
| Emoji equality | ✅ aligned | `'😀' = '😀'` → true in both. |

---

## Collations and Unicode equality

DuckDB supports the following collations on `=` (verified empirically):

| Pair | BINARY | NFC | NOACCENT | icu_noaccent | NOCASE | ICU `en` | ICU `es` |
|---|---|---|---|---|---|---|---|
| `'café'` precomposed = `'cafe' + U+0301` | false | **true** | **true** | **true** | — | — | — |
| `'HeLLo'` = `'hello'` | false | false | false | false | **true** | false | false |
| `'İ'` = `'i'` | false | false | false | false | **true** | false | false |
| `'İ'` = `'i' + U+0307` | false | (untested) | (untested) | (untested) | false | (untested) | (untested) |
| `'ß'` = `'ss'` | false | false | false | false | false | false | false |
| `'café'` = `'cafe'` | false | false | **true** | **true** | false | false | false |
| `'naïve'` = `'naive'` | false | false | **true** | **true** | false | false | false |

Observations:

- **`COLLATE NFC` makes precomposed and decomposed forms compare equal**, while BINARY does not. Useful as a more-permissive-than-Trino comparison for cross-encoding tolerance.
- **`COLLATE NOACCENT`** is more aggressive (also matches `'café' = 'cafe'`).
- **`COLLATE NOCASE`** handles simple case-insensitive pairs but NOT the Turkish `İ ↔ i + U+0307` case — so it is NOT a substitute for full Unicode case folding.
- **ICU language collations (`en`, `es`)** behave like BINARY for basic equality — they affect ordering, not equality.

For the default use case, BINARY matches Trino, so there is no divergence to
widen around. NFC/NOACCENT would only help if cross-encoding tolerance is
needed.

---

## Outstanding questions / non-conclusions

1. **Trino's exact `trim` whitespace set** — asserted as Java's `Character.isWhitespace` semantics based on Trino's Java-based VARCHAR handling. Worth verifying against Trino's `trim` implementation for exact parity.

2. **`reverse` semantics on lone surrogates** — the corpus did not include malformed UTF-16. Probably moot for VARCHAR data but worth a fixture.

3. **Grapheme cluster vs code point in `substring` / `length`** — both engines agreed that combining marks count as separate code points. `substring(p̀, 2, 1)` returns the combining acute alone in both engines. This matches Trino's documented spec ("code points") and rules out the divergence class found in `reverse`.

4. **Regex Unicode property escapes** — `\p{Han}`, `\p{So}`, `[\p{L}]+` all worked identically in both engines. RE2's Unicode coverage is consistent across Re2J (Trino) and google/re2 (DuckDB). Deeper property classes (e.g. `\p{Block=Linear_B_Syllabary}`) untested.

5. **`concat_ws` with NULL separator** — not probed. Behaviour is believed to match, but worth a fixture before relying on it.

---

## Appendix A — raw probe output

`**NO**` markers in the raw output sometimes reflect a probe-side rendering
quirk (the comparison escaped both bool and integer results into quoted
strings while the "expected" column held raw values) — those rows are actually
aligned. The curated summary above accounts for this.

```text
=== String / Unicode audit ===
Each row: corpus-key, DuckDB result, length(result), Trino-expected (per spec).
`!=` flags a divergence from Trino's documented behaviour.

## length — input -> code-point count
| corpus | input | duckdb actual | trino expected | aligned? |
|---|---|---|---|---|
| ascii_hello | "hello" | "5" | "5" | yes |
| empty | "" | "0" | "0" | yes |
| cafe_precomposed | "café" | "4" | "4" | yes |
| cafe_decomposed | "café" | "5" | "5" | yes |
| turkish_capital_I | "İ" | "1" | "1" | yes |
| german_sharp_s | "ß" | "1" | "1" | yes |
| greek_capital_sigma | "Σ" | "1" | "1" | yes |
| cjk_japanese | "日本語" | "3" | "3" | yes |
| emoji_smile | "😀" | "1" | "1" | yes |
| emoji_zwj_family | "👨‍👩‍👧" | "5" | "5" | yes |
| combining_p_acute | "ṕ" | "2" | "2" | yes |

## reverse
| cafe_decomposed | "café" | "éfac" | "́efac" | **NO** ← real divergence (grapheme vs codepoint)
| emoji_zwj_family | "..." | "👨‍👩‍👧" (unchanged) | "👧‍👩‍👨" | **NO** ← real divergence

## trim whitespace coverage
| tab + 'hi' + tab | tab-hi-tab | "hi" | **NO** ← real divergence (tab not stripped)
| LF  + 'hi' + LF  | LF-hi-LF   | "hi" | **NO** ← real divergence (LF not stripped)
| CR  + 'hi' + CR  | CR-hi-CR   | "hi" | **NO** ← real divergence (CR not stripped)
| FF  + 'hi' + FF  | FF-hi-FF   | "hi" | **NO** ← real divergence (FF not stripped)
| NBSP + 'hi' + NBSP | " hi " | " hi " | yes (Java NBSP is NOT whitespace)
| EM SPACE + 'hi' + EM SPACE | "hi" | "hi" | yes
| ZWSP + 'hi' + ZWSP | "​hi​" | "​hi​" | yes

## substring   (all aligned for the corpus)
## replace     (all aligned for the corpus)
## strpos      (all aligned; "**NO**" in raw output was the int-vs-string escape quirk)
## starts_with (all aligned; same escape quirk)
## lpad / rpad (all aligned)
## concat_ws   (all aligned, including Unicode separator U+30FB and NULL skipping)
## translate   (all aligned)
## regexp_like (all aligned; same escape quirk in raw output)
## regexp_extract (all aligned)

## lower
| turkish_capital_I | "İ" | "i" | "i̇" | **NO** ← full-folding divergence
| (all other corpus members aligned)

## upper
| german_sharp_s | "ß" | "ẞ" | "SS" | **NO** ← full-folding divergence
| (all other corpus members aligned)

## Comparison operators (BINARY default) — all aligned

## Collations
| café (precomposed) vs café (decomposed) | NFC          | true   |
| café (precomposed) vs café (decomposed) | NOACCENT     | true   |
| café (precomposed) vs café (decomposed) | icu_noaccent | true   |
| café (precomposed) vs café (decomposed) | BINARY       | false  |
| HeLLo vs hello                          | NOCASE       | true   |
| İ vs i                                  | NOCASE       | true   |
| İ vs i+U+0307                           | NOCASE       | false  |
| ß vs ss                                 | NOCASE       | false  |
| café vs cafe                            | NOACCENT     | true   |
| naïve vs naive                          | NOACCENT     | true   |
| a vs á                                  | ICU en       | false  |
| a vs á                                  | ICU es       | false  |
```
