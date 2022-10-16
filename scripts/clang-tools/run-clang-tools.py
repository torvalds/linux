#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) Google LLC, 2020
#
# Author: Nathan Huckleberry <nhuck@google.com>
#
"""A helper routine run clang-tidy and the clang static-analyzer on
compile_commands.json.
"""

import argparse
import json
import multiprocessing
import subprocess
import sys


def parse_arguments():
    """Set up and parses command-line arguments.
    Returns:
        args: Dict of parsed args
        Has keys: [path, type]
    """
    usage = """Run clang-tidy or the clang static-analyzer on a
        compilation database."""
    parser = argparse.ArgumentParser(description=usage)

    type_help = "Type of analysis to be performed"
    parser.add_argument("type",
                        choices=["clang-tidy", "clang-analyzer"],
                        help=type_help)
    path_help = "Path to the compilation database to parse"
    parser.add_argument("path", type=str, help=path_help)

    return parser.parse_args()


def init(l, a):
    global lock
    global args
    lock = l
    args = a


def run_analysis(entry):
    # Disable all checks, then re-enable the ones we want
    checks = []
    checks.append("-checks=-*")
    if args.type == "clang-tidy":
        checks.append("linuxkernel-*")
    else:
        checks.append("clang-analyzer-*")
        checks.append("-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling")
    p = subprocess.run(["clang-tidy", "-p", args.path, ",".join(checks), entry["file"]],
                       stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT,
                       cwd=entry["directory"])
    with lock:
        sys.stderr.buffer.write(p.stdout)


def main():
    args = parse_arguments()

    lock = multiprocessing.Lock()
    pool = multiprocessing.Pool(initializer=init, initargs=(lock, args))
    # Read JSON data into the datastore variable
    with open(args.path, "r") as f:
        datastore = json.load(f)
        pool.map(run_analysis, datastore)


if __name__ == "__main__":
    main()
