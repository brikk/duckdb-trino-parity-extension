# REPORT: DuckDB date/time + TZ handling vs Trino

> **Scope note.** The `trino_parity` extension ships only 10 native functions
> (see the repo [README](../README.md)) — **none of them are date/time
> functions**. Every `year` / `month` / `day` / `date_trunc` / `date_diff` /
> `day_of_week` / `week` / `year_of_week` / `hour` / `to_unixtime` /
> `from_unixtime` / `with_timezone` operation discussed here is emitted by the
> caller directly against DuckDB (bare built-in, or a documented one-line
> rewrite such as `isodow(…)` for `day_of_week` or `extract('isoyear' …)` for
> `year_of_week`). This report remains the reference for **how** the caller
> should emit each one and for the session-`TimeZone` obligation that governs
> `TIMESTAMP WITH TIME ZONE` correctness.

This is the empirical audit behind the date/time rewrites a caller emits when
pushing Trino predicates to DuckDB (`year`, `month`, `day`, `date_trunc`,
`date_diff`, `day_of_week`, `week`, `year_of_week`, `hour`, `to_unixtime`,
`from_unixtime`, `with_timezone`, and the rest of the `date` category). It
records where DuckDB and Trino agree on calendar/wall-clock arithmetic and —
crucially — where any function that touches `TIMESTAMP WITH TIME ZONE` depends
on the DuckDB **session `TimeZone`**. That session-zone dependence is an
alignment obligation neither the extension nor a bare rewrite can enforce on
its own: a caller that pushes Trino predicates to DuckDB must set DuckDB's
`TimeZone` to Trino's session zone before any predicate evaluates. This report
pins that requirement empirically and documents the exact zone-string
normalisation that makes it work.

**Convention:**
- ✅ aligned — DuckDB output matches Trino's documented behaviour on every probed input, independent of session zone.
- ⚠️ session-sensitive — aligned only when the DuckDB session `TimeZone` matches Trino's session zone.
- ❌ not pushable — semantics cannot be reconciled by a rename/rewrite; the caller must evaluate the function above the scan rather than push it.

---

## The core hazard

Date/time alignment between Trino and DuckDB is **not** uniformly
TZ-sensitive. The hazard concentrates in a narrow band; most of the surface is
safe. Get the boundary right and most of the catalog is a cheap rename. Get it
wrong once and a year-boundary timestamp returns the wrong year.

Three storage shapes matter:

| Type | Trino interpretation | DuckDB interpretation | TZ-sensitive ops |
|---|---|---|---|
| `DATE` | Calendar day, no time | Calendar day, no time | **None.** Always safe. |
| `TIMESTAMP(p)` (no TZ) | Wall clock — components stored directly | Wall clock — components stored directly | **None.** `year()` etc. read off the wall clock without consulting any zone. Safe in both engines regardless of session TZ. |
| `TIMESTAMP(p) WITH TIME ZONE` | Instant + zone (zone travels with the value) | `TIMESTAMPTZ` — instant only; interpretation uses the **session** `TimeZone` | **All extract operations diverge** unless session zones agree. |

The third row is the entire problem. The first two rows are the entire
opportunity: `DATE` and `TIMESTAMP`-without-zone functions are emitted by the
caller as bare DuckDB built-ins with no session-configuration caveat. Only
functions that can receive a `TIMESTAMP WITH TIME ZONE` argument (and
`from_unixtime`, which *produces* one) carry the session-`TimeZone` dependency.

---

## Empirical findings

Probed against an in-process DuckDB (1.5.x-era) with ICU available; behaviour
is identical regardless of how the DuckDB session is hosted (embedded in-process
or reached over an RPC boundary — the same DuckDB engine sits underneath either
way).

### Finding 1 — `TIMESTAMP WITH TIME ZONE` stores a pure instant

**Question.** When a `TIMESTAMPTZ` value is written into a DuckDB database file,
does storage preserve the original zone, or does it become a pure instant that
the reading session reinterprets through its own `TimeZone` setting?

**Answer — instant only; the writer's session zone is discarded.**

The same instant (`2024-06-15 12:00:00 UTC`) was written from three sessions
with writer `TimeZone` = UTC / America/Los_Angeles / Asia/Singapore. All three
rows were read from a single reader session while varying the reader's
`TimeZone`:

