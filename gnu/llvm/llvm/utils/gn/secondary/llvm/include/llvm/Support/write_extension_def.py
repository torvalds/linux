#!/usr/bin/env python3

import argparse
import os
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("exts", nargs="*", help="list of supported extensions")
    parser.add_argument("-o", "--output", required=True, help="output file")
    args = parser.parse_args()

    output = "".join(["HANDLE_EXTENSION(%s)\n" % ext for ext in args.exts])
    output += "#undef HANDLE_EXTENSION\n"

    if not os.path.exists(args.output) or open(args.output).read() != output:
        open(args.output, "w").write(output)


if __name__ == "__main__":
    sys.exit(main())
