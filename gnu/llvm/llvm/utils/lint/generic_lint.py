#!/usr/bin/env python
#
# Checks files to make sure they conform to LLVM standards which can be applied
# to any programming language: at present, line length and trailing whitespace.

import common_lint
import sys


class GenericCodeLint(common_lint.BaseLint):
    MAX_LINE_LENGTH = 80

    def RunOnFile(self, filename, lines):
        common_lint.VerifyLineLength(filename, lines, GenericCodeLint.MAX_LINE_LENGTH)
        common_lint.VerifyTrailingWhitespace(filename, lines)


def GenericCodeLintMain(filenames):
    common_lint.RunLintOverAllFiles(GenericCodeLint(), filenames)
    return 0


if __name__ == "__main__":
    sys.exit(GenericCodeLintMain(sys.argv[1:]))
