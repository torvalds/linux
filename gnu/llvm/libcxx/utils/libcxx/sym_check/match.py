# -*- Python -*- vim: set syntax=python tabstop=4 expandtab cc=80:
# ===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===##
"""
match - A set of functions for matching symbols in a list to a list of regexs
"""

import re


def find_and_report_matching(symbol_list, regex_list):
    report = ""
    found_count = 0
    for regex_str in regex_list:
        report += 'Matching regex "%s":\n' % regex_str
        matching_list = find_matching_symbols(symbol_list, regex_str)
        if not matching_list:
            report += "    No matches found\n\n"
            continue
        # else
        found_count += len(matching_list)
        for m in matching_list:
            report += "    MATCHES: %s\n" % m["name"]
        report += "\n"
    return found_count, report


def find_matching_symbols(symbol_list, regex_str):
    regex = re.compile(regex_str)
    matching_list = []
    for s in symbol_list:
        if regex.match(s["name"]):
            matching_list += [s]
    return matching_list
