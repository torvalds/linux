# -*- coding: utf-8 -*-
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import libear as sut
import unittest
import os.path


class TemporaryDirectoryTest(unittest.TestCase):
    def test_creates_directory(self):
        dirname = None
        with sut.TemporaryDirectory() as tmpdir:
            self.assertTrue(os.path.isdir(tmpdir))
            dirname = tmpdir
        self.assertIsNotNone(dirname)
        self.assertFalse(os.path.exists(dirname))

    def test_removes_directory_when_exception(self):
        dirname = None
        try:
            with sut.TemporaryDirectory() as tmpdir:
                self.assertTrue(os.path.isdir(tmpdir))
                dirname = tmpdir
                raise RuntimeError("message")
        except:
            self.assertIsNotNone(dirname)
            self.assertFalse(os.path.exists(dirname))
