#!/usr/bin/env python3
r"""Independent Unicode 16 checks of an explicitly selected extension binary.

Requires Python >=3.10, stock `python3 -m pip install duckdb==1.5.5`, and the
OpenJDK 25 GA archive at JDK_URL (verify JDK_SHA256 before extracting). Download
NORMALIZATION_URL separately; its hash is checked on every full run. No network
access, extension INSTALL, builds, or runtime extension dependencies are added.

Example (Linux x64, downloaded/extracted dependencies in /tmp/opencode):
  python3 scripts/check_unicode.py \
    --extension /absolute/path/to/trino_parity.duckdb_extension \
    --java /tmp/opencode/jdk-25/bin/java \
    --normalization-test /tmp/opencode/NormalizationTest-16.0.0.txt

For an old DuckDB 1.5.5-compatible binary, use --extension OLD --canary-only.
It MUST exit 1 on a canary mismatch; a LOAD/dependency error exits 2 instead.
No version-mismatch override is used. --canary-only needs neither Java nor UCD.
"""

import argparse
from collections import Counter
import hashlib
from itertools import islice
from pathlib import Path
import subprocess
import sys
import tempfile

JDK_URL = (
    "https://download.java.net/java/GA/jdk25/"
    "bd75d5f9689641da8e1daabeccb5528b/36/GPL/openjdk-25_linux-x64_bin.tar.gz"
)
JDK_SHA256 = "59cdcaf255add4721de38eb411d4ecfe779356b61fb671aee63c7dec78054c2b"
NORMALIZATION_URL = "https://www.unicode.org/Public/16.0.0/ucd/NormalizationTest.txt"
NORMALIZATION_SHA256 = "d811971453e7075e1ad56fb1b301eece5aa80757b81f6156e74a1bfb3ae5ceb1"
SCALAR_COUNT = 0x110000 - 0x800
ROOT = Path(__file__).resolve().parents[1]


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def scalars():
    return (cp for cp in range(0x110000) if not 0xD800 <= cp <= 0xDFFF)


def batches(rows, size):
    rows = iter(rows)
    while batch := list(islice(rows, size)):
        yield batch


def codepoints(value):
    if value is None:
        return "<NULL>"
    return " ".join(f"U+{ord(c):04X}" for c in value) or "<empty>"


def normalization_cases(path):
    if sha256(path) != NORMALIZATION_SHA256:
        raise ValueError("NormalizationTest SHA-256 mismatch; use " + NORMALIZATION_URL)
    cases, part1 = [], {}
    part = None
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        if line.startswith("@Part"):
            part = line
            continue
        columns = ["".join(chr(int(cp, 16)) for cp in field.split())
                   for field in line.split(";")[:5]]
        if len(columns) != 5 or any(not c for c in columns):
            raise ValueError(f"Invalid normalization row {number}")
        if part == "@Part1":
            if len(columns[0]) != 1:
                raise ValueError(f"Non-singleton Part1 source at {number}")
            part1[ord(columns[0])] = columns[1]
        # NFC(c1)=NFC(c2)=NFC(c3)=c2; NFC(c4)=NFC(c5)=c4.
        for index, value in enumerate(columns):
            cases.append((f"NormalizationTest:{number}:c{index + 1}", value,
                          columns[1 if index < 3 else 3]))
    if len(cases) != 19965 * 5 or not part1:
        raise ValueError("Incomplete Unicode 16 normalization corpus")
    return cases, part1


def canaries(connection):
    # This deliberately supports only the boolean-query subset used by this file.
    # Keep the quick binary check and sqllogic regression expectations identical.
    path = ROOT / "test/sql/trino_unicode16.test"
    lines = path.read_text(encoding="utf-8").splitlines()
    count = failures = index = 0
    while index < len(lines):
        line = lines[index].strip()
        index += 1
        if not line or line.startswith("#") or line == "require trino_parity":
            continue
        if line != "query I":
            raise ValueError(f"Unsupported canary record at {path}:{index}")
        start, sql = index, []
        while lines[index] != "----":
            sql.append(lines[index])
            index += 1
        if lines[index + 1] != "true":
            raise ValueError(f"Canary must expect true at {path}:{start}")
        index += 2
        count += 1
        if connection.execute("\n".join(sql)).fetchall() != [(True,)]:
            failures += 1
            print(f"FAIL {path}:{start + 1}: {' '.join(sql)}", flush=True)
    if not count:
        raise ValueError("No canaries found")
    print(f"Canaries: {count} queries, {failures} mismatches", flush=True)
    return failures


