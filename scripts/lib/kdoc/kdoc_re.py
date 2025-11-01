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
    Helper class to simplify regex declaration and usage,

    It calls re.compile for a given pattern. It also allows adding
    regular expressions and define sub at class init time.

    Regular expressions can be cached via an argument, helping to speedup
    searches.
    """

    def _add_regex(self, string, flags):
        """
        Adds a new regex or re-use it from the cache.
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

    def __add__(self, other):
        """
        Allows adding two regular expressions into one.
        """

        return KernRe(str(self) + str(other), cache=self.cache or other.cache,
                  flags=self.regex.flags | other.regex.flags)

    def match(self, string):
        """
        Handles a re.match storing its results
        """

        self.last_match = self.regex.match(string)
        return self.last_match

    def search(self, string):
        """
        Handles a re.search storing its results
        """

        self.last_match = self.regex.search(string)
        return self.last_match

    def findall(self, string):
        """
        Alias to re.findall
        """

        return self.regex.findall(string)

    def split(self, string):
        """
        Alias to re.split
        """

        return self.regex.split(string)

    def sub(self, sub, string, count=0):
        """
        Alias to re.sub
        """

        return self.regex.sub(sub, string, count=count)

    def group(self, num):
        """
        Returns the group results of the last match
        """

        return self.last_match.group(num)


class NestedMatch:
    """
    Finding nested delimiters is hard with regular expressions. It is
    even harder on Python with its normal re module, as there are several
    advanced regular expressions that are missing.

    This is the case of this pattern:

            '\\bSTRUCT_GROUP(\\(((?:(?>[^)(]+)|(?1))*)\\))[^;]*;'

    which is used to properly match open/close parenthesis of the
    string search STRUCT_GROUP(),

    Add a class that counts pairs of delimiters, using it to match and
    replace nested expressions.

    The original approach was suggested by:
        https://stackoverflow.com/questions/5454322/python-how-to-match-nested-parentheses-with-regex

    Although I re-implemented it to make it more generic and match 3 types
    of delimiters. The logic checks if delimiters are paired. If not, it
    will ignore the search string.
    """

    # TODO: make NestedMatch handle multiple match groups
    #
    # Right now, regular expressions to match it are defined only up to
    #       the start delimiter, e.g.:
    #
    #       \bSTRUCT_GROUP\(
    #
    # is similar to: STRUCT_GROUP\((.*)\)
    # except that the content inside the match group is delimiter's aligned.
    #
    # The content inside parenthesis are converted into a single replace
    # group (e.g. r`\1').
    #
    # It would be nice to change such definition to support multiple
    # match groups, allowing a regex equivalent to.
    #
    #   FOO\((.*), (.*), (.*)\)
    #
    # it is probably easier to define it not as a regular expression, but
    # with some lexical definition like:
    #
    #   FOO(arg1, arg2, arg3)

    DELIMITER_PAIRS = {
        '{': '}',
        '(': ')',
        '[': ']',
    }

    RE_DELIM = re.compile(r'[\{\}\[\]\(\)]')

    def _search(self, regex, line):
        """
        Finds paired blocks for a regex that ends with a delimiter.

        The suggestion of using finditer to match pairs came from:
        https://stackoverflow.com/questions/5454322/python-how-to-match-nested-parentheses-with-regex
        but I ended using a different implementation to align all three types
        of delimiters and seek for an initial regular expression.

        The algorithm seeks for open/close paired delimiters and place them
        into a stack, yielding a start/stop position of each match  when the
        stack is zeroed.

        The algorithm shoud work fine for properly paired lines, but will
        silently ignore end delimiters that preceeds an start delimiter.
        This should be OK for kernel-doc parser, as unaligned delimiters
        would cause compilation errors. So, we don't need to rise exceptions
        to cover such issues.
        """

        stack = []

        for match_re in regex.finditer(line):
            start = match_re.start()
            offset = match_re.end()

            d = line[offset - 1]
            if d not in self.DELIMITER_PAIRS:
                continue

            end = self.DELIMITER_PAIRS[d]
            stack.append(end)

            for match in self.RE_DELIM.finditer(line[offset:]):
                pos = match.start() + offset

                d = line[pos]

                if d in self.DELIMITER_PAIRS:
                    end = self.DELIMITER_PAIRS[d]

                    stack.append(end)
                    continue

                # Does the end delimiter match what it is expected?
                if stack and d == stack[-1]:
                    stack.pop()

                    if not stack:
                        yield start, offset, pos + 1
                        break

    def search(self, regex, line):
        """
        This is similar to re.search:

        It matches a regex that it is followed by a delimiter,
        returning occurrences only if all delimiters are paired.
        """

        for t in self._search(regex, line):

            yield line[t[0]:t[2]]

    def sub(self, regex, sub, line, count=0):
        """
        This is similar to re.sub:

        It matches a regex that it is followed by a delimiter,
        replacing occurrences only if all delimiters are paired.

        if r'\1' is used, it works just like re: it places there the
        matched paired data with the delimiter stripped.

        If count is different than zero, it will replace at most count
        items.
        """
        out = ""

        cur_pos = 0
        n = 0

        for start, end, pos in self._search(regex, line):
            out += line[cur_pos:start]

            # Value, ignoring start/end delimiters
            value = line[end:pos - 1]

            # replaces \1 at the sub string, if \1 is used there
            new_sub = sub
            new_sub = new_sub.replace(r'\1', value)

            out += new_sub

            # Drop end ';' if any
            if line[pos] == ';':
                pos += 1

            cur_pos = pos
            n += 1

            if count and count >= n:
                break

        # Append the remaining string
        l = len(line)
        out += line[cur_pos:l]

        return out
