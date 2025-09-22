# -*- coding: utf-8 -*-
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import os
import sys

this_dir = os.path.dirname(os.path.realpath(__file__))
sys.path.append(os.path.join(os.path.dirname(this_dir), "lib"))

import unittest

import tests.unit
import tests.functional.cases


def suite():
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()
    suite.addTests(loader.loadTestsFromModule(tests.unit))
    suite.addTests(loader.loadTestsFromModule(tests.functional.cases))
    return suite
