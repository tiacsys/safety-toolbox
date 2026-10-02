#!/usr/bin/env python3
# Copyright (c) 2026 inovex GmbH
# SPDX-License-Identifier: Apache-2.0
"""Write a copy of the document registry without some documents.

doc/CMakeLists.txt calls this when SAFE_DATA_DOC_TEST_REPORT is OFF, so a
build without a twister run leaves the test report out. zdocs derives every
cross-document link and the navigation from the registry, so a document that
is not in the registry is not built and no peer links to it.

zdocs resolves a relative ``doc_dir:`` against the directory of the registry
file. The copy lives in the build directory, so this script makes every
``doc_dir:`` absolute (resolved against the original registry).
"""

import argparse
import sys
from pathlib import Path

import yaml


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--registry", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--drop", action="append", default=[],
                        help="registry id of a document to leave out (repeatable)")
    args = parser.parse_args()

    data = yaml.safe_load(args.registry.read_text(encoding="utf-8"))
    documents = data.get("documents", {})

    for doc_id in args.drop:
        if doc_id not in documents:
            print(f"registry_without: no document '{doc_id}' in {args.registry}",
                  file=sys.stderr)
            return 1
        del documents[doc_id]

    # A remaining document that names a dropped one (for example as its
    # testmodule spec) would fail later with a less clear message.
    for doc_id, entry in documents.items():
        text = yaml.safe_dump(entry)
        for dropped in args.drop:
            if dropped in text:
                print(f"registry_without: '{doc_id}' refers to dropped '{dropped}'",
                      file=sys.stderr)
                return 1

    base = args.registry.resolve().parent
    for entry in documents.values():
        doc_dir = entry.get("doc_dir")
        if doc_dir and not Path(doc_dir).is_absolute():
            entry["doc_dir"] = str(base / doc_dir)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    header = (f"# Generated from {args.registry.name} by {Path(__file__).name}; "
              f"left out: {', '.join(args.drop) or 'nothing'}. Do not edit.\n")
    args.out.write_text(header + yaml.safe_dump(data, sort_keys=False),
                        encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
