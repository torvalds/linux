# ===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===##
import unittest

import filecheck_lint as fcl


class TestParser(unittest.TestCase):
    def test_parse_all_additional_prefixes(self):
        def run(content, expected_prefixes):
            prefixes = set(fcl.parse_custom_prefixes(content))
            for prefix in expected_prefixes:
                self.assertIn(prefix, prefixes)

        for content, expected_prefixes in [
            ("-check-prefix=PREFIX", {"PREFIX"}),
            ("-check-prefix='PREFIX'", {"PREFIX"}),
            ('-check-prefix="PREFIX"', {"PREFIX"}),
            ("-check-prefix PREFIX", {"PREFIX"}),
            ("-check-prefix      PREFIX", {"PREFIX"}),
            ("-check-prefixes=PREFIX1,PREFIX2", {"PREFIX1", "PREFIX2"}),
            ("-check-prefixes PREFIX1,PREFIX2", {"PREFIX1", "PREFIX2"}),
            (
                """-check-prefix=PREFIX1 -check-prefix PREFIX2
            -check-prefixes=PREFIX3,PREFIX4 -check-prefix=PREFIX5
            -check-prefixes PREFIX6,PREFIX7 -check-prefixes=PREFIX8',
         """,  # pylint: disable=bad-continuation
                {f"PREFIX{i}" for i in range(1, 9)},
            ),
        ]:
            run(content, expected_prefixes)

    def test_additional_prefixes_uniquely(self):
        lines = ["--check-prefix=SOME-PREFIX", "--check-prefix=SOME-PREFIX"]
        prefixes = set(fcl.parse_custom_prefixes("\n".join(lines)))
        assert len(prefixes) == 1


class TestTypoDetection(unittest.TestCase):
    def test_find_potential_directives_comment_prefix(self):
        lines = ["junk; CHCK1:", "junk// CHCK2:", "SOME CHCK3:"]
        content = "\n".join(lines)

        results = list(fcl.find_potential_directives(content))
        assert len(results) == 3
        pos, match = results[0]
        assert pos.as_str() == "1:7-11"
        assert match == "CHCK1"

        pos, match = results[1]
        assert pos.as_str() == "2:8-12"
        assert match == "CHCK2"

        pos, match = results[2]
        assert pos.as_str() == "3:1-10"
        assert match == "SOME CHCK3"

    def test_levenshtein(self):
        for s1, s2, distance in [
            ("Levenshtein", "Levenstin", 2),  # 2 insertions
            ("Levenshtein", "Levenstherin", 3),  # 1 insertion, 2 deletions
            ("Levenshtein", "Lenvinshtein", 2),  # 1 deletion, 1 substitution
            ("Levenshtein", "Levenshtein", 0),  # identical strings
        ]:
            assert fcl.levenshtein(s1, s2) == distance