| Reader session `TimeZone` | All three rows render as | `year(ts)` | `hour(ts)` |
|---|---|---|---|
| `UTC` | `2024-06-15 12:00:00+00` | 2024 | 12 |
| `America/Los_Angeles` | `2024-06-15 05:00:00-07` | 2024 | 5 |
| `Asia/Singapore` | `2024-06-15 20:00:00+08` | 2024 | 20 |

All three writer-zone rows produce **identical** rendering and extraction in
any given reader session. The writer's `TimeZone` does not travel with the
value.

**Companion data point.** `TIMESTAMP` (no TZ) is wall-clock: the same column
reads `2024-06-15 12:00:00` regardless of reader `TimeZone`, and
`year/hour/day` always return `2024/12/15`. `DATE` is likewise TZ-invariant.

**Implication.** Correctness for `TIMESTAMP WITH TIME ZONE` predicates lives
*entirely* on the DuckDB session `TimeZone` knob — there is no per-column zone
metadata to interrogate. A caller pushing `year(timestamptz_col) = 2024`
to DuckDB without first aligning the session zone could silently return rows
from `2025` (when the reader session is east of UTC and the boundary crosses).
DuckDB's `year` etc. are correct passthroughs; it is the caller's
responsibility to align the session zone before evaluating them against
`TIMESTAMPTZ` inputs.

### Finding 2 — `SET TimeZone` accepted zone shapes + the `Etc/GMT±N` table

**Question.** Which zone shapes does `SET TimeZone = '...'` accept? Can fixed
offsets be translated?

**Answer — named IANA zones work; bare fixed-offset shapes do NOT; `Etc/GMT±N`
works with POSIX sign inversion.**

| Zone literal | `SET TimeZone` result |
|---|---|
| `UTC` | OK |
| `GMT` | OK |
| `EST` | OK |
| `America/Los_Angeles` | OK |
| `Europe/Berlin` | OK |
| `Asia/Singapore` | OK |
| `Pacific/Chatham` | OK |
| `+00:00` | FAIL — `Unknown TimeZone '+00:00'!` |
| `+05:00` | FAIL |
| `-08:00` | FAIL |

DuckDB recognises a small POSIX-style zone set that does **not** include the
bare `±HH:MM` shape a Trino session zone can sometimes take; the error for
`+00:00` even suggests `GMT0`.

#### `Etc/GMT±N` translation table (probed empirically)

POSIX-style `Etc/GMT±N` zones translate fixed integer-hour offsets — with the
documented **sign inversion** (a positive UTC offset gets a *negative*
`Etc/GMT-N`). The reference instant `2024-06-15 12:00:00+00` was rendered under
each:

| Set `TimeZone` value | Rendered offset (= actual UTC offset) | Verdict |
|---|---|---|
| `Etc/GMT` | `+00:00` | UTC alias — OK |
| `Etc/GMT0` | `+00:00` | UTC alias — OK |
| `Etc/UTC` | `+00:00` | UTC alias — OK |
| `Etc/GMT-5` | `+05:00` | UTC+5 (POSIX inversion) — OK |
| `Etc/GMT+5` | `-05:00` | UTC-5 — OK |
| `Etc/GMT-8` | `+08:00` | UTC+8 (Singapore-like) — OK |
| `Etc/GMT+8` | `-08:00` | UTC-8 (LA-non-DST) — OK |
| `Etc/GMT-12` | `+12:00` | far-east — OK |
| `Etc/GMT+12` | `-12:00` | OK |
| `Etc/GMT-14` | `+14:00` | Kiribati range — OK |
| `Etc/GMT-5:30` | — | FAIL — `Unknown TimeZone 'Etc/GMT-5:30'!` |

Integer hours only. Fractional offsets (India UTC+05:30, Newfoundland
UTC-03:30, Chatham UTC+12:45) cannot be expressed as `Etc/GMT±N` and must be
served by their named IANA zones (`Asia/Kolkata`, `America/St_Johns`,
`Pacific/Chatham`) instead.

### Finding 3 — default session zone is the JVM/OS zone, not UTC

**Question.** What does a fresh DuckDB session default to for `TimeZone`?

**Answer — DuckDB inherits the host/JVM system default zone, not UTC.**

