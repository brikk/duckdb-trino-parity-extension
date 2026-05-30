-- Trino -> DuckDB function aliases.
--
-- Sourced from the trino-ducklake connector's
-- resources/dev/brikk/ducklake/trino/plugin/trino-function-aliases.sql.
-- This file is the source of truth for the macros; the connector consumes
-- them by loading the trino_parity extension at attach time
-- (INSTALL trino_parity; LOAD trino_parity;).
--
-- The translator in the connector emits trino_<name>(...) calls; each macro
-- below maps one Trino function (by name + arity) to the DuckDB construct
-- that matches Trino's semantics. Semantic fixes (NULL handling, collation,
-- edge cases) go here so they can be corrected without rebuilding the Trino
-- plugin.
--
-- The full-case-folding functions trino_lower / trino_upper and the
-- code-point reverse trino_reverse are NOT macros — they are native C++
-- scalar functions registered in src/string_functions.cpp via vcpkg's
-- statically-linked ICU. They intentionally do not appear below;
-- trino_meta() does still list them because they remain pushable.
--
-- Two ICU-related concerns coexist here:
--   (a) ICU case folding / code-point reverse — handled by the statically
--       linked ICU inside this extension. No INSTALL/LOAD needed.
--   (b) DuckDB's `timezone(zone, ts)` function used by trino_with_timezone
--       and its timezone-name table — lives in DuckDB's bundled `icu`
--       extension, separate from our static link. We INSTALL/LOAD it
--       best-effort below; if the install fails (sandboxed env, no
--       network, no on-disk cache) the loader logs and continues, and
--       trino_with_timezone fails only if actually called.
INSTALL icu;
LOAD icu;

-- ---- String functions ----

CREATE OR REPLACE MACRO trino_length(s) AS length(s);

-- Whitespace set used by Trino's trim — matches Java's Character.isWhitespace
-- (the semantics behind String.strip()). DuckDB's bare trim() strips only ASCII
-- space + EM SPACE by default, so we pass the explicit character set as the
-- second argument. Excludes U+00A0 NBSP, U+2007 FIGURE SPACE, U+202F NARROW
-- NBSP per the Java spec — these are intentionally NOT whitespace.
CREATE OR REPLACE MACRO trino__java_whitespace_chars() AS
    chr(9) || chr(10) || chr(11) || chr(12) || chr(13)         -- HT LF VT FF CR
    || chr(28) || chr(29) || chr(30) || chr(31) || chr(32)     -- FS GS RS US SPACE
    || chr(5760)                                                -- U+1680 OGHAM SPACE MARK
    || chr(8192) || chr(8193) || chr(8194) || chr(8195)        -- U+2000-3 quads/spaces
    || chr(8196) || chr(8197) || chr(8198)                     -- U+2004-6 m-spaces
    -- U+2007 FIGURE SPACE intentionally excluded
    || chr(8200) || chr(8201) || chr(8202)                     -- U+2008-A puncts/thin/hair
    || chr(8232) || chr(8233)                                  -- U+2028-9 line/para sep
    -- U+202F NARROW NBSP intentionally excluded
    || chr(8287)                                                -- U+205F MEDIUM MATH SPACE
    || chr(12288);                                              -- U+3000 IDEOGRAPHIC SPACE

CREATE OR REPLACE MACRO trino_trim(s)  AS trim(s,  trino__java_whitespace_chars());

CREATE OR REPLACE MACRO trino_ltrim(s) AS ltrim(s, trino__java_whitespace_chars());

CREATE OR REPLACE MACRO trino_rtrim(s) AS rtrim(s, trino__java_whitespace_chars());

CREATE OR REPLACE MACRO trino_substring
    (s, start) AS substring(s, start),
    (s, start, length) AS substring(s, start, length);

CREATE OR REPLACE MACRO trino_replace(s, search, replacement) AS replace(s, search, replacement);

CREATE OR REPLACE MACRO trino_strpos(s, sub) AS strpos(s, sub);

CREATE OR REPLACE MACRO trino_starts_with(s, prefix) AS starts_with(s, prefix);

CREATE OR REPLACE MACRO trino_lpad(s, size, padstring) AS lpad(s, size, padstring);

CREATE OR REPLACE MACRO trino_rpad(s, size, padstring) AS rpad(s, size, padstring);

