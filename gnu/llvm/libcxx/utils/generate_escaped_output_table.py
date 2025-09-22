#!/usr/bin/env python
# ===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===##

# The code is based on
# https://github.com/microsoft/STL/blob/main/tools/unicode_properties_parse/grapheme_break_property_data_gen.py
#
# Copyright (c) Microsoft Corporation.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

from io import StringIO
from pathlib import Path
from dataclasses import dataclass
from typing import Optional
import re
import sys


@dataclass
class PropertyRange:
    lower: int = -1
    upper: int = -1
    prop: str = None


@dataclass
class Entry:
    lower: int = -1
    offset: int = -1


LINE_REGEX = re.compile(
    r"^(?P<lower>[0-9A-F]{4,6})(?:\.\.(?P<upper>[0-9A-F]{4,6}))?\s*;\s*(?P<prop>\w+)"
)


# https://www.unicode.org/reports/tr44/#GC_Values_Table
def filterGeneralProperty(element: PropertyRange) -> Optional[PropertyRange]:
    if element.prop in ["Zs", "Zl", "Zp", "Cc", "Cf", "Cs", "Co", "Cn"]:
        return element
    return None


def parsePropertyLine(inputLine: str) -> Optional[PropertyRange]:
    result = PropertyRange()
    if m := LINE_REGEX.match(inputLine):
        lower_str, upper_str, result.prop = m.group("lower", "upper", "prop")
        result.lower = int(lower_str, base=16)
        result.upper = result.lower
        if upper_str is not None:
            result.upper = int(upper_str, base=16)
        return result

    else:
        return None


def compactPropertyRanges(input: list[PropertyRange]) -> list[PropertyRange]:
    """
    Merges overlapping and consecutive ranges to one range.

    Since the input properties are filtered the exact property isn't
    interesting anymore. The properties in the output are merged to aid
    debugging.
    Merging the ranges results in fewer ranges in the output table,
    reducing binary and improving lookup performance.
    """
    result = list()
    for x in input:
        if (
            len(result)
            and x.lower > result[-1].lower
            and x.lower <= result[-1].upper + 1
        ):
            result[-1].upper = max(result[-1].upper, x.upper)
            result[-1].prop += f" {x.prop}"
            continue
        result.append(x)
    return result


DATA_ARRAY_TEMPLATE = r"""
/// The entries of the characters to escape in format's debug string.
///
/// Contains the entries for [format.string.escaped]/2.2.1.2.1
///   CE is a Unicode encoding and C corresponds to a UCS scalar value whose
///   Unicode property General_Category has a value in the groups Separator (Z)
///   or Other (C), as described by table 12 of UAX #44
///
/// Separator (Z) consists of General_Category
/// - Space_Separator,
/// - Line_Separator,
/// - Paragraph_Separator.
///
/// Other (C) consists of General_Category
/// - Control,
/// - Format,
/// - Surrogate,
/// - Private_Use,
/// - Unassigned.
///
/// The data is generated from
/// - https://www.unicode.org/Public/UCD/latest/ucd/extracted/DerivedGeneralCategory.txt
///
/// The table is similar to the table
///  __extended_grapheme_custer_property_boundary::__entries
/// which explains the details of these classes. The only difference is this
/// table lacks a property, thus having more bits available for the size.
///
/// The data has 2 values:
/// - bits [0, 13] The size of the range, allowing 16384 elements.
/// - bits [14, 31] The lower bound code point of the range. The upper bound of
///   the range is lower bound + size. Note the code expects code units the fit
///   into 18 bits, instead of the 21 bits needed for the full Unicode range.
_LIBCPP_HIDE_FROM_ABI inline constexpr uint32_t __entries[{size}] = {{
{entries}}};

/// Returns whether the code unit needs to be escaped.
///
/// At the end of the valid Unicode code points space a lot of code points are
/// either reserved or a noncharacter. Adding all these entries to the
/// lookup table would greatly increase the size of the table. Instead these
/// entries are manually processed. In this large area of reserved code points,
/// there is a small area of extended graphemes that should not be escaped
/// unconditionally. This is also manually coded. See the generation script for
/// more details.

///
/// \\pre The code point is a valid Unicode code point.
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr bool __needs_escape(const char32_t __code_point) noexcept {{

  // The entries in the gap at the end.
  if(__code_point >= 0x{gap_lower:08x} && __code_point <= 0x{gap_upper:08x})
     return false;

  // The entries at the end.
  if (__code_point >= 0x{unallocated:08x})
    return true;

  ptrdiff_t __i = std::ranges::upper_bound(__entries, (__code_point << 14) | 0x3fffu) - __entries;
  if (__i == 0)
    return false;

  --__i;
  uint32_t __upper_bound = (__entries[__i] >> 14) + (__entries[__i] & 0x3fffu);
  return __code_point <= __upper_bound;
}}
"""

