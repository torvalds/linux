# -*- coding: utf-8 -*-
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import libscanbuild.compilation as sut
import unittest


class CompilerTest(unittest.TestCase):
    def test_is_compiler_call(self):
        self.assertIsNotNone(sut.compiler_language(["clang"]))
        self.assertIsNotNone(sut.compiler_language(["clang-3.6"]))
        self.assertIsNotNone(sut.compiler_language(["clang++"]))
        self.assertIsNotNone(sut.compiler_language(["clang++-3.5.1"]))
        self.assertIsNotNone(sut.compiler_language(["cc"]))
        self.assertIsNotNone(sut.compiler_language(["c++"]))
        self.assertIsNotNone(sut.compiler_language(["gcc"]))
        self.assertIsNotNone(sut.compiler_language(["g++"]))
        self.assertIsNotNone(sut.compiler_language(["/usr/local/bin/gcc"]))
        self.assertIsNotNone(sut.compiler_language(["/usr/local/bin/g++"]))
        self.assertIsNotNone(sut.compiler_language(["/usr/local/bin/clang"]))
        self.assertIsNotNone(sut.compiler_language(["armv7_neno-linux-gnueabi-g++"]))

        self.assertIsNone(sut.compiler_language([]))
        self.assertIsNone(sut.compiler_language([""]))
        self.assertIsNone(sut.compiler_language(["ld"]))
        self.assertIsNone(sut.compiler_language(["as"]))
        self.assertIsNone(sut.compiler_language(["/usr/local/bin/compiler"]))


class SplitTest(unittest.TestCase):
    def test_detect_cxx_from_compiler_name(self):
        def test(cmd):
            result = sut.split_command([cmd, "-c", "src.c"])
            self.assertIsNotNone(result, "wrong input for test")
            return result.compiler == "c++"

        self.assertFalse(test("cc"))
        self.assertFalse(test("gcc"))
        self.assertFalse(test("clang"))

        self.assertTrue(test("c++"))
        self.assertTrue(test("g++"))
        self.assertTrue(test("g++-5.3.1"))
        self.assertTrue(test("clang++"))
        self.assertTrue(test("clang++-3.7.1"))
        self.assertTrue(test("armv7_neno-linux-gnueabi-g++"))

    def test_action(self):
        self.assertIsNotNone(sut.split_command(["clang", "source.c"]))
        self.assertIsNotNone(sut.split_command(["clang", "-c", "source.c"]))
        self.assertIsNotNone(
            sut.split_command(["clang", "-c", "source.c", "-MF", "a.d"])
        )

        self.assertIsNone(sut.split_command(["clang", "-E", "source.c"]))
        self.assertIsNone(sut.split_command(["clang", "-c", "-E", "source.c"]))
        self.assertIsNone(sut.split_command(["clang", "-c", "-M", "source.c"]))
        self.assertIsNone(sut.split_command(["clang", "-c", "-MM", "source.c"]))

    def test_source_file(self):
        def test(expected, cmd):
            self.assertEqual(expected, sut.split_command(cmd).files)

        test(["src.c"], ["clang", "src.c"])
        test(["src.c"], ["clang", "-c", "src.c"])
        test(["src.C"], ["clang", "-x", "c", "src.C"])
        test(["src.cpp"], ["clang++", "-c", "src.cpp"])
        test(["s1.c", "s2.c"], ["clang", "-c", "s1.c", "s2.c"])
        test(["s1.c", "s2.c"], ["cc", "s1.c", "s2.c", "-ldep", "-o", "a.out"])
        test(["src.c"], ["clang", "-c", "-I", "./include", "src.c"])
        test(["src.c"], ["clang", "-c", "-I", "/opt/me/include", "src.c"])
        test(["src.c"], ["clang", "-c", "-D", "config=file.c", "src.c"])

        self.assertIsNone(sut.split_command(["cc", "this.o", "that.o", "-o", "a.out"]))
        self.assertIsNone(sut.split_command(["cc", "this.o", "-lthat", "-o", "a.out"]))

    def test_filter_flags(self):
        def test(expected, flags):
            command = ["clang", "-c", "src.c"] + flags
            self.assertEqual(expected, sut.split_command(command).flags)

        def same(expected):
            test(expected, expected)

        def filtered(flags):
            test([], flags)

        same([])
        same(["-I", "/opt/me/include", "-DNDEBUG", "-ULIMITS"])
        same(["-O", "-O2"])
        same(["-m32", "-mmms"])
        same(["-Wall", "-Wno-unused", "-g", "-funroll-loops"])

        filtered([])
        filtered(["-lclien", "-L/opt/me/lib", "-L", "/opt/you/lib"])
        filtered(["-static"])
        filtered(["-MD", "-MT", "something"])
        filtered(["-MMD", "-MF", "something"])


class SourceClassifierTest(unittest.TestCase):
    def test_sources(self):
        self.assertIsNone(sut.classify_source("file.o"))
        self.assertIsNone(sut.classify_source("file.exe"))
        self.assertIsNone(sut.classify_source("/path/file.o"))
        self.assertIsNone(sut.classify_source("clang"))

        self.assertEqual("c", sut.classify_source("file.c"))
        self.assertEqual("c", sut.classify_source("./file.c"))
        self.assertEqual("c", sut.classify_source("/path/file.c"))
        self.assertEqual("c++", sut.classify_source("file.c", False))
        self.assertEqual("c++", sut.classify_source("./file.c", False))
        self.assertEqual("c++", sut.classify_source("/path/file.c", False))