-- DuckDB macros are fixed-arity; concat_ws is variadic in both Trino and DuckDB.
-- 2..5 arg overloads cover common pushdown shapes; extend the list (and
-- trino_meta below) if real workloads call for more.
CREATE OR REPLACE MACRO trino_concat_ws
    (sep, s1)                 AS concat_ws(sep, s1),
    (sep, s1, s2)             AS concat_ws(sep, s1, s2),
    (sep, s1, s2, s3)         AS concat_ws(sep, s1, s2, s3),
    (sep, s1, s2, s3, s4)     AS concat_ws(sep, s1, s2, s3, s4);

-- ---- Numeric functions ----

CREATE OR REPLACE MACRO trino_abs(x) AS abs(x);

CREATE OR REPLACE MACRO trino_ceil(x) AS ceil(x);

CREATE OR REPLACE MACRO trino_floor(x) AS floor(x);

-- Integer mod is semantically aligned (truncated division, sign follows
-- dividend). Float mod diverges (Trino IEEE-remainder, DuckDB fmod) and must be
-- gated at the translator before pushdown — the macro itself is not type-aware.
CREATE OR REPLACE MACRO trino_mod(n, m) AS mod(n, m);

CREATE OR REPLACE MACRO trino_power(x, y) AS power(x, y);

CREATE OR REPLACE MACRO trino_sqrt(x) AS sqrt(x);

CREATE OR REPLACE MACRO trino_exp(x) AS exp(x);

CREATE OR REPLACE MACRO trino_ln(x) AS ln(x);

CREATE OR REPLACE MACRO trino_log2(x) AS log2(x);

CREATE OR REPLACE MACRO trino_log10(x) AS log10(x);

-- ---- More string functions ----

CREATE OR REPLACE MACRO trino_translate(source, src_chars, dest_chars) AS translate(source, src_chars, dest_chars);

-- ---- Regex (RE2 on both sides) ----
-- trino_regexp_like is the canonical demonstration of the interpretation layer:
-- Trino calls regexp_like(s, p), the translator pushes trino_regexp_like(...),
-- and the macro routes to DuckDB's regexp_matches(...). Renaming is invisible to
-- the plugin code.
CREATE OR REPLACE MACRO trino_regexp_like(s, pattern) AS regexp_matches(s, pattern);

-- `group` is a DuckDB reserved word; use `group_index` for the macro parameter.
CREATE OR REPLACE MACRO trino_regexp_extract
    (s, pattern)              AS regexp_extract(s, pattern),
    (s, pattern, group_index) AS regexp_extract(s, pattern, group_index);

-- ---- Round 4: encoding / distance / character-from-code ----

-- Char-from-codepoint. Aligned for valid code points; behavior outside the
-- Unicode range diverges but Trino's signature is `chr(bigint)` matching.
CREATE OR REPLACE MACRO trino_chr(n) AS chr(n);

-- URL percent-encoding (RFC 3986). Both engines aligned.
CREATE OR REPLACE MACRO trino_url_encode(s) AS url_encode(s);

CREATE OR REPLACE MACRO trino_url_decode(s) AS url_decode(s);

-- Hex / base64 encode + decode. Trino returns VARBINARY for to_hex / to_base64
-- and VARCHAR for from_hex / from_base64; DuckDB's hex() returns VARCHAR and
-- unhex() returns BLOB. Type alignment relies on the connector's BLOB↔VARBINARY
-- mapping; output bytes/chars are identical to Trino's.
CREATE OR REPLACE MACRO trino_to_hex(b) AS hex(b);

CREATE OR REPLACE MACRO trino_from_hex(s) AS unhex(s);

CREATE OR REPLACE MACRO trino_to_base64(b) AS to_base64(b);

CREATE OR REPLACE MACRO trino_from_base64(s) AS from_base64(s);

-- Distance metrics. DuckDB names differ (no _distance suffix); macro renames.
-- Levenshtein: number of single-char edits. Hamming: positional differences
-- (requires equal-length strings — both engines raise on mismatch).
CREATE OR REPLACE MACRO trino_levenshtein_distance(s1, s2) AS levenshtein(s1, s2);

CREATE OR REPLACE MACRO trino_hamming_distance(s1, s2) AS hamming(s1, s2);

-- ---- Round 5: trig + math ----
-- All double -> double; both engines use IEEE 754 standard math, output is
-- bit-exact aligned for finite inputs. NaN / ±Inf behaviour matches.

