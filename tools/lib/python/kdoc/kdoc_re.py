#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# Copyright(c) 2025: Mauro Carvalho Chehab <mchehab@kernel.org>.

"""
Regular expression ancillary classes.

Those help caching regular expressions and do matching for kernel-doc.
"""

import re

# Local cache for regular expressions
re_cache = {}


class KernRe:
    """
    Helper class to simplify regex declaration and usage.

    It calls re.compile for a given pattern. It also allows adding
    regular expressions and define sub at class init time.

    Regular expressions can be cached via an argument, helping to speedup
    searches.
    """

    def _add_regex(self, string, flags):
        """
        Adds a new regex or reuses it from the cache.
        """
        self.regex = re_cache.get(string, None)
        if not self.regex:
            self.regex = re.compile(string, flags=flags)
            if self.cache:
                re_cache[string] = self.regex

    def __init__(self, string, cache=True, flags=0):
        """
        Compile a regular expression and initialize internal vars.
        """

        self.cache = cache
        self.last_match = None

        self._add_regex(string, flags)

    def __str__(self):
        """
        Return the regular expression pattern.
        """
        return self.regex.pattern

    def __repr__(self):
        """
        Returns a displayable version of the class init.
        """

        flag_map = {
            re.IGNORECASE: "re.I",
            re.MULTILINE: "re.M",
            re.DOTALL: "re.S",
            re.VERBOSE: "re.X",
        }

        flags = []
        for flag, name in flag_map.items():
            if self.regex.flags & flag:
                flags.append(name)

        flags_name = " | ".join(flags)

        max_len = 60
        pattern = ""
        for pos in range(0, len(self.regex.pattern), max_len):
            pattern += '"' + self.regex.pattern[pos:max_len + pos] + '" '

        if flags_name:
            return f'KernRe({pattern}, {flags_name})'
        else:
            return f'KernRe({pattern})'

    def __add__(self, other):
        """
        Allows adding two regular expressions into one.
        """

        return KernRe(str(self) + str(other), cache=self.cache or other.cache,
                  flags=self.regex.flags | other.regex.flags)

    def match(self, string):
        """
        Handles a re.match storing its results.
        """

        self.last_match = self.regex.match(string)
        return self.last_match

    def search(self, string):
        """
        Handles a re.search storing its results.
        """

        self.last_match = self.regex.search(string)
        return self.last_match

    def finditer(self,  string):
        """
        Alias to re.finditer.
        """

        return self.regex.finditer(string)

    def findall(self, string):
        """
        Alias to re.findall.
        """

        return self.regex.findall(string)

    def split(self, string):
        """
        Alias to re.split.
        """

        return self.regex.split(string)

    def sub(self, sub, string, count=0):
        """
        Alias to re.sub.
        """

        return self.regex.sub(sub, string, count=count)

    def group(self, num):
        """
        Returns the group results of the last match.
        """

        return self.last_match.group(num)

    def groups(self):
        """
        Returns the group results of the last match
        """

        return self.last_match.groups()
