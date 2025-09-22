#!/usr/bin/env python3

"""Converts a .exports file to a format consumable by linkers.

An .exports file is a file with one exported symbol per line.
This script converts a .exports file into a format that linkers
can understand:
- It prepends a `_` to each line for use with -exported_symbols_list for Darwin
- It writes a .def file for use with /DEF: for Windows
- It writes a linker script for use with --version-script elsewhere
"""

import argparse
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--format", required=True, choices=("linux", "mac", "win"))
    parser.add_argument("source")
    parser.add_argument("output")
    args = parser.parse_args()

    symbols = open(args.source).readlines()

    if args.format == "linux":
        output_lines = (
            [
                "LLVM_0 {\n",
                "  global:\n",
            ]
            + ["    %s;\n" % s.rstrip() for s in symbols]
            + ["  local:\n", "    *;\n", "};\n"]
        )
    elif args.format == "mac":
        output_lines = ["_" + s for s in symbols]
    else:
        assert args.format == "win"
        output_lines = ["EXPORTS\n"] + ["  " + s for s in symbols]

    open(args.output, "w").writelines(output_lines)


if __name__ == "__main__":
    sys.exit(main())