CREATE OR REPLACE MACRO trino_sin(x)  AS sin(x);
CREATE OR REPLACE MACRO trino_cos(x)  AS cos(x);
CREATE OR REPLACE MACRO trino_tan(x)  AS tan(x);
CREATE OR REPLACE MACRO trino_asin(x) AS asin(x);
CREATE OR REPLACE MACRO trino_acos(x) AS acos(x);
CREATE OR REPLACE MACRO trino_atan(x) AS atan(x);

CREATE OR REPLACE MACRO trino_atan2(y, x) AS atan2(y, x);

CREATE OR REPLACE MACRO trino_sinh(x) AS sinh(x);
CREATE OR REPLACE MACRO trino_cosh(x) AS cosh(x);
CREATE OR REPLACE MACRO trino_tanh(x) AS tanh(x);

CREATE OR REPLACE MACRO trino_degrees(x) AS degrees(x);
CREATE OR REPLACE MACRO trino_radians(x) AS radians(x);

CREATE OR REPLACE MACRO trino_cbrt(x) AS cbrt(x);

-- Trino name is `truncate`; DuckDB name is `trunc`. Macro rename, same semantics
-- (truncate toward zero, integer result for double input — both return DOUBLE).
CREATE OR REPLACE MACRO trino_truncate(x) AS trunc(x);

-- ---- Round 6b-core: cryptographic hashes (md5 / sha1 / sha256) ----
-- DuckDB core md5/sha1/sha256 return hex VARCHAR; Trino returns VARBINARY.
-- Wrap with unhex(...) to produce a BLOB matching Trino's wire shape.
-- NULL handling verified aligned (md5(NULL) → NULL; md5('a' || NULL || 'c') → NULL).

CREATE OR REPLACE MACRO trino_md5(b) AS unhex(md5(b));

CREATE OR REPLACE MACRO trino_sha1(b) AS unhex(sha1(b));

CREATE OR REPLACE MACRO trino_sha256(b) AS unhex(sha256(b));

-- ---- Round 6a — Core DuckDB easy wins (no extension) ----

-- sign: returns -1 / 0 / +1.
CREATE OR REPLACE MACRO trino_sign(x) AS sign(x);

-- bit_length: number of bits in the string (8 * octet length for UTF-8).
CREATE OR REPLACE MACRO trino_bit_length(s) AS bit_length(s);

-- pi: 0-arg constant.
CREATE OR REPLACE MACRO trino_pi() AS pi();

-- bitwise_xor → DuckDB's scalar xor(x, y). Aligned for integer inputs.
CREATE OR REPLACE MACRO trino_bitwise_xor(x, y) AS xor(x, y);

-- regexp_replace: Trino replaces ALL matches by default; DuckDB replaces only the
-- FIRST match unless the 'g' flag is set. Pass 'g' to align with Trino.
-- Trino's 2-arg form (no replacement) deletes matches; we pass '' as replacement.
CREATE OR REPLACE MACRO trino_regexp_replace
    (s, pattern)              AS regexp_replace(s, pattern, '', 'g'),
    (s, pattern, replacement) AS regexp_replace(s, pattern, replacement, 'g');

-- ---- Round 6g — Bitwise function-form to operator-form ----
-- Trino exposes bitwise ops as function calls; DuckDB exposes them as operators.
-- Macros bridge the shape. Trino-aligned for typical positive-integer use.
-- ⚠️ bitwise_right_shift on negative integers: signed/unsigned semantics CAN
-- differ between engines. Verify before relying on negative inputs.
CREATE OR REPLACE MACRO trino_bitwise_and(x, y) AS x & y;
CREATE OR REPLACE MACRO trino_bitwise_or(x, y) AS x | y;
CREATE OR REPLACE MACRO trino_bitwise_not(x) AS ~x;
CREATE OR REPLACE MACRO trino_bitwise_left_shift(v, s) AS v << s;
CREATE OR REPLACE MACRO trino_bitwise_right_shift(v, s) AS v >> s;

-- ---- Round 6g — Date convenience ----
CREATE OR REPLACE MACRO trino_year(x) AS year(x);
CREATE OR REPLACE MACRO trino_month(x) AS month(x);
CREATE OR REPLACE MACRO trino_day(x) AS day(x);
CREATE OR REPLACE MACRO trino_quarter(x) AS quarter(x);

