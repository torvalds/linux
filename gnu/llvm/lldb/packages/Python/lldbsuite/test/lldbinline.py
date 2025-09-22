# System modules
import os
import textwrap

# Third-party modules
import io

# LLDB modules
import lldb
from .lldbtest import *
from . import configuration
from . import lldbutil
from .decorators import *


def source_type(filename):
    _, extension = os.path.splitext(filename)
    return {
        ".c": "C_SOURCES",
        ".cpp": "CXX_SOURCES",
        ".cxx": "CXX_SOURCES",
        ".cc": "CXX_SOURCES",
        ".m": "OBJC_SOURCES",
        ".mm": "OBJCXX_SOURCES",
    }.get(extension, None)


class CommandParser:
    def __init__(self):
        self.breakpoints = []

    def parse_one_command(self, line):
        parts = line.split("//%")

        command = None
        new_breakpoint = True

        if len(parts) == 2:
            command = parts[1].rstrip()
            new_breakpoint = parts[0].strip() != ""

        return (command, new_breakpoint)

    def parse_source_files(self, source_files):
        for source_file in source_files:
            file_handle = io.open(source_file, encoding="utf-8")
            lines = file_handle.readlines()
            line_number = 0
            # non-NULL means we're looking through whitespace to find
            # additional commands
            current_breakpoint = None
            for line in lines:
                line_number = line_number + 1  # 1-based, so we do this first
                (command, new_breakpoint) = self.parse_one_command(line)

                if new_breakpoint:
                    current_breakpoint = None

                if command is not None:
                    if current_breakpoint is None:
                        current_breakpoint = {}
                        current_breakpoint["file_name"] = source_file
                        current_breakpoint["line_number"] = line_number
                        current_breakpoint["command"] = command
                        self.breakpoints.append(current_breakpoint)
                    else:
                        current_breakpoint["command"] = (
                            current_breakpoint["command"] + "\n" + command
                        )
        for bkpt in self.breakpoints:
            bkpt["command"] = textwrap.dedent(bkpt["command"])

    def set_breakpoints(self, target):
        for breakpoint in self.breakpoints:
            breakpoint["breakpoint"] = target.BreakpointCreateByLocation(
                breakpoint["file_name"], breakpoint["line_number"]
            )

    def handle_breakpoint(self, test, breakpoint_id):
        for breakpoint in self.breakpoints:
            if breakpoint["breakpoint"].GetID() == breakpoint_id:
                test.execute_user_command(breakpoint["command"])
                return


class InlineTest(TestBase):
    def getBuildDirBasename(self):
        return self.__class__.__name__ + "." + self.testMethodName

    def BuildMakefile(self):
        makefilePath = self.getBuildArtifact("Makefile")
        if os.path.exists(makefilePath):
            return

        categories = {}
        for f in os.listdir(self.getSourceDir()):
            t = source_type(f)
            if t:
                if t in list(categories.keys()):
                    categories[t].append(f)
                else:
                    categories[t] = [f]

        with open(makefilePath, "w+") as makefile:
            for t in list(categories.keys()):
                line = t + " := " + " ".join(categories[t])
                makefile.write(line + "\n")

            if ("OBJCXX_SOURCES" in list(categories.keys())) or (
                "OBJC_SOURCES" in list(categories.keys())
            ):
                makefile.write("LDFLAGS = $(CFLAGS) -lobjc -framework Foundation\n")

            if "CXX_SOURCES" in list(categories.keys()):
                makefile.write("CXXFLAGS += -std=c++11\n")

            makefile.write("include Makefile.rules\n")

    def _test(self):
        self.BuildMakefile()
        self.build(dictionary=self._build_dict)
        self.do_test()

    def execute_user_command(self, __command):
        exec(__command, globals(), locals())

    def _get_breakpoint_ids(self, thread):
        ids = set()
        for i in range(0, thread.GetStopReasonDataCount(), 2):
            ids.add(thread.GetStopReasonDataAtIndex(i))
        self.assertGreater(len(ids), 0)
        return sorted(ids)

    def do_test(self):
        exe = self.getBuildArtifact("a.out")
        source_files = [f for f in os.listdir(self.getSourceDir()) if source_type(f)]
        target = self.dbg.CreateTarget(exe)

        parser = CommandParser()
        parser.parse_source_files(source_files)
        parser.set_breakpoints(target)

        process = target.LaunchSimple(None, None, self.get_process_working_directory())
        self.assertIsNotNone(process, PROCESS_IS_VALID)

        hit_breakpoints = 0

        while lldbutil.get_stopped_thread(process, lldb.eStopReasonBreakpoint):
            hit_breakpoints += 1
            thread = lldbutil.get_stopped_thread(process, lldb.eStopReasonBreakpoint)
            for bp_id in self._get_breakpoint_ids(thread):
                parser.handle_breakpoint(self, bp_id)
            process.Continue()

        self.assertTrue(
            hit_breakpoints > 0, "inline test did not hit a single breakpoint"
        )
        # Either the process exited or the stepping plan is complete.
        self.assertTrue(
            process.GetState() in [lldb.eStateStopped, lldb.eStateExited],
            PROCESS_EXITED,
        )

    def check_expression(self, expression, expected_result, use_summary=True):
        value = self.frame().EvaluateExpression(expression)
        self.assertTrue(value.IsValid(), expression + "returned a valid value")
        if self.TraceOn():
            print(value.GetSummary())
            print(value.GetValue())
        if use_summary:
            answer = value.GetSummary()
        else:
            answer = value.GetValue()
        report_str = "%s expected: %s got: %s" % (expression, expected_result, answer)
        self.assertTrue(answer == expected_result, report_str)


def ApplyDecoratorsToFunction(func, decorators):
    tmp = func
    if isinstance(decorators, list):
        for decorator in decorators:
            tmp = decorator(tmp)
    elif hasattr(decorators, "__call__"):
        tmp = decorators(tmp)
    return tmp


def MakeInlineTest(__file, __globals, decorators=None, name=None, build_dict=None):
    # Adjust the filename if it ends in .pyc.  We want filenames to
    # reflect the source python file, not the compiled variant.
    if __file is not None and __file.endswith(".pyc"):
        # Strip the trailing "c"
        __file = __file[0:-1]

    if name is None:
        # Derive the test name from the current file name
        file_basename = os.path.basename(__file)
        name, _ = os.path.splitext(file_basename)

    test_func = ApplyDecoratorsToFunction(InlineTest._test, decorators)
    # Build the test case
    test_class = type(
        name, (InlineTest,), dict(test=test_func, name=name, _build_dict=build_dict)
    )

    # Add the test case to the globals, and hide InlineTest
    __globals.update({name: test_class})

    # Keep track of the original test filename so we report it
    # correctly in test results.
    test_class.test_filename = __file
    test_class.mydir = TestBase.compute_mydir(__file)
    return test_class
