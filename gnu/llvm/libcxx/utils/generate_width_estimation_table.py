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


def filterProperty(element: PropertyRange) -> Optional[PropertyRange]:
    ### Matches property predicate?
    if element.prop in ["W", "F"]:
        return element

    ### Matches hardcode ranges predicate?

    # Yijing Hexagram Symbols
    if element.lower >= 0x4DC0 and element.upper <= 0x4DFF:
        return element

    # Miscellaneous Symbols and Pictographs
    if element.lower >= 0x1F300 and element.upper <= 0x1F5FF:
        return element

    # Supplemental Symbols and Pictographs
    if element.lower >= 0x1F900 and element.upper <= 0x1F9FF:
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
/// The entries of the characters with an estimated width of 2.
///
/// Contains the entries for [format.string.std]/12
///  -  Any code point with the East_Asian_Width="W" or East_Asian_Width="F"
///     Derived Extracted Property as described by UAX #44
/// - U+4DC0 - U+4DFF (Yijing Hexagram Symbols)
/// - U+1F300 - U+1F5FF (Miscellaneous Symbols and Pictographs)
/// - U+1F900 - U+1F9FF (Supplemental Symbols and Pictographs)
///
/// The data is generated from
/// - https://www.unicode.org/Public/UCD/latest/ucd/EastAsianWidth.txt
/// - The "overrides" in [format.string.std]/12
///
/// The format of EastAsianWidth.txt is two fields separated by a semicolon.
/// Field 0: Unicode code point value or range of code point values
/// Field 1: East_Asian_Width property, consisting of one of the following values:
///         "A", "F", "H", "N", "Na", "W"
///  - All code points, assigned or unassigned, that are not listed
///      explicitly are given the value "N".
///  - The unassigned code points in the following blocks default to "W":
///         CJK Unified Ideographs Extension A: U+3400..U+4DBF
///         CJK Unified Ideographs:             U+4E00..U+9FFF
///         CJK Compatibility Ideographs:       U+F900..U+FAFF
///  - All undesignated code points in Planes 2 and 3, whether inside or
///      outside of allocated blocks, default to "W":
///         Plane 2:                            U+20000..U+2FFFD
///         Plane 3:                            U+30000..U+3FFFD
///
/// The table is similar to the table
///  __extended_grapheme_custer_property_boundary::__entries
/// which explains the details of these classes. The only difference is this
/// table lacks a property, thus having more bits available for the size.
///
/// The maximum code point that has an estimated width of 2 is U+3FFFD. This
/// value can be encoded in 18 bits. Thus the upper 3 bits of the code point
/// are always 0. These 3 bits are used to enlarge the offset range. This
/// optimization reduces the table in Unicode 15 from 184 to 104 entries,
/// saving 320 bytes.
///
/// The data has 2 values:
/// - bits [0, 13] The size of the range, allowing 16384 elements.
/// - bits [14, 31] The lower bound code point of the range. The upper bound of
///   the range is lower bound + size.
_LIBCPP_HIDE_FROM_ABI inline constexpr uint32_t __entries[{size}] = {{
{entries}}};

/// The upper bound entry of EastAsianWidth.txt.
///
/// Values greater than this value may have more than 18 significant bits.
/// They always have a width of 1. This property makes it possible to store
/// the table in its compact form.
inline constexpr uint32_t __table_upper_bound = 0x{upper_bound:08x};

