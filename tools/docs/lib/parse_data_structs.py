#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2016-2025 by Mauro Carvalho Chehab <mchehab@kernel.org>.
# pylint: disable=R0912,R0915

"""
Parse a source file or header, creating ReStructured Text cross references.

It accepts an optional file to change the default symbol reference or to
suppress symbols from the output.

It is capable of identifying defines, functions, structs, typedefs,
enums and enum symbols and create cross-references for all of them.
It is also capable of distinguish #define used for specifying a Linux
ioctl.

The optional rules file contains a set of rules like:

    ignore ioctl VIDIOC_ENUM_FMT
    replace ioctl VIDIOC_DQBUF vidioc_qbuf
    replace define V4L2_EVENT_MD_FL_HAVE_FRAME_SEQ :c:type:`v4l2_event_motion_det`
"""

import os
import re
import sys


class ParseDataStructs:
    """
    Creates an enriched version of a Kernel header file with cross-links
    to each C data structure type.

    It is meant to allow having a more comprehensive documentation, where
    uAPI headers will create cross-reference links to the code.

    It is capable of identifying defines, functions, structs, typedefs,
    enums and enum symbols and create cross-references for all of them.
    It is also capable of distinguish #define used for specifying a Linux
    ioctl.

    By default, it create rules for all symbols and defines, but it also
    allows parsing an exception file. Such file contains a set of rules
    using the syntax below:

    1. Ignore rules:

        ignore <type> <symbol>`

    Removes the symbol from reference generation.

    2. Replace rules:

        replace <type> <old_symbol> <new_reference>

    Replaces how old_symbol with a new reference. The new_reference can be:
        - A simple symbol name;
        - A full Sphinx reference.

    On both cases, <type> can be:
        - ioctl: for defines that end with _IO*, e.g. ioctl definitions
        - define: for other defines
        - symbol: for symbols defined within enums;
        - typedef: for typedefs;
        - enum: for the name of a non-anonymous enum;
        - struct: for structs.

    Examples:

        ignore define __LINUX_MEDIA_H
        ignore ioctl VIDIOC_ENUM_FMT
        replace ioctl VIDIOC_DQBUF vidioc_qbuf
        replace define V4L2_EVENT_MD_FL_HAVE_FRAME_SEQ :c:type:`v4l2_event_motion_det`
    """

    # Parser regexes with multiple ways to capture enums and structs
    RE_ENUMS = [
        re.compile(r"^\s*enum\s+([\w_]+)\s*\{"),
        re.compile(r"^\s*enum\s+([\w_]+)\s*$"),
        re.compile(r"^\s*typedef\s*enum\s+([\w_]+)\s*\{"),
        re.compile(r"^\s*typedef\s*enum\s+([\w_]+)\s*$"),
    ]
    RE_STRUCTS = [
        re.compile(r"^\s*struct\s+([_\w][\w\d_]+)\s*\{"),
        re.compile(r"^\s*struct\s+([_\w][\w\d_]+)$"),
        re.compile(r"^\s*typedef\s*struct\s+([_\w][\w\d_]+)\s*\{"),
        re.compile(r"^\s*typedef\s*struct\s+([_\w][\w\d_]+)$"),
    ]

    # FIXME: the original code was written a long time before Sphinx C
    # domain to have multiple namespaces. To avoid to much turn at the
    # existing hyperlinks, the code kept using "c:type" instead of the
    # right types. To change that, we need to change the types not only
    # here, but also at the uAPI media documentation.
    DEF_SYMBOL_TYPES = {
        "ioctl": {
            "prefix": "\\ ",
            "suffix": "\\ ",
            "ref_type": ":ref",
            "description": "IOCTL Commands",
        },
        "define": {
            "prefix": "\\ ",
            "suffix": "\\ ",
            "ref_type": ":ref",
            "description": "Macros and Definitions",
        },
        # We're calling each definition inside an enum as "symbol"
        "symbol": {
            "prefix": "\\ ",
            "suffix": "\\ ",
            "ref_type": ":ref",
            "description": "Enumeration values",
        },
        "typedef": {
            "prefix": "\\ ",
            "suffix": "\\ ",
            "ref_type": ":c:type",
            "description": "Type Definitions",
        },
        # This is the description of the enum itself
        "enum": {
            "prefix": "\\ ",
            "suffix": "\\ ",
            "ref_type": ":c:type",
            "description": "Enumerations",
        },
        "struct": {
            "prefix": "\\ ",
            "suffix": "\\ ",
            "ref_type": ":c:type",
            "description": "Structures",
        },
    }

    def __init__(self, debug: bool = False):
        """Initialize internal vars"""
        self.debug = debug
        self.data = ""

        self.symbols = {}

        for symbol_type in self.DEF_SYMBOL_TYPES:
            self.symbols[symbol_type] = {}

    def store_type(self, symbol_type: str, symbol: str,
                   ref_name: str = None, replace_underscores: bool = True):
        """
        Stores a new symbol at self.symbols under symbol_type.

        By default, underscores are replaced by "-"
        """
        defs = self.DEF_SYMBOL_TYPES[symbol_type]

        prefix = defs.get("prefix", "")
        suffix = defs.get("suffix", "")
        ref_type = defs.get("ref_type")

        # Determine ref_link based on symbol type
        if ref_type:
            if symbol_type == "enum":
                ref_link = f"{ref_type}:`{symbol}`"
            else:
                if not ref_name:
                    ref_name = symbol.lower()

                # c-type references don't support hash
                if ref_type == ":ref" and replace_underscores:
                    ref_name = ref_name.replace("_", "-")

                ref_link = f"{ref_type}:`{symbol} <{ref_name}>`"
        else:
            ref_link = symbol

        self.symbols[symbol_type][symbol] = f"{prefix}{ref_link}{suffix}"

    def store_line(self, line):
        """Stores a line at self.data, properly indented"""
        line = "    " + line.expandtabs()
        self.data += line.rstrip(" ")

    def parse_file(self, file_in: str):
        """Reads a C source file and get identifiers"""
        self.data = ""
        is_enum = False
        is_comment = False
        multiline = ""

        with open(file_in, "r",
                  encoding="utf-8", errors="backslashreplace") as f:
            for line_no, line in enumerate(f):
                self.store_line(line)
                line = line.strip("\n")

                # Handle continuation lines
                if line.endswith(r"\\"):
                    multiline += line[-1]
                    continue

                if multiline:
                    line = multiline + line
                    multiline = ""

                # Handle comments. They can be multilined
                if not is_comment:
                    if re.search(r"/\*.*", line):
                        is_comment = True
                    else:
                        # Strip C99-style comments
                        line = re.sub(r"(//.*)", "", line)

                if is_comment:
                    if re.search(r".*\*/", line):
                        is_comment = False
                    else:
                        multiline = line
                        continue

                # At this point, line variable may be a multilined statement,
                # if lines end with \ or if they have multi-line comments
                # With that, it can safely remove the entire comments,
                # and there's no need to use re.DOTALL for the logic below

                line = re.sub(r"(/\*.*\*/)", "", line)
                if not line.strip():
                    continue

                # It can be useful for debug purposes to print the file after
                # having comments stripped and multi-lines grouped.
                if self.debug > 1:
                    print(f"line {line_no + 1}: {line}")

                # Now the fun begins: parse each type and store it.

                # We opted for a two parsing logic here due to:
                # 1. it makes easier to debug issues not-parsed symbols;
                # 2. we want symbol replacement at the entire content, not
                #    just when the symbol is detected.

                if is_enum:
                    match = re.match(r"^\s*([_\w][\w\d_]+)\s*[\,=]?", line)
                    if match:
                        self.store_type("symbol", match.group(1))
                    if "}" in line:
                        is_enum = False
                    continue

                match = re.match(r"^\s*#\s*define\s+([\w_]+)\s+_IO", line)
                if match:
                    self.store_type("ioctl", match.group(1),
                                    replace_underscores=False)
                    continue

                match = re.match(r"^\s*#\s*define\s+([\w_]+)(\s+|$)", line)
                if match:
                    self.store_type("define", match.group(1))
                    continue

                match = re.match(r"^\s*typedef\s+([_\w][\w\d_]+)\s+(.*)\s+([_\w][\w\d_]+);",
                                 line)
                if match:
                    name = match.group(2).strip()
                    symbol = match.group(3)
                    self.store_type("typedef", symbol, ref_name=name)
                    continue

                for re_enum in self.RE_ENUMS:
                    match = re_enum.match(line)
                    if match:
                        self.store_type("enum", match.group(1))
                        is_enum = True
                        break

                for re_struct in self.RE_STRUCTS:
                    match = re_struct.match(line)
                    if match:
                        self.store_type("struct", match.group(1))
                        break

    def process_exceptions(self, fname: str):
        """
        Process exceptions file with rules to ignore or replace references.
        """
        if not fname:
            return

        name = os.path.basename(fname)

        with open(fname, "r", encoding="utf-8", errors="backslashreplace") as f:
            for ln, line in enumerate(f):
                ln += 1
                line = line.strip()
                if not line or line.startswith("#"):
                    continue

                # Handle ignore rules
                match = re.match(r"^ignore\s+(\w+)\s+(\S+)", line)
                if match:
                    c_type = match.group(1)
                    symbol = match.group(2)

                    if c_type not in self.DEF_SYMBOL_TYPES:
                        sys.exit(f"{name}:{ln}: {c_type} is invalid")

                    d = self.symbols[c_type]
                    if symbol in d:
                        del d[symbol]

                    continue

                # Handle replace rules
                match = re.match(r"^replace\s+(\S+)\s+(\S+)\s+(\S+)", line)
                if not match:
                    sys.exit(f"{name}:{ln}: invalid line: {line}")

                c_type, old, new = match.groups()

                if c_type not in self.DEF_SYMBOL_TYPES:
                    sys.exit(f"{name}:{ln}: {c_type} is invalid")

                reftype = None

                # Parse reference type when the type is specified

                match = re.match(r"^\:c\:(data|func|macro|type)\:\`(.+)\`", new)
                if match:
                    reftype = f":c:{match.group(1)}"
                    new = match.group(2)
                else:
                    match = re.search(r"(\:ref)\:\`(.+)\`", new)
                    if match:
                        reftype = match.group(1)
                        new = match.group(2)

                # If the replacement rule doesn't have a type, get default
                if not reftype:
                    reftype = self.DEF_SYMBOL_TYPES[c_type].get("ref_type")
                    if not reftype:
                        reftype = self.DEF_SYMBOL_TYPES[c_type].get("real_type")

                new_ref = f"{reftype}:`{old} <{new}>`"

                # Change self.symbols to use the replacement rule
                if old in self.symbols[c_type]:
                    self.symbols[c_type][old] = new_ref
                else:
                    print(f"{name}:{ln}: Warning: can't find {old} {c_type}")

    def debug_print(self):
        """
        Print debug information containing the replacement rules per symbol.
        To make easier to check, group them per type.
        """
        if not self.debug:
            return

        for c_type, refs in self.symbols.items():
            if not refs:  # Skip empty dictionaries
                continue

            print(f"{c_type}:")

            for symbol, ref in sorted(refs.items()):
                print(f"  {symbol} -> {ref}")

            print()

    def gen_output(self):
        """Write the formatted output to a file."""

        # Avoid extra blank lines
        text = re.sub(r"\s+$", "", self.data) + "\n"
        text = re.sub(r"\n\s+\n", "\n\n", text)

        # Escape Sphinx special characters
        text = re.sub(r"([\_\`\*\<\>\&\\\\:\/\|\%\$\#\{\}\~\^])", r"\\\1", text)

        # Source uAPI files may have special notes. Use bold font for them
        text = re.sub(r"DEPRECATED", "**DEPRECATED**", text)

        # Delimiters to catch the entire symbol after escaped
        start_delim = r"([ \n\t\(=\*\@])"
        end_delim = r"(\s|,|\\=|\\:|\;|\)|\}|\{)"

        # Process all reference types
        for ref_dict in self.symbols.values():
            for symbol, replacement in ref_dict.items():
                symbol = re.escape(re.sub(r"([\_\`\*\<\>\&\\\\:\/])", r"\\\1", symbol))
                text = re.sub(fr'{start_delim}{symbol}{end_delim}',
                              fr'\1{replacement}\2', text)

        # Remove "\ " where not needed: before spaces and at the end of lines
        text = re.sub(r"\\ ([\n ])", r"\1", text)
        text = re.sub(r" \\ ", " ", text)

        return text

    def gen_toc(self):
        """
        Create a TOC table pointing to each symbol from the header
        """
        text = []

        # Add header
        text.append(".. contents:: Table of Contents")
        text.append("   :depth: 2")
        text.append("   :local:")
        text.append("")

        # Sort symbol types per description
        symbol_descriptions = []
        for k, v in self.DEF_SYMBOL_TYPES.items():
            symbol_descriptions.append((v['description'], k))

        symbol_descriptions.sort()

        # Process each category
        for description, c_type in symbol_descriptions:

            refs = self.symbols[c_type]
            if not refs:  # Skip empty categories
                continue

            text.append(f"{description}")
            text.append("-" * len(description))
            text.append("")

            # Sort symbols alphabetically
            for symbol, ref in sorted(refs.items()):
                text.append(f"* :{ref}:")

            text.append("")  # Add empty line between categories

        return "\n".join(text)

    def write_output(self, file_in: str, file_out: str, toc: bool):
        title = os.path.basename(file_in)

        if toc:
            text = self.gen_toc()
        else:
            text = self.gen_output()

        with open(file_out, "w", encoding="utf-8", errors="backslashreplace") as f:
            f.write(".. -*- coding: utf-8; mode: rst -*-\n\n")
            f.write(f"{title}\n")
            f.write("=" * len(title) + "\n\n")

            if not toc:
                f.write(".. parsed-literal::\n\n")

            f.write(text)
