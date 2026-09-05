#!/usr/bin/env python3
"""Import (or verify) the pinned, unmodified ICU sources from a local release archive."""

import argparse
import hashlib
from pathlib import Path, PurePosixPath
import shutil
import tarfile


URL = "https://github.com/unicode-org/icu/releases/download/release-76-1/icu4c-76_1-src.tgz"
SHA256 = "dfacb46bfe4747410472ce3e1144bf28a102feeaa4e3875bac9b4c6cf30f4f3e"
COMPONENTS = ("common", "i18n", "stubdata")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path, help=f"local archive downloaded from {URL}")
    parser.add_argument("--check", action="store_true", help="verify without modifying files")
    args = parser.parse_args()
    if hashlib.sha256(args.archive.read_bytes()).hexdigest() != SHA256:
        parser.error("archive SHA-256 does not match the pinned ICU 76.1 release")

    # Select only library sources/headers, upstream source manifests and license.
    # Reading members directly avoids extracting archive paths or symlinks.
    files = {}
    with tarfile.open(args.archive, "r:gz") as archive:
        for member in archive.getmembers():
            if not member.isfile():
                continue
            path = PurePosixPath(member.name)
            if path == PurePosixPath("icu/LICENSE"):
                relative = PurePosixPath("LICENSE")
            elif (len(path.parts) >= 4 and path.parts[:2] == ("icu", "source")
                  and path.parts[2] in COMPONENTS
                  and (path.suffix in (".cpp", ".h") or path.name == "sources.txt")):
                relative = PurePosixPath(*path.parts[2:])
            else:
                continue
            if ".." in relative.parts or relative.is_absolute():
                parser.error(f"unsafe archive path: {path}")
            source = archive.extractfile(member)
            assert source is not None
            with source:
                files[Path(relative)] = source.read()

    for component in COMPONENTS:
        sources = files[Path(component, "sources.txt")].decode("ascii").split()
        for source in sources:
            if Path(component, source) not in files:
                parser.error(f"missing source: {component}/{source}")

    destination = Path(__file__).resolve().parent
    if args.check:
        actual = {path.relative_to(destination) for component in COMPONENTS
                  for path in (destination / component).rglob("*") if path.is_file()}
        actual.add(Path("LICENSE"))
        mismatches = actual.symmetric_difference(files)
        for relative, data in files.items():
            path = destination / relative
            if not path.is_file() or path.read_bytes() != data:
                mismatches.add(relative)
        if mismatches:
            parser.error("vendor mismatch: " + ", ".join(str(p) for p in sorted(mismatches)))
        print(f"Verified {len(files)} unmodified ICU 76.1 files")
        return

    # These directories contain only imported files. Local build/configuration
    # files live alongside this script and are deliberately left untouched.
    for component in COMPONENTS:
        if (destination / component).exists():
            shutil.rmtree(destination / component)
    for relative, data in sorted(files.items()):
        path = destination / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    print(f"Imported {len(files)} unmodified ICU 76.1 files")


if __name__ == "__main__":
    main()
