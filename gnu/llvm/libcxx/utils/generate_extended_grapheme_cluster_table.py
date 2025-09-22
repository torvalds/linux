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
    prop: int = -1


LINE_REGEX = re.compile(
    r"^(?P<lower>[0-9A-F]{4,5})(?:\.\.(?P<upper>[0-9A-F]{4,5}))?\s*;\s*(?P<prop>\w+)"
)


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
    Merges consecutive ranges with the same property to one range.

    Merging the ranges results in fewer ranges in the output table,
    reducing binary and improving lookup performance.
    """
    result = list()
    for x in input:
        if (
            len(result)
            and result[-1].prop == x.prop
            and result[-1].upper + 1 == x.lower
        ):
            result[-1].upper = x.upper
            continue
        result.append(x)
    return result


PROP_VALUE_ENUMERATOR_TEMPLATE = "  __{}"
PROP_VALUE_ENUM_TEMPLATE = """
enum class __property : uint8_t {{
  // Values generated from the data files.
{enumerators},

  // The properies below aren't stored in the "database".

  // Text position properties.
  __sot,
  __eot,

  // The code unit has none of above properties.
  __none
}};
"""

DATA_ARRAY_TEMPLATE = """
/// The entries of the extended grapheme cluster bondary property table.
///
/// The data is generated from
/// - https://www.unicode.org/Public/UCD/latest/ucd/auxiliary/GraphemeBreakProperty.txt
/// - https://www.unicode.org/Public/UCD/latest/ucd/emoji/emoji-data.txt
///
/// The data has 3 values
/// - bits [0, 3] The property. One of the values generated from the datafiles
///   of \\ref __property
/// - bits [4, 10] The size of the range.
/// - bits [11, 31] The lower bound code point of the range. The upper bound of
///   the range is lower bound + size.
///
/// The 7 bits for the size allow a maximum range of 128 elements. Some ranges
/// in the Unicode tables are larger. They are stored in multiple consecutive
/// ranges in the data table. An alternative would be to store the sizes in a
/// separate 16-bit value. The original MSVC STL code had such an approach, but
/// this approach uses less space for the data and is about 4% faster in the
/// following benchmark.
/// libcxx/benchmarks/std_format_spec_string_unicode.bench.cpp
// clang-format off
_LIBCPP_HIDE_FROM_ABI inline constexpr uint32_t __entries[{size}] = {{
{entries}}};
// clang-format on

/// Returns the extended grapheme cluster bondary property of a code point.
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr __property __get_property(const char32_t __code_point) noexcept {{
  // The algorithm searches for the upper bound of the range and, when found,
  // steps back one entry. This algorithm is used since the code point can be
  // anywhere in the range. After a lower bound is found the next step is to
  // compare whether the code unit is indeed in the range.
  //
  // Since the entry contains a code unit, size, and property the code point
  // being sought needs to be adjusted. Just shifting the code point to the
  // proper position doesn't work; suppose an entry has property 0, size 1,
  // and lower bound 3. This results in the entry 0x1810.
  // When searching for code point 3 it will search for 0x1800, find 0x1810
  // and moves to the previous entry. Thus the lower bound value will never
  // be found.
  // The simple solution is to set the bits belonging to the property and
  // size. Then the upper bound for code point 3 will return the entry after
  // 0x1810. After moving to the previous entry the algorithm arrives at the
  // correct entry.
  ptrdiff_t __i = std::ranges::upper_bound(__entries, (__code_point << 11) | 0x7ffu) - __entries;
  if (__i == 0)
    return __property::__none;

  --__i;
  uint32_t __upper_bound = (__entries[__i] >> 11) + ((__entries[__i] >> 4) & 0x7f);
  if (__code_point <= __upper_bound)
    return static_cast<__property>(__entries[__i] & 0xf);

  return __property::__none;
}}
"""

MSVC_FORMAT_UCD_TABLES_HPP_TEMPLATE = """
// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// WARNING, this entire header is generated by
// utils/generate_extended_grapheme_cluster_table.py
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

#ifndef _LIBCPP___FORMAT_EXTENDED_GRAPHEME_CLUSTER_TABLE_H
#define _LIBCPP___FORMAT_EXTENDED_GRAPHEME_CLUSTER_TABLE_H

#include <__algorithm/ranges_upper_bound.h>
#include <__config>
#include <__iterator/access.h>
#include <cstddef>
#include <cstdint>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

namespace __extended_grapheme_custer_property_boundary {{
{content}
}} // namespace __extended_grapheme_custer_property_boundary

#endif //_LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FORMAT_EXTENDED_GRAPHEME_CLUSTER_TABLE_H"""


def property_ranges_to_table(
    ranges: list[PropertyRange], props: list[str]
) -> list[Entry]:
    assert len(props) < 16
    result = list[Entry]()
    high = -1
    for range in sorted(ranges, key=lambda x: x.lower):
        # Validate overlapping ranges
        assert range.lower > high
        high = range.upper

        while True:
            e = Entry(range.lower, range.upper - range.lower, props.index(range.prop))
            if e.offset <= 127:
                result.append(e)
                break
            e.offset = 127
            result.append(e)
            range.lower += 128
    return result


cpp_entrytemplate = "    0x{:08x}"


def generate_cpp_data(prop_name: str, ranges: list[PropertyRange]) -> str:
    result = StringIO()
    prop_values = sorted(set(x.prop for x in ranges))
    table = property_ranges_to_table(ranges, prop_values)
    enumerator_values = [PROP_VALUE_ENUMERATOR_TEMPLATE.format(x) for x in prop_values]
    result.write(
        PROP_VALUE_ENUM_TEMPLATE.format(enumerators=",\n".join(enumerator_values))
    )
    result.write(
        DATA_ARRAY_TEMPLATE.format(
            prop_name=prop_name,
            size=len(table),
            entries=",\n".join(
                [
                    cpp_entrytemplate.format(x.lower << 11 | x.offset << 4 | x.prop)
                    for x in table
                ]
            ),
        )
    )

    return result.getvalue()


def generate_data_tables() -> str:
    """
    Generate Unicode data for inclusion into <format> from
    - https://www.unicode.org/Public/UCD/latest/ucd/auxiliary/GraphemeBreakProperty.txt
    - https://www.unicode.org/Public/UCD/latest/ucd/emoji/emoji-data.txt
    - https://www.unicode.org/Public/UCD/latest/ucd/DerivedCoreProperties.txt

    These files are expected to be stored in the same directory as this script.
    """
    root = Path(__file__).absolute().parent / "data" / "unicode"
    gbp_data_path = root / "GraphemeBreakProperty.txt"
    emoji_data_path = root / "emoji-data.txt"

    gbp_ranges = list()
    emoji_ranges = list()
    with gbp_data_path.open(encoding="utf-8") as f:
        gbp_ranges = compactPropertyRanges(
            [x for line in f if (x := parsePropertyLine(line))]
        )
    with emoji_data_path.open(encoding="utf-8") as f:
        emoji_ranges = compactPropertyRanges(
            [x for line in f if (x := parsePropertyLine(line))]
        )

    [gbp_ranges.append(x) for x in emoji_ranges if x.prop == "Extended_Pictographic"]
    gpb_cpp_data = generate_cpp_data("Grapheme_Break", gbp_ranges)
    return "\n".join([gpb_cpp_data])


if __name__ == "__main__":
    if len(sys.argv) == 2:
        sys.stdout = open(sys.argv[1], "w")
    print(
        MSVC_FORMAT_UCD_TABLES_HPP_TEMPLATE.lstrip().format(
            content=generate_data_tables()
        )
    )
