# Extension updating 
The current target is **DuckDB 1.5.5**. The calendar plans 1.5.6 for
2026-09-16 and 2.0 for the second half of October 2026; neither automatically
changes our pins. Apply the procedure below only when adopting a release,
with build and semantic regression checks against that release.

EV-E3 / **0.5.0** independently upgrades the statically vendored ICU to
**76.1 / Unicode 16.0**, targeting Trino 483 on pinned JDK 25. Linux 1.5.5
build/tests and the exhaustive scalar/NFC oracle checks pass; cross-platform
CI and publication remain pending. See the
[validation report](REPORT-unicode16-validation.md) for exact coverage and pins.
See [TODO](../TODO.md) for the agreed scope and
[`third_party/icu/`](../third_party/icu/) for vendor provenance. Landing 0.4.0
does not block implementing or testing 0.5.0; changing the manifest version
does not publish a release. Community publication requires the catalog ref
update and rebuilt, published signed binaries.

When cloning this template, the target version of DuckDB should be the latest stable release of DuckDB. However, there 
will inevitably come a time when a new DuckDB is released and the extension repository needs updating. This process goes
as follows:

- Bump submodules
  - `./duckdb` should be set to latest tagged release
  - `./extension-ci-tools` should be set to updated branch corresponding to latest DuckDB release. So if you're building for DuckDB `v1.1.0` there will be a branch in `extension-ci-tools` named `v1.1.0` to which you should check out. 
- Bump versions in `./github/workflows`
  - `duckdb_version` input in `duckdb-stable-build` job in `MainDistributionPipeline.yml` should be set to latest tagged release
  - `duckdb_version` input in `duckdb-stable-deploy` job in `MainDistributionPipeline.yml` should be set to latest tagged release
  - the reusable workflow `duckdb/extension-ci-tools/.github/workflows/_extension_distribution.yml` for the `duckdb-stable-build` job should be set to latest tagged release

# API changes
DuckDB's 2.0 preview does not force stable-ABI adoption. Evaluate that option
separately from any source fixes needed to build against a new release. Our
statically vendored ICU is independent of DuckDB's ICU and need not be removed
for 2.0. Recheck built-in `lower`/`upper`/`nfc_normalize` semantics on the actual
release rather than inferring that they are unchanged from the preview.

DuckDB extensions built with this extension template are built against the internal C++ API of DuckDB. This API is not guaranteed to be stable.
What this means for extension development is that when updating your extensions DuckDB target version using the above steps, you may run into the fact that your extension no longer builds properly.

Currently, DuckDB does not (yet) provide a specific change log for these API changes, but it is generally not too hard to figure out what has changed.

For figuring out how and why the C++ API changed, we recommend using the following resources:
- DuckDB's [Release Notes](https://github.com/duckdb/duckdb/releases)
- DuckDB's history of [Core extension patches](https://github.com/duckdb/duckdb/commits/main/.github/patches/extensions)
- The git history of the relevant C++ Header file of the API that has changed
