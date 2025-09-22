"""
Test mpx-table command.
"""

import os
import time
import re
import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class TestMPXTable(TestBase):
    def setUp(self):
        TestBase.setUp(self)

    @skipIf(compiler="clang")
    @skipIf(oslist=no_match(["linux"]))
    @skipIf(archs=no_match(["i386", "x86_64"]))
    @skipIf(compiler="gcc", compiler_version=["<", "5"])  # GCC version >= 5 supports
    # Intel(R) Memory Protection Extensions (Intel(R) MPX).
    def test_show_command(self):
        """Test 'mpx-table show' command"""
        self.build()

        plugin_file = os.path.join(
            configuration.lldb_libs_dir, "liblldbIntelFeatures.so"
        )
        if not os.path.isfile(plugin_file):
            self.skipTest("features plugin missing.")
        plugin_command = " "
        seq = ("plugin", "load", plugin_file)
        plugin_command = plugin_command.join(seq)
        self.runCmd(plugin_command)

        exe = os.path.join(os.getcwd(), "a.out")
        self.runCmd("file " + exe, CURRENT_EXECUTABLE_SET)

        self.b1 = line_number("main.cpp", "// Break 1.")
        self.b2 = line_number("main.cpp", "// Break 2.")
        self.b3 = line_number("main.cpp", "// Break 3.")
        self.b4 = line_number("main.cpp", "// Break 4.")
        lldbutil.run_break_set_by_file_and_line(
            self, "main.cpp", self.b1, num_expected_locations=1
        )
        lldbutil.run_break_set_by_file_and_line(
            self, "main.cpp", self.b2, num_expected_locations=1
        )
        lldbutil.run_break_set_by_file_and_line(
            self, "main.cpp", self.b3, num_expected_locations=1
        )
        lldbutil.run_break_set_by_file_and_line(
            self, "main.cpp", self.b4, num_expected_locations=1
        )
        self.runCmd("run", RUN_SUCCEEDED)

        target = self.dbg.GetSelectedTarget()
        process = target.GetProcess()

        if process.GetState() == lldb.eStateExited:
            self.skipTest("Intel(R) MPX is not supported.")
        else:
            self.expect(
                "thread backtrace",
                STOPPED_DUE_TO_BREAKPOINT,
                substrs=["stop reason = breakpoint 1."],
            )

        self.expect(
            "mpx-table show a",
            substrs=[
                "lbound = 0x",
                ", ubound = 0x",
                "(pointer value = 0x",
                ", metadata = 0x",
                ")",
            ],
            error=False,
        )

        self.expect(
            "continue",
            STOPPED_DUE_TO_BREAKPOINT,
            substrs=["stop reason = breakpoint 2."],
        )

        # Check that out of scope pointer cannot be reached.
        #
        self.expect("mpx-table show a", substrs=["Invalid pointer."], error=True)

        self.expect(
            "mpx-table show tmp",
            substrs=[
                "lbound = 0x",
                ", ubound = 0x",
                "(pointer value = 0x",
                ", metadata = 0x",
                ")",
            ],
            error=False,
        )

        self.expect(
            "continue",
            STOPPED_DUE_TO_BREAKPOINT,
            substrs=["stop reason = breakpoint 3."],
        )

        # Check that the pointer value is correctly updated.
        #
        self.expect(
            "mpx-table show tmp",
            substrs=[
                "lbound = 0x",
                ", ubound = 0x",
                "(pointer value = 0x2",
                ", metadata = 0x",
                ")",
            ],
            error=False,
        )

        self.expect(
            "continue",
            STOPPED_DUE_TO_BREAKPOINT,
            substrs=["stop reason = breakpoint 4."],
        )

        # After going back to main(), check that out of scope pointer cannot be
        # reached.
        #
        self.expect("mpx-table show tmp", substrs=["Invalid pointer."], error=True)

        self.expect(
            "mpx-table show a",
            substrs=[
                "lbound = 0x",
                ", ubound = 0x",
                "(pointer value = 0x",
                ", metadata = 0x",
                ")",
            ],
            error=False,
        )

    def test_set_command(self):
        """Test 'mpx-table set' command"""
        self.build()

        plugin_file = os.path.join(
            configuration.lldb_libs_dir, "liblldbIntelFeatures.so"
        )
        if not os.path.isfile(plugin_file):
            self.skipTest("features plugin missing.")
        plugin_command = " "
        seq = ("plugin", "load", plugin_file)
        plugin_command = plugin_command.join(seq)
        self.runCmd(plugin_command)

        exe = os.path.join(os.getcwd(), "a.out")
        self.runCmd("file " + exe, CURRENT_EXECUTABLE_SET)

        self.b1 = line_number("main.cpp", "// Break 1.")
        lldbutil.run_break_set_by_file_and_line(
            self, "main.cpp", self.b1, num_expected_locations=1
        )
        self.runCmd("run", RUN_SUCCEEDED)

        target = self.dbg.GetSelectedTarget()
        process = target.GetProcess()

        if process.GetState() == lldb.eStateExited:
            self.skipTest("Intel(R) MPX is not supported.")
        else:
            self.expect(
                "thread backtrace",
                STOPPED_DUE_TO_BREAKPOINT,
                substrs=["stop reason = breakpoint 1."],
            )

        # Check that the BT Entry doesn't already contain the test values.
        #
        self.expect(
            "mpx-table show a",
            matching=False,
            substrs=["lbound = 0xcafecafe", ", ubound = 0xbeefbeef"],
        )

        # Set the test values.
        #
        self.expect("mpx-table set a 0xcafecafe 0xbeefbeef", error=False)

        # Verify that the test values have been correctly written in the BT
        # entry.
        #
        self.expect(
            "mpx-table show a",
            substrs=["lbound = 0xcafecafe", ", ubound = 0xbeefbeef"],
            error=False,
        )
