#!/usr/bin/env bash
# Fetch trino_parity.duckdb_extension binaries from the latest successful CI run
# on GitHub Actions, instead of building them locally. Useful when:
#   - You don't want to wait for the ~15 min Linux container build
#   - You don't have Docker installed for `make linux-arm64`
#   - You only need the binary for testing, not active development
#
# The fetched binaries land in the same `build/<platform>/release/extension/trino_parity/`
# paths that `make` and `make linux-<arch>` would produce, so the connector's
# gradle bundling picks them up automatically.
#
# Requirements:
#   - gh CLI (https://cli.github.com/) authenticated to GitHub
#   - The repo's CI workflow has completed at least one successful run on main
#
# Usage:
#   scripts/fetch-from-ci-artifacts.sh                # latest successful run on main
#   scripts/fetch-from-ci-artifacts.sh --run <id>     # specific run id
#   scripts/fetch-from-ci-artifacts.sh --platform linux-arm64,linux-amd64
#                                                      # subset
#
# Artifact naming: the upstream extension-ci-tools workflow uploads as
#   `trino_parity-${duckdb_version}-extension-${duckdb_arch}`
# where duckdb_arch is `linux_amd64`, `linux_arm64`, `osx_arm64`, `osx_amd64`,
# `windows_amd64`. This script renames `osx_*` → `darwin-*` to match the
# TrinoParityExtensionResolver's platform string format.

set -euo pipefail

REPO="${PARITY_EXTENSION_REPO:-brikk/duckdb-trino-parity-extension}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

RUN_ID=""
PLATFORMS_FILTER=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --run)
            RUN_ID="$2"
            shift 2
            ;;
        --platform)
            PLATFORMS_FILTER="$2"
            shift 2
            ;;
        -h|--help)
            sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 2
            ;;
    esac
done

if ! command -v gh >/dev/null 2>&1; then
    echo "FATAL: gh CLI not on PATH. Install from https://cli.github.com/ and run \`gh auth login\`." >&2
    exit 2
fi

if [[ -z "$RUN_ID" ]]; then
    echo "[fetch] looking up latest successful run on $REPO main..."
    RUN_ID=$(gh run list --repo "$REPO" --branch main --status success --limit 1 --json databaseId --jq '.[0].databaseId' 2>/dev/null || true)
    if [[ -z "$RUN_ID" || "$RUN_ID" == "null" ]]; then
        echo "FATAL: no successful CI run found on $REPO main. Pass --run <id> with a known-good run, or rebuild locally." >&2
        exit 3
    fi
fi

echo "[fetch] using run id $RUN_ID from $REPO"

# Download all artifacts from the run into a scratch dir.
SCRATCH="$(mktemp -d -t parity-extensions-XXXXXX)"
trap 'rm -rf "$SCRATCH"' EXIT

echo "[fetch] downloading artifacts into $SCRATCH ..."
gh run download "$RUN_ID" --repo "$REPO" --dir "$SCRATCH" || {
    echo "FATAL: gh run download failed. The run may have no artifacts (build failure) or be too old (GitHub expires artifacts after ~90 days)." >&2
    exit 4
}

# The upstream extension-ci-tools workflow uploads artifacts named
# `trino_parity-${duckdb_version}-extension-${duckdb_arch}`. Walk them and
# rewrite into our layout.
copied=0
for artifact_dir in "$SCRATCH"/*; do
    [[ -d "$artifact_dir" ]] || continue
    artifact_name="$(basename "$artifact_dir")"
    # Extract duckdb_arch (last hyphen-separated token).
    case "$artifact_name" in
        trino_parity-*-extension-*)
            duckdb_arch="${artifact_name##*-extension-}"
            ;;
        *)
            # Not one of our binary artifacts (could be a test-report bundle).
            continue
            ;;
    esac

    # Map upstream duckdb_arch -> our TrinoParityExtensionResolver platform string.
    case "$duckdb_arch" in
        osx_amd64)     platform="darwin-amd64" ;;
        osx_arm64)     platform="darwin-arm64" ;;
        linux_amd64)   platform="linux-amd64" ;;
        linux_arm64)   platform="linux-arm64" ;;
        windows_amd64) platform="windows-amd64" ;;
        *)
            echo "[fetch] skipping unknown architecture: $duckdb_arch"
            continue
            ;;
    esac

    if [[ -n "$PLATFORMS_FILTER" ]]; then
        if [[ ",$PLATFORMS_FILTER," != *",$platform,"* ]]; then
            continue
        fi
    fi

    binary="$artifact_dir/trino_parity.duckdb_extension"
    if [[ ! -f "$binary" ]]; then
        echo "[fetch] WARN: $artifact_name has no trino_parity.duckdb_extension inside, skipping"
        continue
    fi

    dest_dir="$PROJ_DIR/build/$platform/release/extension/trino_parity"
    mkdir -p "$dest_dir"
    cp "$binary" "$dest_dir/trino_parity.duckdb_extension"
    size=$(du -h "$dest_dir/trino_parity.duckdb_extension" | cut -f1)
    echo "[fetch] $platform: copied $size to build/$platform/release/extension/trino_parity/"
    copied=$((copied + 1))
done

if [[ $copied -eq 0 ]]; then
    echo "FATAL: no platform binaries found in run $RUN_ID. The run may have failed before the upload step." >&2
    exit 5
fi

echo "[fetch] done — $copied platform binaries fetched."
echo "[fetch] connector's gradle bundling will now pick them up; rebuild the plugin jar."