On a machine where the system default zone is `America/Costa_Rica`, a fresh
DuckDB session reported `current_setting('TimeZone') = America/Costa_Rica`, with
`TIMESTAMPTZ '2024-06-15 12:00:00+00'` rendering as `2024-06-15T06:00-06:00`.

**Implication — a silent portability hazard:**
- Dev machine in Costa Rica: DuckDB default = `America/Costa_Rica`.
- CI worker in a Linux container: DuckDB default = `UTC` (assuming container TZ).
- Production worker in another region: default = whatever the host OS says.

A query whose result depends on the session zone (which is most `TIMESTAMPTZ`
queries) will quietly produce different results on different hosts if the zone
is not explicitly set. The safe posture is to `SET TimeZone = '<trino_session_zone>'`
on **every** session, even for work believed to be TZ-invariant — both for a
deterministic debugging baseline and because any `TIMESTAMPTZ`-touching
predicate would otherwise inherit the hazard.

### Finding 4 — Trino session zone → DuckDB `SET TimeZone` normaliser

**Question.** Can a caller mechanically read Trino's session zone, normalise
it, and `SET TimeZone` on DuckDB such that subsequent extracts match what Trino
itself would compute?

**Answer — works end-to-end for every named IANA zone and every integer-hour
offset; cleanly refuses fractional bare offsets.**

14 inputs covering the realistic session-zone shape space, extracting
`year(TIMESTAMPTZ '2024-12-31 22:00:00+00')` and `hour(...)`, with `java.time`
(`Instant.atZone(ZoneId.of(zone))`) as ground truth:

| Trino zone | Normalised → DuckDB | DuckDB (year/hr) | Ground truth (year/hr) | Verdict |
|---|---|---|---|---|
| `UTC` | `UTC` | 2024 / 22 | 2024 / 22 | MATCH |
| `GMT` | `GMT` | 2024 / 22 | 2024 / 22 | MATCH |
| `Z` | `UTC` | 2024 / 22 | 2024 / 22 | MATCH |
| `America/Los_Angeles` | passthrough | 2024 / 14 | 2024 / 14 | MATCH |
| `Europe/Berlin` | passthrough | 2024 / 23 | 2024 / 23 | MATCH |
| `Asia/Singapore` | passthrough | **2025 / 6** | **2025 / 6** | MATCH (year-boundary smoking gun) |
| `Asia/Kolkata` (UTC+05:30) | passthrough | 2025 / 3 | 2025 / 3 | MATCH (fractional offset survives via named IANA) |
| `Pacific/Chatham` (UTC+12:45) | passthrough | 2025 / 11 | 2025 / 11 | MATCH |
| `+00:00` | `Etc/GMT-0` | 2024 / 22 | 2024 / 22 | MATCH |
| `+05:00` | `Etc/GMT-5` | 2025 / 3 | 2025 / 3 | MATCH |
| `-08:00` | `Etc/GMT+8` | 2024 / 14 | 2024 / 14 | MATCH |
| `+14:00` | `Etc/GMT-14` | 2025 / 12 | 2025 / 12 | MATCH |
| `+05:30` | `+05:30` (no translation possible) | — | (n/a) | REFUSED — `Unknown TimeZone '+05:30'!` |
| `-03:30` | `-03:30` | — | (n/a) | REFUSED |

**The three-rule normaliser** (validated above) is what a caller should apply
to Trino's session zone before emitting `SET TimeZone`:

1. `Z` → `UTC`.
2. `±HH:MM` with `MM == 00` → `Etc/GMT∓HH` (POSIX sign inversion).
3. Anything else (named IANA, `UTC`, `GMT`, fractional offset) → pass through
   unchanged. DuckDB accepts the named cases; fractional bare offsets are
   cleanly rejected.

Every integer-hour offset and every named IANA zone produces extracts that
exactly match `java.time`. Fractional bare-offset shapes (`+05:30`, `-03:30`)
fail cleanly with a useful DuckDB error message — the right outcome, because
there is no integer-hour `Etc/GMT-N` that means UTC+5:30. In practice fractional
offsets almost always arrive as the corresponding named IANA zone
(`Asia/Kolkata` rather than `+05:30`), so the failure mode is narrow. The
correct response to a refusal is to *not* push `TIMESTAMPTZ`-sensitive
predicates for that session and let Trino re-evaluate above the scan.

