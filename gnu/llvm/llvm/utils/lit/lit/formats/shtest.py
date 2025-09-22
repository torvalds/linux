from __future__ import absolute_import

import lit.TestRunner
import lit.util

from .base import FileBasedTest


class ShTest(FileBasedTest):
    """ShTest is a format with one file per test.

    This is the primary format for regression tests as described in the LLVM
    testing guide:

        http://llvm.org/docs/TestingGuide.html

    The ShTest files contain some number of shell-like command pipelines, along
    with assertions about what should be in the output.
    """

    def __init__(
        self, execute_external=False, extra_substitutions=[], preamble_commands=[]
    ):
        self.execute_external = execute_external
        self.extra_substitutions = extra_substitutions
        self.preamble_commands = preamble_commands

    def execute(self, test, litConfig):
        return lit.TestRunner.executeShTest(
            test,
            litConfig,
            self.execute_external,
            self.extra_substitutions,
            self.preamble_commands,
        )