/// Returns the estimated width of a Unicode code point.
///
/// \\pre The code point is a valid Unicode code point.
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr int __estimated_width(const char32_t __code_point) noexcept {{
  // Since __table_upper_bound contains the unshifted range do the
  // comparison without shifting.
  if (__code_point > __table_upper_bound) [[unlikely]]
    return 1;

  // When the code-point is less than the first element in the table
  // the lookup is quite expensive. Since quite some scripts are in
  // that range, it makes sense to validate that first.
  // The std_format_spec_string_unicode benchmark gives a measurable
  // improvement.
  if (__code_point < (__entries[0] >> 14))
    return 1;

  ptrdiff_t __i = std::ranges::upper_bound(__entries, (__code_point << 14) | 0x3fffu) - __entries;
  if (__i == 0)
    return 1;

  --__i;
  uint32_t __upper_bound = (__entries[__i] >> 14) + (__entries[__i] & 0x3fffu);
  return 1 + (__code_point <= __upper_bound);
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
// utils/generate_width_estimation_table.py
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

#ifndef _LIBCPP___FORMAT_WIDTH_ESTIMATION_TABLE_H
#define _LIBCPP___FORMAT_WIDTH_ESTIMATION_TABLE_H

#include <__algorithm/ranges_upper_bound.h>
#include <__config>
#include <cstddef>
#include <cstdint>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

namespace __width_estimation_table {{
{content}
}} // namespace __width_estimation_table

#endif //_LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FORMAT_WIDTH_ESTIMATION_TABLE_H"""


def property_ranges_to_table(ranges: list[PropertyRange]) -> list[Entry]:
    # The maximum value that can be encoded in the available bits in the
    # __entries table.
    upper_bound = 0x3FFFF
    # The maximum offset in an __entries entry. Larger offsets will be
    # splitted and stored in multiple entries.
    chunk = 16384
    result = list[Entry]()
    high = -1
    for range in sorted(ranges, key=lambda x: x.lower):
        # Validate overlapping ranges
        assert range.lower > high
        high = range.upper
        assert high <= upper_bound

        while True:
            e = Entry(range.lower, range.upper - range.lower)
            if e.offset < chunk:
                result.append(e)
                break
            e.offset = chunk - 1
            result.append(e)
            range.lower += chunk
    return result


cpp_entrytemplate = "    0x{:08x} /* {:08x} - {:08x} [{:>5}] */"


def generate_cpp_data(ranges: list[PropertyRange], upper_bound: int) -> str:
    result = StringIO()
    table = property_ranges_to_table(ranges)
    result.write(
        DATA_ARRAY_TEMPLATE.format(
            size=len(table),
            entries=", //\n".join(
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
            upper_bound=upper_bound,
        )
    )

    return result.getvalue()


def generate_data_tables() -> str:
    """
    Generate Unicode data for [format.string.std]/12
    """
    east_asian_width_path = (
        Path(__file__).absolute().parent / "data" / "unicode" / "EastAsianWidth.txt"
    )

    properties = list()
    with east_asian_width_path.open(encoding="utf-8") as f:
        properties.extend(
            list(
                filter(
                    filterProperty,
                    [x for line in f if (x := parsePropertyLine(line))],
                )
            )
        )
    # The range U+4DC0 - U+4DFF is neutral and should not be in the table
    # The range U+1F300 - U+1F5FF is partly in the range, for example
    #   1F300..1F320;W   # So    [33] CYCLONE..SHOOTING STAR
    #   1F321..1F32C;N   # So    [12] THERMOMETER..WIND BLOWING FACE
    #   1F32D..1F335;W   # So     [9] HOT DOG..CACTUS
    # The first and last ranges are present, but the second isn't

    # Validate the hardcode ranges are present

    # Yijing Hexagram Symbols
    for i in range(0x4DC0, 0x4DFF + 1):
        assert [x for x in properties if i >= x.lower and i <= x.upper]

    # Miscellaneous Symbols and Pictographs
    for i in range(0x1F300, 0x1F5FF + 1):
        assert [x for x in properties if i >= x.lower and i <= x.upper]

    # Miscellaneous Symbols and Pictographs
    for i in range(0x1F900, 0x1F9FF + 1):
        assert [x for x in properties if i >= x.lower and i <= x.upper]

    data = compactPropertyRanges(sorted(properties, key=lambda x: x.lower))

    return "\n".join([generate_cpp_data(data, data[-1].upper)])


if __name__ == "__main__":
    if len(sys.argv) == 2:
        sys.stdout = open(sys.argv[1], "w")
    print(TABLES_HPP_TEMPLATE.lstrip().format(content=generate_data_tables()))
