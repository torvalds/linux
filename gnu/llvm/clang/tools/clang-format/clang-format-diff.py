#!/usr/bin/env python3
#
# ===- clang-format-diff.py - ClangFormat Diff Reformatter ----*- python -*--===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===------------------------------------------------------------------------===#

"""
This script reads input from a unified diff and reformats all the changed
lines. This is useful to reformat all the lines touched by a specific patch.
Example usage for git/svn users:

  git diff -U0 --no-color --relative HEAD^ | {clang_format_diff} -p1 -i
  svn diff --diff-cmd=diff -x-U0 | {clang_format_diff} -i

It should be noted that the filename contained in the diff is used unmodified
to determine the source file to update. Users calling this script directly
should be careful to ensure that the path in the diff is correct relative to the
current working directory.
"""
from __future__ import absolute_import, division, print_function

import argparse
import difflib
import re
import subprocess
import sys

if sys.version_info.major >= 3:
    from io import StringIO
else:
    from io import BytesIO as StringIO


def main():
    parser = argparse.ArgumentParser(
        description=__doc__.format(clang_format_diff="%(prog)s"),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "-i",
        action="store_true",
        default=False,
        help="apply edits to files instead of displaying a diff",
    )
    parser.add_argument(
        "-p",
        metavar="NUM",
        default=0,
        help="strip the smallest prefix containing P slashes",
    )
    parser.add_argument(
        "-regex",
        metavar="PATTERN",
        default=None,
        help="custom pattern selecting file paths to reformat "
        "(case sensitive, overrides -iregex)",
    )
    parser.add_argument(
        "-iregex",
        metavar="PATTERN",
        default=r".*\.(?:cpp|cc|c\+\+|cxx|cppm|ccm|cxxm|c\+\+m|c|cl|h|hh|hpp"
        r"|hxx|m|mm|inc|js|ts|proto|protodevel|java|cs|json|s?vh?)",
        help="custom pattern selecting file paths to reformat "
        "(case insensitive, overridden by -regex)",
    )
    parser.add_argument(
        "-sort-includes",
        action="store_true",
        default=False,
        help="let clang-format sort include blocks",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="be more verbose, ineffective without -i",
    )
    parser.add_argument(
        "-style",
        help="formatting style to apply (LLVM, GNU, Google, Chromium, "
        "Microsoft, Mozilla, WebKit)",
    )
    parser.add_argument(
        "-fallback-style",
        help="The name of the predefined style used as a"
        "fallback in case clang-format is invoked with"
        "-style=file, but can not find the .clang-format"
        "file to use.",
    )
    parser.add_argument(
        "-binary",
        default="clang-format",
        help="location of binary to use for clang-format",
    )
    args = parser.parse_args()

    # Extract changed lines for each file.
    filename = None
    lines_by_file = {}
    for line in sys.stdin:
        match = re.search(r"^\+\+\+\ (.*?/){%s}(\S*)" % args.p, line)
        if match:
            filename = match.group(2)
        if filename is None:
            continue

        if args.regex is not None:
            if not re.match("^%s$" % args.regex, filename):
                continue
        else:
            if not re.match("^%s$" % args.iregex, filename, re.IGNORECASE):
                continue

        match = re.search(r"^@@.*\+(\d+)(?:,(\d+))?", line)
        if match:
            start_line = int(match.group(1))
            line_count = 1
            if match.group(2):
                line_count = int(match.group(2))
                # The input is something like
                #
                # @@ -1, +0,0 @@
                #
                # which means no lines were added.
                if line_count == 0:
                    continue
            # Also format lines range if line_count is 0 in case of deleting
            # surrounding statements.
            end_line = start_line
            if line_count != 0:
                end_line += line_count - 1
            lines_by_file.setdefault(filename, []).extend(
                ["-lines", str(start_line) + ":" + str(end_line)]
            )

    # Reformat files containing changes in place.
    has_diff = False
    for filename, lines in lines_by_file.items():
        if args.i and args.verbose:
            print("Formatting {}".format(filename))
        command = [args.binary, filename]
        if args.i:
            command.append("-i")
        if args.sort_includes:
            command.append("-sort-includes")
        command.extend(lines)
        if args.style:
            command.extend(["-style", args.style])
        if args.fallback_style:
            command.extend(["-fallback-style", args.fallback_style])

        try:
            p = subprocess.Popen(
                command,
                stdout=subprocess.PIPE,
                stderr=None,
                stdin=subprocess.PIPE,
                universal_newlines=True,
            )
        except OSError as e:
            # Give the user more context when clang-format isn't
            # found/isn't executable, etc.
            raise RuntimeError(
                'Failed to run "%s" - %s"' % (" ".join(command), e.strerror)
            )

        stdout, stderr = p.communicate()
        if p.returncode != 0:
            return p.returncode

        if not args.i:
            with open(filename) as f:
                code = f.readlines()
            formatted_code = StringIO(stdout).readlines()
            diff = difflib.unified_diff(
                code,
                formatted_code,
                filename,
                filename,
                "(before formatting)",
                "(after formatting)",
            )
            diff_string = "".join(diff)
            if len(diff_string) > 0:
                has_diff = True
                sys.stdout.write(diff_string)

    if has_diff:
        return 1


if __name__ == "__main__":
    sys.exit(main())