**No separate ICU install to assert.** Named IANA zones resolve without an
explicit `INSTALL icu; LOAD icu`; DuckDB's `icu` extension (needed only for the
`with_timezone` → `timezone()` rewrite the caller emits) is not load-bearing for
zone resolution. The correctness gate is "did `SET TimeZone = '<normalised>'`
succeed?", not "did ICU load?".

---

## Per-function divergence notes

Functions that look the same on the surface but aren't — and how the caller
should emit each one against DuckDB. Argument-shape classes below: **DATE**,
**TIMESTAMP-without-zone**, **TIMESTAMP-WITH-zone**, **format-string**.

### `day_of_week` — ISO numbering

- Trino: `1 = Monday … 7 = Sunday` (ISO).
- DuckDB `dayofweek(x)`: `0 = Sunday … 6 = Saturday` (non-ISO). ❌ would diverge.
- DuckDB `isodow(x)`: `1 = Monday … 7 = Sunday` (ISO). ← what the caller emits.
- Caller emits: `day_of_week(d)` → `isodow(d)`.

### `week` / `week_of_year` — both ISO

- Trino: `week(x)` and `week_of_year(x)` both return the ISO week (1–53). Trino
  has no separate `iso_week` function.
- DuckDB: bare `week(x)` is already ISO-aligned — verified empirically
  (`week('2023-01-01') = 52`, `week('2024-12-30') = 1`). No separate `isoweek`
  function exists in DuckDB.
- Caller emits: `week(d)` → `week(d)` and `week_of_year(d)` → `week(d)`.

### `year_of_week` / `yow` — ISO week-year, not calendar year

- Trino: `year_of_week(x)` / `yow(x)` return the ISO week-numbering year, NOT
  the calendar year. `2024-12-30` → `2025`. Trino has no `iso_year` function.
- DuckDB: no bare `isoyear()` function — reach it via `extract('isoyear' FROM x)`,
  cast to `BIGINT` to match Trino's return type.
- Caller emits: `year_of_week(d)` → `CAST(extract('isoyear' FROM d) AS BIGINT)`
  (and `yow` identically).

### `date_trunc(unit, x)` — unit set + Monday-start week caveat

- Trino units: `'second' | 'minute' | 'hour' | 'day' | 'week' | 'month' | 'quarter' | 'year'`.
- DuckDB units: a superset (`'microseconds' | 'millisecond' | … | 'decade' | 'century' | 'millennium'`).
- Direct passthrough is safe for the Trino unit set; both engines reject unknown
  units. **`'week'` truncation aligns iff both engines use Monday-start ISO
  weeks** — confirmed by the corpus below.
- Return-type caveat: DuckDB always returns `TIMESTAMP` even on `DATE` input,
  while Trino preserves `DATE → DATE` for unit ≥ day. An auto-cast keeps numeric
  comparisons aligned.

### `date_diff(unit, t1, t2)` — boundary-count semantics

- Trino: count of whole `unit` boundaries crossed between `t1` and `t2`.
  `date_diff('month', '2024-01-31', '2024-02-29') = 1`.
- DuckDB: same boundary-count semantics.
- The caller emits it as a direct passthrough; the **cross-month-end edge cases**
  are the test pressure point (pinned in the corpus). On `TIMESTAMP WITH TIME ZONE`
  inputs `date_diff` is session-sensitive like every other WTZ extract.

### `from_unixtime` / `to_unixtime`

- `from_unixtime(double)`: Trino returns `TIMESTAMP(3) WITH TIME ZONE` —
  so this function is **session-zone sensitive** (its rendered wall-clock
  components depend on the session zone). DuckDB's analogue is
  `to_timestamp(numeric)` returning `TIMESTAMPTZ`; same absolute instant,
  session-zone rendering. Caller emits: `from_unixtime(d)` → `to_timestamp(d)`.
- `to_unixtime(timestamp)`: Trino returns a `double` in seconds; DuckDB
  `epoch(timestamp)` returns `double` in seconds. When the input is a
  wall-clock `TIMESTAMP` this is TZ-invariant. Caller emits:
  `to_unixtime(t)` → `CAST(epoch(t) AS DOUBLE)`.

### `with_timezone` — argument-order flip

- Trino `with_timezone(ts, zone)` vs DuckDB `timezone(zone, ts)` — the caller
  flips the argument order: `with_timezone(t, zone)` → `timezone(zone, t)`.
  It requires DuckDB's bundled `icu` extension for `timezone()`, and is
  inherently session/zone-aware.

