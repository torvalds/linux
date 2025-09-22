#!/usr/bin/env python3

"""Replaces absolute line numbers in lit-tests with relative line numbers.

Writing line numbers like 152 in 'RUN: or CHECK:' makes tests hard to maintain:
inserting lines in the middle of the test means updating all the line numbers.

Encoding them relative to the current line helps, and tools support it:
    Lit will substitute %(line+2) with the actual line number
    FileCheck supports [[@LINE+2]]

This tool takes a regex which captures a line number, and a list of test files.
It searches for line numbers in the files and replaces them with a relative
line number reference.
"""

USAGE = """Example usage:
    find -type f clang/test/CodeCompletion | grep -v /Inputs/ | \\
    xargs relative_lines.py --dry-run --verbose --near=100 \\
    --pattern='-code-completion-at[ =]%s:(\d+)' \\
    --pattern='requires fix-it: {(\d+):\d+-(\d+):\d+}'
"""

import argparse
import re
import sys


def b(x):
    return bytes(x, encoding="utf-8")


parser = argparse.ArgumentParser(
    prog="relative_lines",
    description=__doc__,
    epilog=USAGE,
    formatter_class=argparse.RawTextHelpFormatter,
)
parser.add_argument(
    "--near", type=int, default=20, help="maximum line distance to make relative"
)
parser.add_argument(
    "--partial",
    action="store_true",
    default=False,
    help="apply replacements to files even if others failed",
)
parser.add_argument(
    "--pattern",
    default=[],
    action="append",
    type=lambda x: re.compile(b(x)),
    help="regex to match, with line numbers captured in ().",
)
parser.add_argument(
    "--verbose", action="store_true", default=False, help="print matches applied"
)
parser.add_argument(
    "--dry-run",
    action="store_true",
    default=False,
    help="don't apply replacements. Best with --verbose.",
)
parser.add_argument("files", nargs="+")
args = parser.parse_args()

for file in args.files:
    try:
        contents = open(file, "rb").read()
    except UnicodeDecodeError as e:
        print(f"{file}: not valid UTF-8 - {e}", file=sys.stderr)
    failures = 0

    def line_number(offset):
        return 1 + contents[:offset].count(b"\n")

    def replace_one(capture, line, offset):
        """Text to replace a capture group, e.g. 42 => %(line+1)"""
        try:
            target = int(capture)
        except ValueError:
            print(f"{file}:{line}: matched non-number '{capture}'", file=sys.stderr)
            return capture

        if args.near > 0 and abs(target - line) > args.near:
            print(
                f"{file}:{line}: target line {target} is farther than {args.near}",
                file=sys.stderr,
            )
            return capture
        if target > line:
            delta = "+" + str(target - line)
        elif target < line:
            delta = "-" + str(line - target)
        else:
            delta = ""

        prefix = contents[:offset].rsplit(b"\n")[-1]
        is_lit = b"RUN" in prefix or b"DEFINE" in prefix
        text = ("%(line{0})" if is_lit else "[[@LINE{0}]]").format(delta)
        if args.verbose:
            print(f"{file}:{line}: {0} ==> {text}")
        return b(text)

    def replace_match(m):
        """Text to replace a whole match, e.g. --at=42:3 => --at=%(line+2):3"""
        line = 1 + contents[: m.start()].count(b"\n")
        result = b""
        pos = m.start()
        for index, capture in enumerate(m.groups()):
            index += 1  # re groups are conventionally 1-indexed
            result += contents[pos : m.start(index)]
            replacement = replace_one(capture, line, m.start(index))
            result += replacement
            if replacement == capture:
                global failures
                failures += 1
            pos = m.end(index)
        result += contents[pos : m.end()]
        return result

    for pattern in args.pattern:
        contents = re.sub(pattern, replace_match, contents)
    if failures > 0 and not args.partial:
        print(f"{file}: leaving unchanged (some failed, --partial not given)")
        continue
    if not args.dry_run:
        open(file, "wb").write(contents)
