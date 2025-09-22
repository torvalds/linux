#!/usr/bin/env python3

import argparse
import re
import os

HEADER = """\
//===-- SBLanguages.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBLANGUAGE_H
#define LLDB_API_SBLANGUAGE_H

namespace lldb {
/// Used by \\ref SBExpressionOptions.
/// These enumerations use the same language enumerations as the DWARF
/// specification for ease of use and consistency.
enum SBSourceLanguageName : uint16_t {
"""

FOOTER = """\
};

} // namespace lldb

#endif
"""

REGEX = re.compile(
    r'^ *HANDLE_DW_LNAME *\( *(?P<value>[^,]+), (?P<name>.*), "(?P<comment>[^"]+)",.*\)'
)


def emit_enum(input, output):
    # Read the input and break it up by lines.
    lines = []
    with open(input, "r") as f:
        lines = f.readlines()

    # Create output folder if it does not exist
    os.makedirs(os.path.dirname(output), exist_ok=True)

    # Write the output.
    with open(output, "w") as f:
        # Emit the header.
        f.write(HEADER)

        # Emit the enum values.
        for line in lines:
            match = REGEX.match(line)
            if not match:
                continue
            f.write(f"  /// {match.group('comment')}.\n")
            f.write(f"  eLanguageName{match.group('name')} = {match.group('value')},\n")

        # Emit the footer
        f.write(FOOTER)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", "-o")
    parser.add_argument("input")
    args = parser.parse_args()

    emit_enum(args.input, args.output)


if __name__ == "__main__":
    main()
