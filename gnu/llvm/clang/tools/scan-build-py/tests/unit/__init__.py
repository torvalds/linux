# -*- coding: utf-8 -*-
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

from . import test_libear
from . import test_compilation
from . import test_clang
from . import test_report
from . import test_analyze
from . import test_intercept
from . import test_shell


def load_tests(loader, suite, _):
    suite.addTests(loader.loadTestsFromModule(test_libear))
    suite.addTests(loader.loadTestsFromModule(test_compilation))
    suite.addTests(loader.loadTestsFromModule(test_clang))
    suite.addTests(loader.loadTestsFromModule(test_report))
    suite.addTests(loader.loadTestsFromModule(test_analyze))
    suite.addTests(loader.loadTestsFromModule(test_intercept))
    suite.addTests(loader.loadTestsFromModule(test_shell))
    return suite
