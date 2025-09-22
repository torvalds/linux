# -*- coding: utf-8 -*-
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import libscanbuild.shell as sut
import unittest


class ShellTest(unittest.TestCase):
    def test_encode_decode_are_same(self):
        def test(value):
            self.assertEqual(sut.encode(sut.decode(value)), value)

        test("")
        test("clang")
        test("clang this and that")

    def test_decode_encode_are_same(self):
        def test(value):
            self.assertEqual(sut.decode(sut.encode(value)), value)

        test([])
        test(["clang"])
        test(["clang", "this", "and", "that"])
        test(["clang", "this and", "that"])
        test(["clang", "it's me", "again"])
        test(["clang", 'some "words" are', "quoted"])

    def test_encode(self):
        self.assertEqual(
            sut.encode(["clang", "it's me", "again"]), 'clang "it\'s me" again'
        )
        self.assertEqual(
            sut.encode(["clang", "it(s me", "again)"]), 'clang "it(s me" "again)"'
        )
        self.assertEqual(
            sut.encode(["clang", "redirect > it"]), 'clang "redirect > it"'
        )
        self.assertEqual(
            sut.encode(["clang", '-DKEY="VALUE"']), 'clang -DKEY=\\"VALUE\\"'
        )
        self.assertEqual(
            sut.encode(["clang", '-DKEY="value with spaces"']),
            'clang -DKEY=\\"value with spaces\\"',
        )
