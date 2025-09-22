#!/usr/bin/env python3
"""Calls C-Reduce to create a minimal reproducer for clang crashes.
Unknown arguments are treated at creduce options.

Output files:
  *.reduced.sh -- crash reproducer with minimal arguments
  *.reduced.cpp -- the reduced file
  *.test.sh -- interestingness test for C-Reduce
"""

from __future__ import print_function
from argparse import ArgumentParser, RawTextHelpFormatter
import os
import re
import shutil
import stat
import sys
import subprocess
import shlex
import tempfile
import shutil
import multiprocessing

verbose = False
creduce_cmd = None
clang_cmd = None


def verbose_print(*args, **kwargs):
    if verbose:
        print(*args, **kwargs)


def check_file(fname):
    fname = os.path.normpath(fname)
    if not os.path.isfile(fname):
        sys.exit("ERROR: %s does not exist" % (fname))
    return fname


def check_cmd(cmd_name, cmd_dir, cmd_path=None):
    """
    Returns absolute path to cmd_path if it is given,
    or absolute path to cmd_dir/cmd_name.
    """
    if cmd_path:
        # Make the path absolute so the creduce test can be run from any directory.
        cmd_path = os.path.abspath(cmd_path)
        cmd = shutil.which(cmd_path)
        if cmd:
            return cmd
        sys.exit("ERROR: executable `%s` not found" % (cmd_path))

    cmd = shutil.which(cmd_name, path=cmd_dir)
    if cmd:
        return cmd

    if not cmd_dir:
        cmd_dir = "$PATH"
    sys.exit("ERROR: `%s` not found in %s" % (cmd_name, cmd_dir))


def quote_cmd(cmd):
    return " ".join(shlex.quote(arg) for arg in cmd)


def write_to_script(text, filename):
    with open(filename, "w") as f:
        f.write(text)
    os.chmod(filename, os.stat(filename).st_mode | stat.S_IEXEC)


