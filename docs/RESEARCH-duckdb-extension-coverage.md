# RESEARCH: DuckDB extension coverage of Trino-only functions

> **Scope note.** The `trino_parity` extension ships only 10 native functions
> (see the repo [README](../README.md)): 7 ICU string functions and the 3
> vendored-lib hash functions (`trino_xxhash64/sha512/hmac_sha256`) discussed
> below. Every other Trino function — including the aligned / rename / rewrite
> cases — is emitted by the caller directly against DuckDB and is **not** part
> of the extension. Nothing else ships as a macro.

This doc substantiates the **scope** of `trino_parity`. Trino exposes a large
surface of functions that DuckDB lacks natively. Some of those gaps can be
served by other DuckDB extensions — one core (`inet`) and a handful of
community ones — while the rest remain genuinely uncovered by any published
extension.

It answers three questions:

1. **Which Trino gaps could another DuckDB extension close** (and with what
   function/construct), so a caller that loads that extension could push those
   operations down.
2. **What stays uncovered** by any surveyed extension — the candidates a strict
   Trino-parity layer must implement itself.
3. **Why `trino_parity` implements the hash gaps natively** instead of leaning
   on `crypto` / `hashfuncs` at runtime.

For the function-by-function mapping of what DuckDB does vs Trino, see
[RESEARCH-trino-duckdb-function-mapping.md](RESEARCH-trino-duckdb-function-mapping.md).
The native-implementation rationale for specific domains lives in
[REPORT-hash-null-handling.md](REPORT-hash-null-handling.md),
[REPORT-string-unicode-audit.md](REPORT-string-unicode-audit.md), and
[REPORT-datetime-tz-handling.md](REPORT-datetime-tz-handling.md).

Many other DuckDB community extensions exist (bitfilters, clamp,
decimal_arithmetic, faiss, fuzzycomplete, geosilo, jsonata, lastra, lindel,
lsh, marisa, markdown, vindex, …), but none of them close a Trino-parity gap on
the target function list, so they are omitted here.

---

## Which extension covers which Trino gap

| Trino-only operation | Extension | Function / construct |
|---|---|---|
| `sha512(varbinary)` | crypto | `crypto_hash('sha2-512', x)` |
| `hmac_md5/sha1/sha256/sha512` | crypto | `crypto_hmac(algo, key, msg)` |
| `xxhash64(varbinary)` | hashfuncs | `xxh64(x)` |
| `murmur3(varbinary)` (128-bit) | hashfuncs | `murmurhash3_x64_128(x)` / `murmurhash3_128(x)` |
| `theta_sketch_*` family | datasketches | `datasketch_theta*` (build/estimate/union/intersect/a_not_b) |
| `tdigest_agg` + quantile reads | datasketches | `datasketch_tdigest*` |
| `qdigest_agg` (closest substitute) | datasketches | `datasketch_kll*` / `datasketch_quantiles*` / `datasketch_req*` |
| `approx_most_frequent` | datasketches | `datasketch_frequent_items*` |
| `url_extract_protocol/host/port/path/query/fragment` | netquack | `extract_schema/host/port/path/query_string/fragment` |
| `url_extract_parameter(url, name)` | netquack | join through `extract_query_parameters(url)` table function |
| `soundex(char)` | splink_udfs | `soundex(s)` |
| IPADDRESS / IPPREFIX type, subnet `contains(network, address)` | **inet (core)** | `INET` type + `>>=` / `<<=` operators; `network`, `netmask`, `broadcast`, `host` helpers |
| IP validators / classifiers / arithmetic | netquack | `is_valid_ip`, `is_private_ip`, `ip_version`, `ip_to_int`, `int_to_ip`, `ipcalc(cidr)` |

A caller that loads the relevant extension could push each of these down as
shown. `trino_parity` itself does not depend on any of them at runtime (see the
native-hashes section below for why).

---

## Still uncovered by any extension

Trino-only operations with **no** community-extension substitute. These are the
candidates a Trino-parity extension must implement itself:

