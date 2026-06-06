# Third-party sources vendored for trino_parity hash functions

These files back the native `trino_xxhash64`, `trino_sha512`, and
`trino_hmac_sha256` scalar functions (see `src/hash_functions.cpp`). They were
vendored to avoid a runtime dependency on the `hashfuncs` / `crypto` DuckDB
community extensions (both of which also ship load-time telemetry).

## xxhash.h

- Upstream: https://github.com/Cyan4973/xxHash
- Copyright (c) 2012-2023 Yann Collet
- License: **BSD 2-Clause** (notice retained in the file header)
- Used header-only via `#define XXH_INLINE_ALL`. Only `XXH64` is called.

## WjCryptLib_Sha256.{c,h}, WjCryptLib_Sha512.{c,h}

- Upstream: https://github.com/WaterJuice/WjCryptLib
- Original SHA author: Tom St Denis (libtom); modified by WaterJuice.
- License: **Public Domain (Unlicense)** — no attribution obligation; credited
  here as good practice.
- Compiled as C (the `.c` files); included from C++ under `extern "C"`.

Both licenses are compatible with this extension's Apache-2.0 license. The files
are vendored verbatim from upstream; do not edit them in place.
