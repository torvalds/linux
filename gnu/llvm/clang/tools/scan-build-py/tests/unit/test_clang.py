# -*- coding: utf-8 -*-
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import libear
import libscanbuild.clang as sut
import unittest
import os.path
import sys


class ClangGetVersion(unittest.TestCase):
    def test_get_version_is_not_empty(self):
        self.assertTrue(sut.get_version("clang"))

    def test_get_version_throws(self):
        with self.assertRaises(OSError):
            sut.get_version("notexists")


class ClangGetArgumentsTest(unittest.TestCase):
    def test_get_clang_arguments(self):
        with libear.TemporaryDirectory() as tmpdir:
            filename = os.path.join(tmpdir, "test.c")
            with open(filename, "w") as handle:
                handle.write("")

            result = sut.get_arguments(
                ["clang", "-c", filename, "-DNDEBUG", '-Dvar="this is it"'], tmpdir
            )

            self.assertTrue("NDEBUG" in result)
            self.assertTrue('var="this is it"' in result)

    def test_get_clang_arguments_fails(self):
        with self.assertRaises(Exception):
            sut.get_arguments(["clang", "-x", "c", "notexist.c"], ".")

    def test_get_clang_arguments_fails_badly(self):
        with self.assertRaises(OSError):
            sut.get_arguments(["notexist"], ".")


class ClangGetCheckersTest(unittest.TestCase):
    def test_get_checkers(self):
        # this test is only to see is not crashing
        result = sut.get_checkers("clang", [])
        self.assertTrue(len(result))
        # do check result types
        string_type = unicode if sys.version_info < (3,) else str
        for key, value in result.items():
            self.assertEqual(string_type, type(key))
            self.assertEqual(string_type, type(value[0]))
            self.assertEqual(bool, type(value[1]))

    def test_get_active_checkers(self):
        # this test is only to see is not crashing
        result = sut.get_active_checkers("clang", [])
        self.assertTrue(len(result))
        # do check result types
        for value in result:
            self.assertEqual(str, type(value))

    def test_is_active(self):
        test = sut.is_active(["a", "b.b", "c.c.c"])

        self.assertTrue(test("a"))
        self.assertTrue(test("a.b"))
        self.assertTrue(test("b.b"))
        self.assertTrue(test("b.b.c"))
        self.assertTrue(test("c.c.c.p"))

        self.assertFalse(test("ab"))
        self.assertFalse(test("ba"))
        self.assertFalse(test("bb"))
        self.assertFalse(test("c.c"))
        self.assertFalse(test("b"))
        self.assertFalse(test("d"))

    def test_parse_checkers(self):
        lines = [
            "OVERVIEW: Clang Static Analyzer Checkers List",
            "",
            "CHECKERS:",
            "  checker.one       Checker One description",
            "  checker.two",
            "                    Checker Two description",
        ]
        result = dict(sut.parse_checkers(lines))
        self.assertTrue("checker.one" in result)
        self.assertEqual("Checker One description", result.get("checker.one"))
        self.assertTrue("checker.two" in result)
        self.assertEqual("Checker Two description", result.get("checker.two"))


class ClangIsCtuCapableTest(unittest.TestCase):
    def test_ctu_not_found(self):
        is_ctu = sut.is_ctu_capable("not-found-clang-extdef-mapping")
        self.assertFalse(is_ctu)


class ClangGetTripleArchTest(unittest.TestCase):
    def test_arch_is_not_empty(self):
        arch = sut.get_triple_arch(["clang", "-E", "-"], ".")
        self.assertTrue(len(arch) > 0)