def check_scalars(connection, path, part1, batch_size):
    names = ("lower", "upper", "trim(single)", "ltrim(single)", "rtrim(single)",
             "trim(edges)", "ltrim(edges)", "rtrim(edges)", "NFC/JDK", "JDK-NFC/UCD")
    failures = Counter()
    count = shown = 0
    expected_cps = iter(scalars())
    with path.open(encoding="ascii") as source:
        for batch in batches(source, batch_size):
            inputs, expected = [], []
            for line in batch:
                cp, lower, upper, whitespace, nfc = line.rstrip("\n").split("\t")
                cp = int(cp)
                if cp != next(expected_cps, None) or whitespace not in ("true", "false"):
                    raise ValueError("Incomplete/out-of-order Java scalar enumeration")
                value = chr(cp)
                ws = whitespace == "true"
                jdk_nfc = bytes.fromhex(nfc).decode("utf-8")
                inputs.append(value)
                expected.append((chr(int(lower)), chr(int(upper)),
                                 "" if ws else value, "" if ws else value,
                                 "" if ws else value, "x" if ws else value + "x" + value,
                                 "x" + value if ws else value + "x" + value,
                                 value + "x" if ws else value + "x" + value,
                                 jdk_nfc, part1.get(cp, value)))
            actual = connection.execute("""
                SELECT trino_lower(s), trino_upper(s),
                       trino_trim(s), trino_ltrim(s), trino_rtrim(s),
                       trino_trim(s || 'x' || s), trino_ltrim(s || 'x' || s),
                       trino_rtrim(s || 'x' || s), trino_normalize(s)
                FROM (SELECT unnest(?::VARCHAR[]) AS s)
            """, [inputs]).fetchall()
            for value, want, got in zip(inputs, expected, actual, strict=True):
                # Also check the Part1 singleton results and identity for every
                # scalar outside Part1 (a superset of the assigned-only rule).
                for name, expected_value, actual_value in zip(names, want, (*got, want[8]), strict=True):
                    if expected_value != actual_value:
                        failures[name] += 1
                        if shown < 12:
                            print(f"FAIL {name} {codepoints(value)}: expected "
                                  f"{codepoints(expected_value)}, got {codepoints(actual_value)}")
                            shown += 1
            count += len(inputs)
    if count != SCALAR_COUNT or next(expected_cps, None) is not None:
        raise ValueError(f"Incomplete scalar enumeration: {count}")
    print(f"Scalars: {count}, mismatches by check: {dict(failures)}", flush=True)
    return sum(failures.values())


def check_sequences(connection, cases, path, batch_size):
    failures = Counter()
    shown = count = 0
    with path.open(encoding="ascii") as oracle:
        for batch in batches(zip(cases, oracle, strict=True), batch_size):
            inputs = [case[1] for case, _ in batch]
            actual = connection.execute(
                "SELECT trino_normalize(s) FROM (SELECT unnest(?::VARCHAR[]) AS s)",
                [inputs]).fetchall()
            for ((label, value, ucd), line), (got,) in zip(batch, actual, strict=True):
                jdk = bytes.fromhex(line.strip()).decode("utf-8")
                for name, want, result in (("NFC/UCD", ucd, got), ("NFC/JDK", jdk, got),
                                           ("JDK-NFC/UCD", ucd, jdk)):
                    if want != result:
                        failures[name] += 1
                        if shown < 12:
                            print(f"FAIL {name} {label} input={codepoints(value)}: "
                                  f"expected {codepoints(want)}, got {codepoints(result)}")
                            shown += 1
                count += 1
    print(f"NFC sequences: {count} column checks, mismatches: {dict(failures)}", flush=True)
    return sum(failures.values())


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=f"JDK_URL={JDK_URL}\nJDK_SHA256={JDK_SHA256}\n"
               f"NORMALIZATION_URL={NORMALIZATION_URL}\nNORMALIZATION_SHA256={NORMALIZATION_SHA256}")
    parser.add_argument("--extension", required=True, type=Path, help="actual binary to LOAD, never a name")
    parser.add_argument("--java", type=Path, help="pinned OpenJDK 25 GA bin/java")
    parser.add_argument("--normalization-test", type=Path, help="Unicode 16.0.0 NormalizationTest.txt")
    parser.add_argument("--canary-only", action="store_true", help="quick check, exit 1 on old-data mismatches")
    parser.add_argument("--batch-size", type=int, default=8192)
    args = parser.parse_args()
    if not 1 <= args.batch_size <= 65536:
        parser.error("--batch-size must be between 1 and 65536")
    if not args.canary_only and (args.java is None or args.normalization_test is None):
        parser.error("full validation requires --java and --normalization-test")

    import duckdb

    if duckdb.__version__ != "1.5.5":
        raise ValueError(f"Expected stock duckdb==1.5.5, got {duckdb.__version__}")
    extension = args.extension.resolve(strict=True)
    fingerprint = sha256(extension)
    print(f"Extension: {extension}\nSHA-256: {fingerprint}", flush=True)
    with duckdb.connect(config={"allow_unsigned_extensions": "true", "threads": "1",
                                "memory_limit": "256MB", "autoload_known_extensions": "false",
                                "autoinstall_known_extensions": "false"}) as connection:
        print(f"DuckDB: {connection.execute('SELECT version()').fetchall()[0][0]}; "
              f"platform={connection.execute('PRAGMA platform').fetchall()[0][0]}; "
              f"module={duckdb.__file__}", flush=True)
        connection.execute("LOAD '" + str(extension).replace("'", "''") + "'")
        failures = canaries(connection)
        if not args.canary_only:
            cases, part1 = normalization_cases(args.normalization_test)
            print(f"UCD: {NORMALIZATION_URL}\nSHA-256: {NORMALIZATION_SHA256}\n"
                  f"Pinned JDK archive (Linux x64): {JDK_URL}\nSHA-256: {JDK_SHA256}", flush=True)
            with tempfile.TemporaryDirectory(prefix="unicode-oracle-") as temporary:
                work = Path(temporary)
                with (work / "sequences.hex").open("w", encoding="ascii") as out:
                    for _, value, _ in cases:
                        out.write(value.encode("utf-8").hex() + "\n")
                subprocess.run([str(args.java.resolve(strict=True)), "-Xmx128m", "-XX:+UseSerialGC",
                                "-XX:ActiveProcessorCount=1", str(ROOT / "scripts/UnicodeOracle.java"),
                                str(work)], check=True)
                failures += check_scalars(connection, work / "scalars.tsv", part1, args.batch_size)
                failures += check_sequences(connection, cases, work / "normalized.hex", args.batch_size)
        if sha256(extension) != fingerprint:
            raise ValueError("Extension binary changed during validation; rerun on an immutable artifact")
    print(f"{'FAIL' if failures else 'PASS'}: {failures} mismatches", flush=True)
    return 1 if failures else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(2)