class Reduce(object):
    def __init__(self, crash_script, file_to_reduce, creduce_flags):
        crash_script_name, crash_script_ext = os.path.splitext(crash_script)
        file_reduce_name, file_reduce_ext = os.path.splitext(file_to_reduce)

        self.testfile = file_reduce_name + ".test.sh"
        self.crash_script = crash_script_name + ".reduced" + crash_script_ext
        self.file_to_reduce = file_reduce_name + ".reduced" + file_reduce_ext
        shutil.copy(file_to_reduce, self.file_to_reduce)

        self.clang = clang_cmd
        self.clang_args = []
        self.expected_output = []
        self.needs_stack_trace = False
        self.creduce_flags = ["--tidy"] + creduce_flags

        self.read_clang_args(crash_script, file_to_reduce)
        self.read_expected_output()

    def get_crash_cmd(self, cmd=None, args=None, filename=None):
        if not cmd:
            cmd = self.clang
        if not args:
            args = self.clang_args
        if not filename:
            filename = self.file_to_reduce

        return [cmd] + args + [filename]

    def read_clang_args(self, crash_script, filename):
        print("\nReading arguments from crash script...")
        with open(crash_script) as f:
            # Assume clang call is the first non comment line.
            cmd = []
            for line in f:
                if not line.lstrip().startswith("#"):
                    cmd = shlex.split(line)
                    break
        if not cmd:
            sys.exit("Could not find command in the crash script.")

        # Remove clang and filename from the command
        # Assume the last occurrence of the filename is the clang input file
        del cmd[0]
        for i in range(len(cmd) - 1, -1, -1):
            if cmd[i] == filename:
                del cmd[i]
                break
        self.clang_args = cmd
        verbose_print("Clang arguments:", quote_cmd(self.clang_args))

    def read_expected_output(self):
        print("\nGetting expected crash output...")
        p = subprocess.Popen(
            self.get_crash_cmd(), stdout=subprocess.PIPE, stderr=subprocess.STDOUT
        )
        crash_output, _ = p.communicate()
        result = []

        # Remove color codes
        ansi_escape = r"\x1b\[[0-?]*m"
        crash_output = re.sub(ansi_escape, "", crash_output.decode("utf-8"))

        # Look for specific error messages
        regexes = [
            r"Assertion .+ failed",  # Linux assert()
            r"Assertion failed: .+,",  # FreeBSD/Mac assert()
            r"fatal error: error in backend: .+",
            r"LLVM ERROR: .+",
            r"UNREACHABLE executed at .+?!",
            r"LLVM IR generation of declaration '.+'",
            r"Generating code for declaration '.+'",
            r"\*\*\* Bad machine code: .+ \*\*\*",
            r"ERROR: .*Sanitizer: [^ ]+ ",
        ]
        for msg_re in regexes:
            match = re.search(msg_re, crash_output)
            if match:
                msg = match.group(0)
                result = [msg]
                print("Found message:", msg)
                break

        # If no message was found, use the top five stack trace functions,
        # ignoring some common functions
        # Five is a somewhat arbitrary number; the goal is to get a small number
        # of identifying functions with some leeway for common functions
        if not result:
            self.needs_stack_trace = True
            stacktrace_re = r"[0-9]+\s+0[xX][0-9a-fA-F]+\s*([^(]+)\("
            filters = [
                "PrintStackTrace",
                "RunSignalHandlers",
                "CleanupOnSignal",
                "HandleCrash",
                "SignalHandler",
                "__restore_rt",
                "gsignal",
                "abort",
            ]

            def skip_function(func_name):
                return any(name in func_name for name in filters)

            matches = re.findall(stacktrace_re, crash_output)
            result = [x for x in matches if x and not skip_function(x)][:5]
            for msg in result:
                print("Found stack trace function:", msg)

        if not result:
            print("ERROR: no crash was found")
            print("The crash output was:\n========\n%s========" % crash_output)
            sys.exit(1)

        self.expected_output = result

    def check_expected_output(self, args=None, filename=None):
        if not args:
            args = self.clang_args
        if not filename:
            filename = self.file_to_reduce

        p = subprocess.Popen(
            self.get_crash_cmd(args=args, filename=filename),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        crash_output, _ = p.communicate()
        return all(msg in crash_output.decode("utf-8") for msg in self.expected_output)

    def write_interestingness_test(self):
        print("\nCreating the interestingness test...")

        # Disable symbolization if it's not required to avoid slow symbolization.
        disable_symbolization = ""
        if not self.needs_stack_trace:
            disable_symbolization = "export LLVM_DISABLE_SYMBOLIZATION=1"

        output = """#!/bin/bash
%s
if %s >& t.log ; then
  exit 1
fi
""" % (
            disable_symbolization,
            quote_cmd(self.get_crash_cmd()),
        )

        for msg in self.expected_output:
            output += "grep -F %s t.log || exit 1\n" % shlex.quote(msg)

        write_to_script(output, self.testfile)
        self.check_interestingness()

    def check_interestingness(self):
        testfile = os.path.abspath(self.testfile)

        # Check that the test considers the original file interesting
        with open(os.devnull, "w") as devnull:
            returncode = subprocess.call(testfile, stdout=devnull)
        if returncode:
            sys.exit("The interestingness test does not pass for the original file.")

        # Check that an empty file is not interesting
        # Instead of modifying the filename in the test file, just run the command
        with tempfile.NamedTemporaryFile() as empty_file:
            is_interesting = self.check_expected_output(filename=empty_file.name)
        if is_interesting:
            sys.exit("The interestingness test passes for an empty file.")

    def clang_preprocess(self):
        print("\nTrying to preprocess the source file...")
        with tempfile.NamedTemporaryFile() as tmpfile:
            cmd_preprocess = self.get_crash_cmd() + ["-E", "-o", tmpfile.name]
            cmd_preprocess_no_lines = cmd_preprocess + ["-P"]
            try:
                subprocess.check_call(cmd_preprocess_no_lines)
                if self.check_expected_output(filename=tmpfile.name):
                    print("Successfully preprocessed with line markers removed")
                    shutil.copy(tmpfile.name, self.file_to_reduce)
                else:
                    subprocess.check_call(cmd_preprocess)
                    if self.check_expected_output(filename=tmpfile.name):
                        print("Successfully preprocessed without removing line markers")
                        shutil.copy(tmpfile.name, self.file_to_reduce)
                    else:
                        print(
                            "No longer crashes after preprocessing -- "
                            "using original source"
                        )
            except subprocess.CalledProcessError:
                print("Preprocessing failed")

    @staticmethod
    def filter_args(
        args, opts_equal=[], opts_startswith=[], opts_one_arg_startswith=[]
    ):
        result = []
        skip_next = False
        for arg in args:
            if skip_next:
                skip_next = False
                continue
            if any(arg == a for a in opts_equal):
                continue
            if any(arg.startswith(a) for a in opts_startswith):
                continue
            if any(arg.startswith(a) for a in opts_one_arg_startswith):
                skip_next = True
                continue
            result.append(arg)
        return result

    def try_remove_args(self, args, msg=None, extra_arg=None, **kwargs):
        new_args = self.filter_args(args, **kwargs)

        if extra_arg:
            if extra_arg in new_args:
                new_args.remove(extra_arg)
            new_args.append(extra_arg)

        if new_args != args and self.check_expected_output(args=new_args):
            if msg:
                verbose_print(msg)
            return new_args
        return args

    def try_remove_arg_by_index(self, args, index):
        new_args = args[:index] + args[index + 1 :]
        removed_arg = args[index]

        # Heuristic for grouping arguments:
        # remove next argument if it doesn't start with "-"
        if index < len(new_args) and not new_args[index].startswith("-"):
            del new_args[index]
            removed_arg += " " + args[index + 1]

        if self.check_expected_output(args=new_args):
            verbose_print("Removed", removed_arg)
            return new_args, index
        return args, index + 1

    def simplify_clang_args(self):
        """Simplify clang arguments before running C-Reduce to reduce the time the
        interestingness test takes to run.
        """
        print("\nSimplifying the clang command...")
        new_args = self.clang_args

        # Remove the color diagnostics flag to make it easier to match error
        # text.
        new_args = self.try_remove_args(
            new_args,
            msg="Removed -fcolor-diagnostics",
            opts_equal=["-fcolor-diagnostics"],
        )

        # Remove some clang arguments to speed up the interestingness test
        new_args = self.try_remove_args(
            new_args,
            msg="Removed debug info options",
            opts_startswith=["-gcodeview", "-debug-info-kind=", "-debugger-tuning="],
        )

        new_args = self.try_remove_args(
            new_args, msg="Removed --show-includes", opts_startswith=["--show-includes"]
        )
        # Not suppressing warnings (-w) sometimes prevents the crash from occurring
        # after preprocessing
        new_args = self.try_remove_args(
            new_args,
            msg="Replaced -W options with -w",
            extra_arg="-w",
            opts_startswith=["-W"],
        )
        new_args = self.try_remove_args(
            new_args,
            msg="Replaced optimization level with -O0",
            extra_arg="-O0",
            opts_startswith=["-O"],
        )

        # Try to remove compilation steps
        new_args = self.try_remove_args(
            new_args, msg="Added -emit-llvm", extra_arg="-emit-llvm"
        )
        new_args = self.try_remove_args(
            new_args, msg="Added -fsyntax-only", extra_arg="-fsyntax-only"
        )

        # Try to make implicit int an error for more sensible test output
        new_args = self.try_remove_args(
            new_args,
            msg="Added -Werror=implicit-int",
            opts_equal=["-w"],
            extra_arg="-Werror=implicit-int",
        )

        self.clang_args = new_args
        verbose_print("Simplified command:", quote_cmd(self.get_crash_cmd()))

    def reduce_clang_args(self):
        """Minimize the clang arguments after running C-Reduce, to get the smallest
        command that reproduces the crash on the reduced file.
        """
        print("\nReducing the clang crash command...")

        new_args = self.clang_args

        # Remove some often occurring args
        new_args = self.try_remove_args(
            new_args, msg="Removed -D options", opts_startswith=["-D"]
        )
        new_args = self.try_remove_args(
            new_args, msg="Removed -D options", opts_one_arg_startswith=["-D"]
        )
        new_args = self.try_remove_args(
            new_args, msg="Removed -I options", opts_startswith=["-I"]
        )
        new_args = self.try_remove_args(
            new_args, msg="Removed -I options", opts_one_arg_startswith=["-I"]
        )
        new_args = self.try_remove_args(
            new_args, msg="Removed -W options", opts_startswith=["-W"]
        )

        # Remove other cases that aren't covered by the heuristic
        new_args = self.try_remove_args(
            new_args, msg="Removed -mllvm", opts_one_arg_startswith=["-mllvm"]
        )

        i = 0
        while i < len(new_args):
            new_args, i = self.try_remove_arg_by_index(new_args, i)

        self.clang_args = new_args

        reduced_cmd = quote_cmd(self.get_crash_cmd())
        write_to_script(reduced_cmd, self.crash_script)
        print("Reduced command:", reduced_cmd)

    def run_creduce(self):
        full_creduce_cmd = (
            [creduce_cmd] + self.creduce_flags + [self.testfile, self.file_to_reduce]
        )
        print("\nRunning C-Reduce...")
        verbose_print(quote_cmd(full_creduce_cmd))
        try:
            p = subprocess.Popen(full_creduce_cmd)
            p.communicate()
        except KeyboardInterrupt:
            # Hack to kill C-Reduce because it jumps into its own pgid
            print("\n\nctrl-c detected, killed creduce")
            p.kill()


def main():
    global verbose
    global creduce_cmd
    global clang_cmd

    parser = ArgumentParser(description=__doc__, formatter_class=RawTextHelpFormatter)
    parser.add_argument(
        "crash_script",
        type=str,
        nargs=1,
        help="Name of the script that generates the crash.",
    )
    parser.add_argument(
        "file_to_reduce", type=str, nargs=1, help="Name of the file to be reduced."
    )
    parser.add_argument(
        "--llvm-bin", dest="llvm_bin", type=str, help="Path to the LLVM bin directory."
    )
    parser.add_argument(
        "--clang",
        dest="clang",
        type=str,
        help="The path to the `clang` executable. "
        "By default uses the llvm-bin directory.",
    )
    parser.add_argument(
        "--creduce",
        dest="creduce",
        type=str,
        help="The path to the `creduce` executable. "
        "Required if `creduce` is not in PATH environment.",
    )
    parser.add_argument("-v", "--verbose", action="store_true")
    args, creduce_flags = parser.parse_known_args()
    verbose = args.verbose
    llvm_bin = os.path.abspath(args.llvm_bin) if args.llvm_bin else None
    creduce_cmd = check_cmd("creduce", None, args.creduce)
    clang_cmd = check_cmd("clang", llvm_bin, args.clang)

    crash_script = check_file(args.crash_script[0])
    file_to_reduce = check_file(args.file_to_reduce[0])

    if "--n" not in creduce_flags:
        creduce_flags += ["--n", str(max(4, multiprocessing.cpu_count() // 2))]

    r = Reduce(crash_script, file_to_reduce, creduce_flags)

    r.simplify_clang_args()
    r.write_interestingness_test()
    r.clang_preprocess()
    r.run_creduce()
    r.reduce_clang_args()


if __name__ == "__main__":
    main()