-- ---- Round 6i — Conditional `if` + date arithmetic ----

-- if(cond, then[, else]). For Trino's 2-arg form (returns NULL when false) we
-- wrap with an explicit NULL else.
CREATE OR REPLACE MACRO trino_if
    (cond, t)    AS if(cond, t, NULL),
    (cond, t, f) AS if(cond, t, f);

-- date_trunc(unit, x). ⚠️ Return-type caveat: DuckDB always returns TIMESTAMP,
-- even on DATE input. Trino preserves the input type (DATE → DATE for unit ≥ day).
CREATE OR REPLACE MACRO trino_date_trunc(unit, x) AS date_trunc(unit, x);

-- date_diff(unit, t1, t2). Both engines return integer count of unit boundaries
-- crossed, not whole units elapsed.
CREATE OR REPLACE MACRO trino_date_diff(unit, t1, t2) AS date_diff(unit, t1, t2);

-- ---- Step 4 chunk 1 — Tier A (DATE-only) + Tier B (DATE or TIMESTAMP no-TZ) ----

-- day_of_week: Trino is 1=Mon, 7=Sun (ISO). DuckDB's bare dayofweek() is
-- 0=Sun, 6=Sat — the wrong mapping. isodow() gives the ISO 1..7 numbering.
CREATE OR REPLACE MACRO trino_day_of_week(d) AS isodow(d);

-- day_of_year: aligned, 1..366.
CREATE OR REPLACE MACRO trino_day_of_year(d) AS dayofyear(d);

-- last_day_of_month: returns DATE. DuckDB calls this `last_day`.
CREATE OR REPLACE MACRO trino_last_day_of_month(d) AS last_day(d);

-- week / week_of_year: DuckDB's bare `week(d)` is already ISO-aligned.
CREATE OR REPLACE MACRO trino_week(d) AS week(d);
CREATE OR REPLACE MACRO trino_week_of_year(d) AS week(d);

-- year_of_week / yow: ISO-week-numbering year. DuckDB has no bare `isoyear`
-- function; reach it via extract('isoyear' FROM d). Cast to BIGINT to match Trino.
CREATE OR REPLACE MACRO trino_year_of_week(d) AS extract('isoyear' FROM d)::BIGINT;
CREATE OR REPLACE MACRO trino_yow(d)          AS extract('isoyear' FROM d)::BIGINT;

-- hour / minute / second: read directly from the wall clock.
CREATE OR REPLACE MACRO trino_hour(t) AS hour(t);
CREATE OR REPLACE MACRO trino_minute(t) AS minute(t);
CREATE OR REPLACE MACRO trino_second(t) AS second(t);

-- millisecond: Trino returns the millis-OF-SECOND (0..999), NOT total millis
-- since epoch. Cast to BIGINT to match Trino's return type.
CREATE OR REPLACE MACRO trino_millisecond(t) AS extract('millisecond' FROM t)::BIGINT;

-- to_unixtime: seconds since 1970-01-01 UTC as DOUBLE.
CREATE OR REPLACE MACRO trino_to_unixtime(t) AS epoch(t)::DOUBLE;

-- ---- Step 4 chunk 4 — Tier C extras ----

-- from_unixtime(double) → TIMESTAMP(3) WITH TIME ZONE in Trino. DuckDB's
-- to_timestamp(numeric) returns TIMESTAMPTZ. Same absolute instant; rendering
-- depends on session zone.
CREATE OR REPLACE MACRO trino_from_unixtime(d) AS to_timestamp(d);

-- with_timezone(timestamp(p), varchar) → timestamp(p) with time zone in Trino.
-- DuckDB's timezone(zone, ts) returns TIMESTAMPTZ. Note the ARG-ORDER FLIP:
-- Trino is (timestamp, zone); DuckDB is (zone, timestamp).
CREATE OR REPLACE MACRO trino_with_timezone(t, zone) AS timezone(zone, t);