- **Hashes**: `crc32`, `spooky_hash_v2_32/64`
- **Strings**: `word_stem`, `luhn_check`, `to_base64url`, `to_base32`,
  `split_to_map`, 3-arg `strpos`, `regexp_count`, `regexp_position`
- **Numeric**: `from_base`, `beta_cdf`, `inverse_beta_cdf`, `normal_cdf`,
  `inverse_normal_cdf`, `t_cdf`, `t_pdf`, `format_number`, `parse_data_size`
- **Date/time**: `parse_duration`, `human_readable_seconds`,
  `timezone_hour`, `timezone_minute`
- **Array**: `array_remove`, `array_union`, `array_except`, `ngrams`
  (array form), `combinations`, `shuffle`
- **Binary**: `from_big_endian_32/64`, `to_big_endian_32/64`,
  `from_ieee754_32/64`, `to_ieee754_32/64`, binary `reverse`, binary `substr`
- **Aggregates**: `listagg WITHIN GROUP`, `map_agg`, `multimap_agg`,
  `checksum`

---

## The six extensions that actually cover a Trino gap

Only the following close a gap on the target list. Every other surveyed
extension is `❌ None` and is not listed.

### crypto
<https://duckdb.org/community_extensions/extensions/crypto> — cryptographic
hashes, HMACs, and CSPRNG (OpenSSL-backed).

- `crypto_hash(algo, x)` — hash for `sha2-256`, `sha2-512`, `sha3-256`, `sha3-512`, `md5`, `sha1`, `blake3`; output is hex VARCHAR (cast if VARBINARY required).
  - Trino `sha512(x)` → `crypto_hash('sha2-512', x)`.
- `crypto_hmac(algo, key, msg)` — HMAC for all of the above except `blake3`.
  - Trino `hmac_md5/sha1/sha256/sha512(k, m)` → `crypto_hmac('md5'|'sha1'|'sha2-256'|'sha2-512', k, m)`.
- `crypto_random_bytes(len)` — CSPRNG bytes.
- `crypto_hash_agg(algo, col ORDER BY …)` — aggregate hash (requires `ORDER BY` for determinism).
- No `crc32`, `xxhash64`, `spooky_hash_v2_*`, or `murmur3` (non-cryptographic; see hashfuncs).

### hashfuncs
<https://duckdb.org/community_extensions/extensions/hashfuncs> —
non-cryptographic hash families (MurmurHash3, xxHash, RapidHash).

- `xxh64(x[, seed])` — 64-bit xxHash. Trino `xxhash64(varbinary)` → `xxh64(x)`, direct algorithm match.
- `murmurhash3_x64_128(x[, seed])` / `murmurhash3_128(x[, seed])` — 128-bit MurmurHash3. Trino `murmur3(varbinary)` (128-bit) → either.
- `murmurhash3_32`, `xxh32`, `xxh3_64`, `xxh3_128`, `xxh3_128_hex`, `rapidhash` — other non-cryptographic hashes with no Trino target.
- No `crc32`, `spooky_hash_v2_*`, no SHA family, no HMAC.

### datasketches
<https://duckdb.org/community_extensions/extensions/datasketches> — Apache
DataSketches port (CPC, HLL, KLL, Quantiles, REQ, T-Digest, Theta, Frequent
Items).

