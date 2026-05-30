#!/usr/bin/env bash
# Entrypoint for the Linux extension build container. Invoked by `make linux-arm64`
# (and `make linux-amd64`) from the repo root.
#
# Required env:
#   TARGET_PLATFORM     "linux-arm64" or "linux-amd64" — used only for log lines.
#                       Output location is determined by the /out mount; see Makefile.
# Required mounts:
#   /src                source repo (read-only is fine; we rsync out of it)
#   /out                destination for the built `.duckdb_extension`
#                       (host path: build/<platform>/release/extension/trino_parity/)
# Optional mounts (recommended for fast incremental builds):
#   /vcpkg-cache        VCPKG_DEFAULT_BINARY_CACHE — ICU et al. persist here
#   /root/.cache/ccache ccache object cache

set -euo pipefail

PLATFORM="${TARGET_PLATFORM:-unknown}"
echo "[trino_parity build] platform=${PLATFORM} arch=$(uname -m)"

if [[ ! -d /src ]]; then
    echo "FATAL: /src not mounted" >&2
    exit 2
fi
if [[ ! -d /out ]]; then
    echo "FATAL: /out not mounted" >&2
    exit 2
fi

# Snapshot /src into /workdir, EXCLUDING the host's build/ (which has artifacts
# for a different platform that would confuse cmake) and .git/ (huge, unneeded).
# rsync with --delete on subsequent runs is fast — only diffs copy.
echo "[trino_parity build] sync /src → /workdir"
rsync -a --delete \
    --exclude='build/' \
    --exclude='.git/' \
    /src/ /workdir/

cd /workdir

# Sanity-check that the duckdb submodule was checked out on the host. The
# template requires `git clone --recurse-submodules`; without it the build
# fails far downstream with a confusing CMake error.
if [[ ! -f duckdb/CMakeLists.txt ]]; then
    echo "FATAL: duckdb submodule appears empty (duckdb/CMakeLists.txt missing)." >&2
    echo "  Run \`git submodule update --init --recursive\` on the host first." >&2
    exit 2
fi
if [[ ! -f extension-ci-tools/makefiles/duckdb_extension.Makefile ]]; then
    echo "FATAL: extension-ci-tools submodule appears empty." >&2
    echo "  Run \`git submodule update --init --recursive\` on the host first." >&2
    exit 2
fi

# Build with ninja + ccache. VCPKG_TOOLCHAIN_PATH is baked into the image's env.
# Cap parallelism so Docker Desktop's default memory cap (often 8 GB) doesn't
# OOM-kill cc1plus during the DuckDB compilation — each heavy template can
# eat ~1.5 GB resident at -O3. Override with -e BUILD_PARALLELISM=N on
# beefier docker setups. 2 is conservative; bump if you have headroom.
BUILD_PARALLELISM="${BUILD_PARALLELISM:-2}"
echo "[trino_parity build] make (CMAKE_BUILD_PARALLEL_LEVEL=$BUILD_PARALLELISM)"
CMAKE_BUILD_PARALLEL_LEVEL="$BUILD_PARALLELISM" GEN=ninja make

# Copy the loadable extension to the host's per-platform output mount.
SRC_BIN=build/release/extension/trino_parity/trino_parity.duckdb_extension
DEST_DIR=/out/release/extension/trino_parity
DEST_BIN=$DEST_DIR/trino_parity.duckdb_extension

if [[ ! -f "$SRC_BIN" ]]; then
    echo "FATAL: build succeeded but $SRC_BIN missing — check the make output." >&2
    exit 3
fi

mkdir -p "$DEST_DIR"
cp "$SRC_BIN" "$DEST_BIN"
echo "[trino_parity build] wrote $(du -h "$DEST_BIN" | cut -f1) to host:build/${PLATFORM}/release/extension/trino_parity/trino_parity.duckdb_extension"