### `date_format` / `parse_datetime` / `format_datetime` — NOT pushable

- Trino uses Joda-Time format strings (`yyyy-MM-dd HH:mm:ss`); DuckDB uses
  C `strftime`/`strptime` (`%Y-%m-%d %H:%M:%S`). The two pattern languages are
  **incompatible** and there is no safe rewrite without owning a format-string
  translation layer.
- Consequently `date_format`, `date_parse`, `format_datetime`, `parse_datetime`
  (and `human_readable_seconds`) are **not pushable**. A caller must evaluate
  these above the scan rather than push them.

---

## Divergence-pressure test corpus

The empirical corpus that pins the caller's date/time rewrites to Trino. The
general principle: pick inputs where the engines would disagree if they were
using different rules. A test that passes on `2024-06-15 12:00:00 UTC` proves
nothing.

### DST transition pressure (session zone: `America/Los_Angeles`)

1. **Spring-forward gap:** `TIMESTAMP '2024-03-10 02:30:00'` — wall clock
   doesn't exist in LA. `hour()` returns 2 in both engines (read from wall
   clock). `date_trunc('day', ts)` → `2024-03-10 00:00:00`.
2. **Fall-back ambiguity:** `TIMESTAMP '2024-11-03 01:30:00'` — exists twice in
   LA. For `TIMESTAMP` no-TZ: irrelevant, wall clock reads 1. For
   `TIMESTAMP WITH TIME ZONE`: depends on which 01:30 the original instant was —
   pin first-occurrence and second-occurrence variants.
3. **Cross-zone year boundary** (session-sensitive):
   `TIMESTAMP '2024-12-31 22:00:00' AT TIME ZONE 'UTC'` viewed in an
   Asia/Singapore session → `year()` is **2025**, not 2024. The single most
   likely case to catch a missing session-zone alignment.

### Calendar boundary pressure

4. **Leap day (divisible-by-400):** `DATE '2000-02-29'`. `month`/`day` agree
   trivially; `date_diff('day', '2000-02-28', '2000-03-01') = 2` in both.
5. **Non-leap century:** `DATE '1900-02-28'`. `date_add('day', 1, …)` →
   `1900-03-01` (not `1900-02-29`).
6. **Leap day 2024:** `DATE '2024-02-29'`. `date_diff('month', '2024-01-31', '2024-02-29') = 1`;
   catches engines that disagree on "did we cross a month boundary."
7. **Month-end add:** `date_add('month', 1, DATE '2024-01-31')` → `2024-02-29`
   in both (clamps to month end) — a common divergence point in date libraries,
   verified aligned here.

### ISO year-numbering edge cases

8. `DATE '2024-01-01'` (Monday) — ISO week 1 of 2024. `year_of_week = 2024`.
9. `DATE '2024-12-30'` (Monday) — ISO week 1 of **2025**. `year_of_week = 2025`.
   The classic "ISO year ≠ calendar year" trap.
10. `DATE '2023-01-01'` (Sunday) — ISO week 52 of **2022**. `year_of_week = 2022`.
11. `DATE '2024-12-31'` (Tuesday) — ISO week 1 of 2025. `year = 2024`,
    `year_of_week = 2025`.

### Day-of-week numbering

12. `DATE '2024-01-07'` (Sunday). Trino `day_of_week = 7`. DuckDB
    `dayofweek = 0` (wrong!); `isodow = 7` (right). Pins the rewrite choice
    (`day_of_week` → `isodow`).

### Extreme dates + epoch

13. `DATE '0001-01-01'`, `DATE '9999-12-31'` — smoke test for overflow / format
    handling.
14. `TIMESTAMP '1970-01-01 00:00:00'` — epoch. `to_unixtime = 0` in both.
15. `TIMESTAMP '1969-12-31 23:59:59'` — negative epoch. `to_unixtime = -1`.

### Sub-second precision

16. `TIMESTAMP '2024-06-15 12:00:00.123456'` (microseconds) — Trino's default
    precision is 3 (millis), DuckDB is 6 (micros). Mismatched precision is a
    common divergence source. Verify `date_trunc('millisecond', ts)` rounds the
    same way; verify `millisecond()` returns millis-of-second (0..999), not
    epoch millis.

