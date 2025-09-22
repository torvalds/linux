# System modules
import os
import sys

# LLDB Modules
import lldb
from .lldbtest import *
from . import lldbutil
from lldbsuite.test.decorators import *


@skipIfRemote
@add_test_categories(["pexpect"])
class PExpectTest(TestBase):
    NO_DEBUG_INFO_TESTCASE = True
    PROMPT = "(lldb) "

    def expect_prompt(self):
        self.child.expect_exact(self.PROMPT)

    def launch(
        self,
        executable=None,
        extra_args=None,
        timeout=60,
        dimensions=None,
        run_under=None,
        post_spawn=None,
        encoding=None,
        use_colors=False,
    ):
        # Using a log file is incompatible with using utf-8 as the encoding.
        logfile = (
            getattr(sys.stdout, "buffer", sys.stdout)
            if (self.TraceOn() and not encoding)
            else None
        )

        args = []
        if run_under is not None:
            args += run_under
        args += [lldbtest_config.lldbExec, "--no-lldbinit"]
        if not use_colors:
            args.append("--no-use-colors")
        for cmd in self.setUpCommands():
            if "use-color false" in cmd and use_colors:
                continue
            args += ["-O", cmd]
        if executable is not None:
            args += ["--file", executable]
        if extra_args is not None:
            args.extend(extra_args)

        env = dict(os.environ)
        env["TERM"] = "vt100"
        env["HOME"] = self.getBuildDir()

        import pexpect

        self.child = pexpect.spawn(
            args[0],
            args=args[1:],
            logfile=logfile,
            timeout=timeout,
            dimensions=dimensions,
            env=env,
            encoding=encoding,
        )
        self.child.ptyproc.delayafterclose = timeout / 10
        self.child.ptyproc.delayafterterminate = timeout / 10

        if post_spawn is not None:
            post_spawn()
        self.expect_prompt()
        for cmd in self.setUpCommands():
            if "use-color false" in cmd and use_colors:
                continue
            self.child.expect_exact(cmd)
            self.expect_prompt()
        if executable is not None:
            self.child.expect_exact("target create")
            self.child.expect_exact("Current executable set to")
            self.expect_prompt()

    def expect(self, cmd, substrs=None):
        self.assertNotIn("\n", cmd)
        # If 'substrs' is a string then this code would just check that every
        # character of the string is in the output.
        assert not isinstance(substrs, str), "substrs must be a collection of strings"

        self.child.sendline(cmd)
        if substrs is not None:
            for s in substrs:
                self.child.expect_exact(s)
        self.expect_prompt()

    def quit(self, gracefully=True):
        self.child.sendeof()
        self.child.close(force=not gracefully)
        self.child = None

    def cursor_forward_escape_seq(self, chars_to_move):
        """
        Returns the escape sequence to move the cursor forward/right
        by a certain amount of characters.
        """
        return b"\x1b\[" + str(chars_to_move).encode("utf-8") + b"C"