- `datasketch_theta*` (build / `_estimate` / `_union` / `_intersect` / `_a_not_b`) — Trino `theta_sketch_*` family.
- `datasketch_tdigest` + `_quantile` / `_cdf` / `_rank` — Trino `tdigest_agg` and quantile reads.
- `datasketch_kll*` / `datasketch_quantiles*` / `datasketch_req*` — closest substitute for Trino `qdigest_agg` (no exact qdigest port; KLL is Apache's recommended modern replacement).
- `datasketch_frequent_items(lg_max_k, col)` + `_get_frequent(sketch, error_type)` — Trino `approx_most_frequent(buckets, value, capacity)`.
- **Sketch state serialization is NOT wire-compatible with Trino's** — only computed-value paths (estimates, quantiles, set-op results) are safe to push; serialized sketch blobs cannot be exchanged with Trino.
- No `listagg WITHIN GROUP`, `map_agg`, `multimap_agg`, `checksum`.

### netquack
<https://duckdb.org/community_extensions/extensions/netquack> —
domain / URI / IP / web-path parsing.

- `extract_schema/host/port/path/query_string/fragment(url)` — Trino `url_extract_protocol/host/port/path/query/fragment`.
- `extract_query_parameters(url)` (table function) — Trino `url_extract_parameter(url, name)` via a join (not a scalar; pushdown shape changes).
- `is_valid_ip`, `is_private_ip`, `ip_version`, `ip_to_int`, `int_to_ip`, `ipcalc(cidr)` — IP validators/classifiers/arithmetic; operate on VARCHAR, useful alongside `inet` but not a substitute for its `INET` type.
- Also `extract_domain/subdomain/tld/extension`, `normalize_url`, `base64_encode/decode`, Tranco ranking — no additional Trino target.

### splink_udfs
<https://duckdb.org/community_extensions/extensions/splink_udfs> — phonetic /
text-normalization / record-linkage functions.

- `soundex(s)` — Trino `soundex(char)`, direct match.
- `ngrams(s, n)` returns **string** character n-grams — this is *different* from Trino's array `ngrams(array, n)`; do not conflate.
- `double_metaphone`, `strip_diacritics`, `unaccent`, faster `levenshtein` / `damerau_levenshtein` — no additional Trino target.
- No `word_stem`, `luhn_check`, base64-url/base32, `split_to_map`, 3-arg `strpos`, `regexp_count`, `regexp_position`.

### inet (core extension — not community)
<https://duckdb.org/docs/stable/core_extensions/inet> — unified `INET` type
plus network operators and helpers. (DuckDB *does* have an IP type; a claim to
the contrary is incorrect.)

- `INET` type — single type covering IPv4 and IPv6 with optional CIDR embedded in the value. Trino `IPADDRESS` → `INET`; Trino's separate `IPPREFIX` is represented as CIDR inside the same `INET` value rather than a distinct type.
- `>>=` — subnet-contains-or-equal. Trino `contains(network, address)` (subnet check) → `network >>= address`. Push as an **operator**, not a function call.
- `<<=` — subnet-contained-by-or-equal.
- `host`, `netmask`, `network`, `broadcast` — network-component helpers.
- `+` / `-` (INET, INTEGER) — address arithmetic (increment/decrement).
- No first-class `ip_prefix(addr, len)` (use a literal CIDR or cast) and no scalar `is_subnet_of` (operator form only).
- The core `inet` extension is always available with DuckDB.

---

## Why `trino_parity` implements hashes natively

`crypto` and `hashfuncs` would, on paper, fill three of the extension's hash
gaps — `trino_sha512`, `trino_xxhash64`, and the `trino_hmac_*` family. The
extension deliberately does **not** depend on them at runtime. Instead it
implements these natively via vendored single-file libraries.

The reason is dependency fragility. Community-extension availability lags each
DuckDB release: after a DuckDB point release the community catalog can take
weeks to publish matching builds for every extension and platform. A
server-side layer that resolved `trino_sha512` by requiring a not-yet-built
`crypto` for the running DuckDB version would fail to load exactly when a fresh
DuckDB is deployed. By vendoring the hash implementations, `trino_parity` is a
single self-contained artifact with no runtime chain to a separately-published
extension.

This is also why the extension implements these three hashes natively rather
than deferring to a community extension: it controls the NULL/empty/encoding
semantics end-to-end rather than inheriting whatever a third-party extension
chose. See
[REPORT-hash-null-handling.md](REPORT-hash-null-handling.md) for the
NULL-handling and VARBINARY-wrapping decisions those native implementations
pin down.
