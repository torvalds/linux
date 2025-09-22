#!/usr/bin/env python
#
# Common lint functions applicable to multiple types of files.

from __future__ import print_function
import re


def VerifyLineLength(filename, lines, max_length):
    """Checks to make sure the file has no lines with lines exceeding the length
    limit.

    Args:
      filename: the file under consideration as string
      lines: contents of the file as string array
      max_length: maximum acceptable line length as number

    Returns:
      A list of tuples with format [(filename, line number, msg), ...] with any
      violations found.
    """
    lint = []
    line_num = 1
    for line in lines:
        length = len(line.rstrip("\n"))
        if length > max_length:
            lint.append(
                (
                    filename,
                    line_num,
                    "Line exceeds %d chars (%d)" % (max_length, length),
                )
            )
        line_num += 1
    return lint


def VerifyTabs(filename, lines):
    """Checks to make sure the file has no tab characters.

    Args:
      filename: the file under consideration as string
      lines: contents of the file as string array

    Returns:
      A list of tuples with format [(line_number, msg), ...] with any violations
      found.
    """
    lint = []
    tab_re = re.compile(r"\t")
    line_num = 1
    for line in lines:
        if tab_re.match(line.rstrip("\n")):
            lint.append((filename, line_num, "Tab found instead of whitespace"))
        line_num += 1
    return lint


def VerifyTrailingWhitespace(filename, lines):
    """Checks to make sure the file has no lines with trailing whitespace.

    Args:
      filename: the file under consideration as string
      lines: contents of the file as string array

    Returns:
      A list of tuples with format [(filename, line number, msg), ...] with any
      violations found.
    """
    lint = []
    trailing_whitespace_re = re.compile(r"\s+$")
    line_num = 1
    for line in lines:
        if trailing_whitespace_re.match(line.rstrip("\n")):
            lint.append((filename, line_num, "Trailing whitespace"))
        line_num += 1
    return lint


class BaseLint:
    def RunOnFile(filename, lines):
        raise Exception("RunOnFile() unimplemented")


def RunLintOverAllFiles(linter, filenames):
    """Runs linter over the contents of all files.

    Args:
      lint: subclass of BaseLint, implementing RunOnFile()
      filenames: list of all files whose contents will be linted

    Returns:
      A list of tuples with format [(filename, line number, msg), ...] with any
      violations found.
    """
    lint = []
    for filename in filenames:
        file = open(filename, "r")
        if not file:
            print("Could not open %s" % filename)
            continue
        lines = file.readlines()
        lint.extend(linter.RunOnFile(filename, lines))

    return lint