TABLES_HPP_TEMPLATE = """
// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// WARNING, this entire header is generated by
// utils/generate_escaped_output_table.py
// DO NOT MODIFY!

// UNICODE, INC. LICENSE AGREEMENT - DATA FILES AND SOFTWARE
//
// See Terms of Use <https://www.unicode.org/copyright.html>
// for definitions of Unicode Inc.'s Data Files and Software.
//
// NOTICE TO USER: Carefully read the following legal agreement.
// BY DOWNLOADING, INSTALLING, COPYING OR OTHERWISE USING UNICODE INC.'S
// DATA FILES ("DATA FILES"), AND/OR SOFTWARE ("SOFTWARE"),
// YOU UNEQUIVOCALLY ACCEPT, AND AGREE TO BE BOUND BY, ALL OF THE
// TERMS AND CONDITIONS OF THIS AGREEMENT.
// IF YOU DO NOT AGREE, DO NOT DOWNLOAD, INSTALL, COPY, DISTRIBUTE OR USE
// THE DATA FILES OR SOFTWARE.
//
// COPYRIGHT AND PERMISSION NOTICE
//
// Copyright (c) 1991-2022 Unicode, Inc. All rights reserved.
// Distributed under the Terms of Use in https://www.unicode.org/copyright.html.
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of the Unicode data files and any associated documentation
// (the "Data Files") or Unicode software and any associated documentation
// (the "Software") to deal in the Data Files or Software
// without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, and/or sell copies of
// the Data Files or Software, and to permit persons to whom the Data Files
// or Software are furnished to do so, provided that either
// (a) this copyright and permission notice appear with all copies
// of the Data Files or Software, or
// (b) this copyright and permission notice appear in associated
// Documentation.
//
// THE DATA FILES AND SOFTWARE ARE PROVIDED "AS IS", WITHOUT WARRANTY OF
// ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
// WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT OF THIRD PARTY RIGHTS.
// IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS INCLUDED IN THIS
// NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL
// DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
// DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
// TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
// PERFORMANCE OF THE DATA FILES OR SOFTWARE.
//
// Except as contained in this notice, the name of a copyright holder
// shall not be used in advertising or otherwise to promote the sale,
// use or other dealings in these Data Files or Software without prior
// written authorization of the copyright holder.

#ifndef _LIBCPP___FORMAT_ESCAPED_OUTPUT_TABLE_H
#define _LIBCPP___FORMAT_ESCAPED_OUTPUT_TABLE_H

#include <__algorithm/ranges_upper_bound.h>
#include <__config>
#include <cstddef>
#include <cstdint>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 23

namespace __escaped_output_table {{
// clang-format off
{content}
// clang-format on
}} // namespace __escaped_output_table

#endif //_LIBCPP_STD_VER >= 23

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FORMAT_ESCAPED_OUTPUT_TABLE_H"""


def property_ranges_to_table(ranges: list[PropertyRange]) -> list[Entry]:
    result = list[Entry]()
    high = -1
    for range in sorted(ranges, key=lambda x: x.lower):
        # Validate overlapping ranges
        assert range.lower > high
        high = range.upper

        while True:
            e = Entry(range.lower, range.upper - range.lower)
            if e.offset <= 16383:
                result.append(e)
                break
            e.offset = 16383
            result.append(e)
            range.lower += 16384
    return result


cpp_entrytemplate = "    0x{:08x} /* {:08x} - {:08x} [{:>5}] */"


def generate_cpp_data(
    ranges: list[PropertyRange], unallocated: int, gap_lower: int, gap_upper: int
) -> str:
    result = StringIO()
    table = property_ranges_to_table(ranges)
    # Validates all entries fit in 18 bits.
    for x in table:
        assert x.lower + x.offset < 0x3FFFF
    result.write(
        DATA_ARRAY_TEMPLATE.format(
            size=len(table),
            entries=",\n".join(
                [
                    cpp_entrytemplate.format(
                        x.lower << 14 | x.offset,
                        x.lower,
                        x.lower + x.offset,
                        x.offset + 1,
                    )
                    for x in table
                ]
            ),
            unallocated=unallocated,
            gap_lower=gap_lower,
            gap_upper=gap_upper,
        )
    )

    return result.getvalue()


def generate_data_tables() -> str:
    """
    Generate Unicode data for [format.string.escaped]/2.2.1.2.1
    """
    derived_general_catagory_path = (
        Path(__file__).absolute().parent
        / "data"
        / "unicode"
        / "DerivedGeneralCategory.txt"
    )

    properties = list()
    with derived_general_catagory_path.open(encoding="utf-8") as f:
        properties.extend(
            list(
                filter(
                    filterGeneralProperty,
                    [x for line in f if (x := parsePropertyLine(line))],
                )
            )
        )

    data = compactPropertyRanges(sorted(properties, key=lambda x: x.lower))

    # The output table has two large entries at the end, with a small "gap"
    #   E0100..E01EF  ; Grapheme_Extend # Mn [240] VARIATION SELECTOR-17..VARIATION SELECTOR-256
    # Based on Unicode 15.1.0:
    # - Encoding all these entries in the table requires 1173 entries.
    # - Manually handling these last two blocks reduces the size to 729 entries.
    # This not only reduces the binary size, but also improves the performance
    # by having fewer elements to search.
    # The exact entries may differ between Unicode versions. When these numbers
    # change the test needs to be updated too.
    #   libcxx/test/libcxx/utilities/format/format.string/format.string.std/escaped_output.pass.cpp
    assert (data[-2].lower) == 0x323B0
    assert (data[-2].upper) == 0xE00FF
    assert (data[-1].lower) == 0xE01F0
    assert (data[-1].upper) == 0x10FFFF

    return "\n".join(
        [
            generate_cpp_data(
                data[:-2], data[-2].lower, data[-2].upper + 1, data[-1].lower - 1
            )
        ]
    )


if __name__ == "__main__":
    if len(sys.argv) == 2:
        sys.stdout = open(sys.argv[1], "w")
    print(TABLES_HPP_TEMPLATE.lstrip().format(content=generate_data_tables()))
