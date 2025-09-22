#!/usr/bin/env python
# ===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===##

from argparse import ArgumentParser
import sys


def print_and_exit(msg):
    sys.stderr.write(msg + "\n")
    sys.exit(1)


def main():
    parser = ArgumentParser(description="Concatenate two files into a single file")
    parser.add_argument(
        "-o",
        "--output",
        dest="output",
        required=True,
        help="The output file. stdout is used if not given",
        type=str,
        action="store",
    )
    parser.add_argument(
        "files", metavar="files", nargs="+", help="The files to concatenate"
    )

    args = parser.parse_args()

    if len(args.files) < 2:
        print_and_exit("fewer than 2 inputs provided")
    data = ""
    for filename in args.files:
        with open(filename, "r") as f:
            data += f.read()
        if len(data) != 0 and data[-1] != "\n":
            data += "\n"
    assert len(data) > 0 and "cannot cat empty files"
    with open(args.output, "w") as f:
        f.write(data)


if __name__ == "__main__":
    main()
    sys.exit(0)
