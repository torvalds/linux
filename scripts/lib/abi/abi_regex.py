#!/usr/bin/env python3
# xxpylint: disable=R0903
# Copyright(c) 2025: Mauro Carvalho Chehab <mchehab@kernel.org>.
# SPDX-License-Identifier: GPL-2.0

"""
Convert ABI what into regular expressions
"""

import re
import sys

from pprint import pformat

from abi_parser import AbiParser
from helpers import AbiDebug

class AbiRegex(AbiParser):
    """Extends AbiParser to search ABI nodes with regular expressions"""

    # Escape only ASCII visible characters
    escape_symbols = r"([\x21-\x29\x2b-\x2d\x3a-\x40\x5c\x60\x7b-\x7e])"
    leave_others = "others"

    # Tuples with regular expressions to be compiled and replacement data
    re_whats = [
        # Drop escape characters that might exist
        (re.compile("\\\\"), ""),

        # Temporarily escape dot characters
        (re.compile(r"\."),  "\xf6"),

        # Temporarily change [0-9]+ type of patterns
        (re.compile(r"\[0\-9\]\+"),  "\xff"),

        # Temporarily change [\d+-\d+] type of patterns
        (re.compile(r"\[0\-\d+\]"),  "\xff"),
        (re.compile(r"\[0:\d+\]"),  "\xff"),
        (re.compile(r"\[(\d+)\]"),  "\xf4\\\\d+\xf5"),

        # Temporarily change [0-9] type of patterns
        (re.compile(r"\[(\d)\-(\d)\]"),  "\xf4\1-\2\xf5"),

        # Handle multiple option patterns
        (re.compile(r"[\{\<\[]([\w_]+)(?:[,|]+([\w_]+)){1,}[\}\>\]]"), r"(\1|\2)"),

        # Handle wildcards
        (re.compile(r"([^\/])\*"), "\\1\\\\w\xf7"),
        (re.compile(r"/\*/"), "/.*/"),
        (re.compile(r"/\xf6\xf6\xf6"), "/.*"),
        (re.compile(r"\<[^\>]+\>"), "\\\\w\xf7"),
        (re.compile(r"\{[^\}]+\}"), "\\\\w\xf7"),
        (re.compile(r"\[[^\]]+\]"), "\\\\w\xf7"),

        (re.compile(r"XX+"), "\\\\w\xf7"),
        (re.compile(r"([^A-Z])[XYZ]([^A-Z])"), "\\1\\\\w\xf7\\2"),
        (re.compile(r"([^A-Z])[XYZ]$"), "\\1\\\\w\xf7"),
        (re.compile(r"_[AB]_"), "_\\\\w\xf7_"),

        # Recover [0-9] type of patterns
        (re.compile(r"\xf4"), "["),
        (re.compile(r"\xf5"),  "]"),

        # Remove duplicated spaces
        (re.compile(r"\s+"), r" "),

        # Special case: drop comparison as in:
        # What: foo = <something>
        # (this happens on a few IIO definitions)
        (re.compile(r"\s*\=.*$"), ""),

        # Escape all other symbols
        (re.compile(escape_symbols), r"\\\1"),
        (re.compile(r"\\\\"), r"\\"),
        (re.compile(r"\\([\[\]\(\)\|])"), r"\1"),
        (re.compile(r"(\d+)\\(-\d+)"), r"\1\2"),

        (re.compile(r"\xff"), r"\\d+"),

        # Special case: IIO ABI which a parenthesis.
        (re.compile(r"sqrt(.*)"), r"sqrt(.*)"),

        # Simplify regexes with multiple .*
        (re.compile(r"(?:\.\*){2,}"),  ""),

        # Recover dot characters
        (re.compile(r"\xf6"), "\\."),
        # Recover plus characters
        (re.compile(r"\xf7"), "+"),
    ]
    re_has_num = re.compile(r"\\d")

    # Symbol name after escape_chars that are considered a devnode basename
    re_symbol_name =  re.compile(r"(\w|\\[\.\-\:])+$")

    # List of popular group names to be skipped to minimize regex group size
    # Use AbiDebug.SUBGROUP_SIZE to detect those
    skip_names = set(["devices", "hwmon"])

    def regex_append(self, what, new):
        """
        Get a search group for a subset of regular expressions.

        As ABI may have thousands of symbols, using a for to search all
        regular expressions is at least O(n^2). When there are wildcards,
        the complexity increases substantially, eventually becoming exponential.

        To avoid spending too much time on them, use a logic to split
        them into groups. The smaller the group, the better, as it would
        mean that searches will be confined to a small number of regular
        expressions.

        The conversion to a regex subset is tricky, as we need something
        that can be easily obtained from the sysfs symbol and from the
        regular expression. So, we need to discard nodes that have
        wildcards.

        If it can't obtain a subgroup, place the regular expression inside
        a special group (self.leave_others).
        """

        search_group = None

        for search_group in reversed(new.split("/")):
            if not search_group or search_group in self.skip_names:
                continue
            if self.re_symbol_name.match(search_group):
                break

        if not search_group:
            search_group = self.leave_others

        if self.debug & AbiDebug.SUBGROUP_MAP:
            self.log.debug("%s: mapped as %s", what, search_group)

        try:
            if search_group not in self.regex_group:
                self.regex_group[search_group] = []

            self.regex_group[search_group].append(re.compile(new))
            if self.search_string:
                if what.find(self.search_string) >= 0:
                    print(f"What: {what}")
        except re.PatternError:
            self.log.warning("Ignoring '%s' as it produced an invalid regex:\n"
                             "           '%s'", what, new)

    def get_regexes(self, what):
        """
        Given an ABI devnode, return a list of all regular expressions that
        may match it, based on the sub-groups created by regex_append()
        """

        re_list = []

        patches = what.split("/")
        patches.reverse()
        patches.append(self.leave_others)

        for search_group in patches:
            if search_group in self.regex_group:
                re_list += self.regex_group[search_group]

        return re_list

    def __init__(self, *args, **kwargs):
        """
        Override init method to get verbose argument
        """

        self.regex_group = None
        self.search_string = None
        self.re_string = None

        if "search_string" in kwargs:
            self.search_string = kwargs.get("search_string")
            del kwargs["search_string"]

            if self.search_string:

                try:
                    self.re_string = re.compile(self.search_string)
                except re.PatternError as e:
                    msg = f"{self.search_string} is not a valid regular expression"
                    raise ValueError(msg) from e

        super().__init__(*args, **kwargs)

    def parse_abi(self, *args, **kwargs):

        super().parse_abi(*args, **kwargs)

        self.regex_group = {}

        print("Converting ABI What fields into regexes...", file=sys.stderr)

        for t in sorted(self.data.items(), key=lambda x: x[0]):
            v = t[1]
            if v.get("type") == "File":
                continue

            v["regex"] = []

            for what in v.get("what", []):
                if not what.startswith("/sys"):
                    continue

                new = what
                for r, s in self.re_whats:
                    try:
                        new = r.sub(s, new)
                    except re.PatternError as e:
                        # Help debugging troubles with new regexes
                        raise re.PatternError(f"{e}\nwhile re.sub('{r.pattern}', {s}, str)") from e

                v["regex"].append(new)

                if self.debug & AbiDebug.REGEX:
                    self.log.debug("%-90s <== %s", new, what)

                # Store regex into a subgroup to speedup searches
                self.regex_append(what, new)

        if self.debug & AbiDebug.SUBGROUP_DICT:
            self.log.debug("%s", pformat(self.regex_group))

        if self.debug & AbiDebug.SUBGROUP_SIZE:
            biggestd_keys = sorted(self.regex_group.keys(),
                                   key= lambda k: len(self.regex_group[k]),
                                   reverse=True)

            print("Top regex subgroups:", file=sys.stderr)
            for k in biggestd_keys[:10]:
                print(f"{k} has {len(self.regex_group[k])} elements", file=sys.stderr)
