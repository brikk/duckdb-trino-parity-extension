PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Configuration of extension
EXT_NAME=trino_parity
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile

# ---- Cross-platform builds via Docker ----
#
# The default `make` target above builds for the HOST platform (darwin-arm64 on
# Apple Silicon, linux-amd64 in CI runners). The targets below build for OTHER
# Linux platforms via a Docker container, so a macOS dev box can produce the
# Linux binary that the Quack server testcontainer needs.
#
# Output goes to `build/<platform>/release/extension/trino_parity/trino_parity.duckdb_extension`.
# The trino-ducklake gradle plugin walks every available `build/<platform>/...`
# path and bundles each binary into its platform-specific resource slot.
#
# First build: ~10-15 min (DuckDB + ICU compiled inside the container).
# Subsequent builds: seconds (ccache + vcpkg binary cache, both persisted via
# named Docker volumes).
#
# Cross-arch on macOS arm64: `linux-arm64` runs as native arm64 (fast).
# `linux-amd64` runs under Rosetta/qemu emulation (slow — minutes per file).
# Use linux-amd64 only when actually needed; linux-arm64 is the daily driver.

DOCKER ?= docker
DOCKER_BUILD_IMAGE = brikk-trino-parity-linux-builder
VCPKG_CACHE_VOLUME = brikk-trino-parity-vcpkg-cache
CCACHE_VOLUME = brikk-trino-parity-ccache

# DuckDB's CMakeLists.txt determines the version baked into the extension's
# compatibility metadata via `git describe --tags --long` against the duckdb
# submodule. Inside the build container we rsync without .git/ — there's no
# git context — so we compute the OVERRIDE_GIT_DESCRIBE value on the host
# (which DOES have git context) and pass it through. Without this the binary
# claims v0.0.1 and DuckDB v1.5.3 refuses to load it.
DUCKDB_GIT_DESCRIBE := $(shell git -C $(PROJ_DIR)duckdb describe --tags --long 2>/dev/null || echo "v1.5.3-0-g0000000000")

.PHONY: docker-image linux-arm64 linux-amd64 all-platforms

docker-image:
	$(DOCKER) build -t $(DOCKER_BUILD_IMAGE) docker -f docker/Dockerfile.linux-build

# build/linux-arm64/release/extension/trino_parity/trino_parity.duckdb_extension
linux-arm64: docker-image
	@mkdir -p $(PROJ_DIR)build/linux-arm64
	$(DOCKER) run --rm --platform linux/arm64 \
		-v $(PROJ_DIR):/src \
		-v $(PROJ_DIR)build/linux-arm64:/out \
		-v $(VCPKG_CACHE_VOLUME):/vcpkg-cache \
		-v $(CCACHE_VOLUME):/root/.cache/ccache \
		-e VCPKG_DEFAULT_BINARY_CACHE=/vcpkg-cache \
		-e TARGET_PLATFORM=linux-arm64 \
		-e OVERRIDE_GIT_DESCRIBE=$(DUCKDB_GIT_DESCRIBE) \
		$(DOCKER_BUILD_IMAGE)

linux-amd64: docker-image
	@mkdir -p $(PROJ_DIR)build/linux-amd64
	$(DOCKER) run --rm --platform linux/amd64 \
		-v $(PROJ_DIR):/src \
		-v $(PROJ_DIR)build/linux-amd64:/out \
		-v $(VCPKG_CACHE_VOLUME):/vcpkg-cache \
		-v $(CCACHE_VOLUME):/root/.cache/ccache \
		-e VCPKG_DEFAULT_BINARY_CACHE=/vcpkg-cache \
		-e TARGET_PLATFORM=linux-amd64 \
		-e OVERRIDE_GIT_DESCRIBE=$(DUCKDB_GIT_DESCRIBE) \
		$(DOCKER_BUILD_IMAGE)

all-platforms: linux-arm64 linux-amd64