-- ---- Catalog of aliased functions ----
--
-- One row per (trino_name, arg_count) the translator may push down. The
-- translator reads this once per session and treats it as the authoritative
-- pushable set: if a (name, arity) is not here, do not push, even if a
-- trino_<name> macro happens to exist.
--
-- lower/upper/reverse stay in this list — they ARE pushable; they are just
-- implemented as native C++ functions in this extension rather than as macros.
CREATE OR REPLACE MACRO trino_meta() AS TABLE
SELECT * FROM (
    VALUES
        -- string
        ('lower',        1, 'string'),    -- native (ICU full case folding, root locale)
        ('upper',        1, 'string'),    -- native (ICU full case folding, root locale)
        ('length',       1, 'string'),
        ('reverse',      1, 'string'),    -- native (code-point reverse via U8_PREV)
        ('trim',         1, 'string'),
        ('ltrim',        1, 'string'),
        ('rtrim',        1, 'string'),
        ('substring',    2, 'string'),
        ('substring',    3, 'string'),
        ('replace',      3, 'string'),
        ('strpos',       2, 'string'),
        ('starts_with',  2, 'string'),
        ('lpad',         3, 'string'),
        ('rpad',         3, 'string'),
        ('concat_ws',    2, 'string'),
        ('concat_ws',    3, 'string'),
        ('concat_ws',    4, 'string'),
        ('concat_ws',    5, 'string'),
        -- numeric
        ('abs',          1, 'numeric'),
        ('ceil',         1, 'numeric'),
        ('floor',        1, 'numeric'),
        ('mod',          2, 'numeric'),
        ('power',        2, 'numeric'),
        ('sqrt',         1, 'numeric'),
        ('exp',          1, 'numeric'),
        ('ln',           1, 'numeric'),
        ('log2',         1, 'numeric'),
        ('log10',        1, 'numeric'),
        ('sin',          1, 'numeric'),
        ('cos',          1, 'numeric'),
        ('tan',          1, 'numeric'),
        ('asin',         1, 'numeric'),
        ('acos',         1, 'numeric'),
        ('atan',         1, 'numeric'),
        ('atan2',        2, 'numeric'),
        ('sinh',         1, 'numeric'),
        ('cosh',         1, 'numeric'),
        ('tanh',         1, 'numeric'),
        ('degrees',      1, 'numeric'),
        ('radians',      1, 'numeric'),
        ('cbrt',         1, 'numeric'),
        ('truncate',     1, 'numeric'),
        ('sign',         1, 'numeric'),
        ('pi',           0, 'numeric'),
        ('bitwise_xor',  2, 'numeric'),
        ('bitwise_and',         2, 'numeric'),
        ('bitwise_or',          2, 'numeric'),
        ('bitwise_not',         1, 'numeric'),
        ('bitwise_left_shift',  2, 'numeric'),
        ('bitwise_right_shift', 2, 'numeric'),
        -- string (extra)
        ('translate',    3, 'string'),
        ('chr',          1, 'string'),
        ('bit_length',   1, 'string'),
        -- regex
        ('regexp_like',  2, 'regex'),
        ('regexp_extract', 2, 'regex'),
        ('regexp_extract', 3, 'regex'),
        ('regexp_replace', 2, 'regex'),
        ('regexp_replace', 3, 'regex'),
        -- encoding
        ('url_encode',   1, 'encoding'),
        ('url_decode',   1, 'encoding'),
        ('to_hex',       1, 'encoding'),
        ('from_hex',     1, 'encoding'),
        ('to_base64',    1, 'encoding'),
        ('from_base64',  1, 'encoding'),
        -- distance
        ('levenshtein_distance', 2, 'distance'),
        ('hamming_distance',     2, 'distance'),
        -- hash
        ('md5',          1, 'hash'),
        ('sha1',         1, 'hash'),
        ('sha256',       1, 'hash'),
        -- date
        ('year',         1, 'date'),
        ('month',        1, 'date'),
        ('day',          1, 'date'),
        ('quarter',      1, 'date'),
        ('date_trunc',   2, 'date'),
        ('date_diff',    3, 'date'),
        ('day_of_week',       1, 'date'),
        ('day_of_year',       1, 'date'),
        ('last_day_of_month', 1, 'date'),
        ('week',              1, 'date'),
        ('week_of_year',      1, 'date'),
        ('year_of_week',      1, 'date'),
        ('yow',               1, 'date'),
        ('hour',              1, 'date'),
        ('minute',            1, 'date'),
        ('second',            1, 'date'),
        ('millisecond',       1, 'date'),
        ('to_unixtime',       1, 'date'),
        ('from_unixtime',     1, 'date'),
        ('with_timezone',     2, 'date'),
        -- conditional
        ('if',           2, 'conditional'),
        ('if',           3, 'conditional')
) AS t(trino_name, arg_count, category);