### `date_diff` semantics

17. `date_diff('month', '2024-01-31', '2024-02-29') = 1` (cross one month boundary).
18. `date_diff('day', '2024-02-29', '2024-03-01') = 1`.
19. `date_diff('year', TIMESTAMP '1999-12-31 23:59:59', TIMESTAMP '2000-01-01 00:00:01') = 1`.
20. `date_diff('year', TIMESTAMP '1999-12-31 23:59:59', TIMESTAMP '1999-12-31 23:59:59.999') = 0` (within same year).
21. `date_diff('hour', t1, t2)` across a DST spring-forward on
    `TIMESTAMP WITH TIME ZONE` — does the "lost hour" count? Both engines: yes
    (real elapsed time, via `epoch_diff`). If a future engine version diverges,
    `date_diff` on WTZ inputs must not be pushed.

### Week-truncation pressure

22. `date_trunc('week', DATE '2024-01-07')` — Sunday. Both engines
    (Monday-start ISO): `2024-01-01`. If DuckDB returned `2024-01-07`, the
    engine would be using Sunday-start and an explicit ISO-week truncation path
    would be needed.
23. `date_trunc('week', DATE '2024-01-01')` — Monday. Both: `2024-01-01`.
24. `date_trunc('week', DATE '2024-01-08')` — next Monday. Both: `2024-01-08`.

### Negative pressure tests (what should NOT push)

25. `year(TIMESTAMP WITH TIME ZONE …)` when the caller has not aligned the
    session zone — the predicate **must not push**; Trino re-applies it above
    the scan.
26. `date_format(ts, '%Y')` / `format_datetime(ts, 'yyyy')` — wrong
    format-string language; **never pushed** (not pushable — see above).

---

## Implications summary

1. **Align the DuckDB session `TimeZone` to Trino's session zone before
   evaluating any predicate**, unconditionally — even for `DATE` /
   `TIMESTAMP`-no-zone work that is TZ-invariant — for a deterministic baseline
   and to defend against the default-zone portability hazard (Finding 3).
2. **Use the validated three-rule normaliser** (Finding 4): `Z` → `UTC`;
   `±HH:MM` with `MM == 00` → `Etc/GMT∓HH` (POSIX inversion); everything else
   passes through. Fractional bare offsets fail cleanly — the correct outcome.
3. **Gate on `SET TimeZone` success, not on an ICU load.** ICU zone resolution
   is available without an explicit `LOAD icu`; DuckDB's `icu` extension is only
   load-bearing for the `with_timezone` → `timezone()` rewrite.
4. **`TIMESTAMPTZ` storage drops the zone** (Finding 1). Correctness for
   WTZ-sensitive predicates lives entirely on the session-`TimeZone` knob — a
   caller obligation that neither the extension nor a bare rewrite can enforce
   itself.
5. **Format/parse functions are out of scope** (Joda vs strftime); the caller
   evaluates them above the scan.

## Open follow-up questions

- **DST history accuracy across engines.** DuckDB ICU and Trino `java.time`
  both use IANA, but the tzdb version each was built against can differ. Probe a
  date in `Europe/Moscow` pre-2014 (Russia's permanent DST change) and
  `America/Mexico_City` pre-2022 (DST abolition) — if the engines disagree on
  the offset for any historical instant, pin tzdb versions or refuse to push for
  the affected window. Low-risk but cheap to verify.
- **Mid-query session-zone change.** Trino allows changing the session zone
  within a batch. If the caller picks up the session zone at predicate-planning
  time but the session changes before the DuckDB session's zone is set, the
  assumed zone might not match the set zone. Verify the in-flight session is
  stable across that boundary.

---

## Related reports

- [`REPORT-string-unicode-audit.md`](REPORT-string-unicode-audit.md) — the Unicode divergence audit behind the native string functions.
- [`REPORT-hash-null-handling.md`](REPORT-hash-null-handling.md) — hash-function NULL/empty semantics.
- [`RESEARCH-trino-duckdb-function-mapping.md`](RESEARCH-trino-duckdb-function-mapping.md) — the full Trino→DuckDB function-mapping survey.
- [`RESEARCH-duckdb-extension-coverage.md`](RESEARCH-duckdb-extension-coverage.md) — DuckDB built-in / extension coverage against Trino's function catalog.
