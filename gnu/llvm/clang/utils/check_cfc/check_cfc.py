#!/usr/bin/env python

"""Check CFC - Check Compile Flow Consistency

This is a compiler wrapper for testing that code generation is consistent with
different compilation processes. It checks that code is not unduly affected by
compiler options or other changes which should not have side effects.

To use:
-Ensure that the compiler under test (i.e. clang, clang++) is on the PATH
-On Linux copy this script to the name of the compiler
   e.g. cp check_cfc.py clang && cp check_cfc.py clang++
-On Windows use setup.py to generate check_cfc.exe and copy that to clang.exe
 and clang++.exe
-Enable the desired checks in check_cfc.cfg (in the same directory as the
 wrapper)
   e.g.
[Checks]
dash_g_no_change = true
dash_s_no_change = false

-The wrapper can be run using its absolute path or added to PATH before the
 compiler under test
   e.g. export PATH=<path to check_cfc>:$PATH
-Compile as normal. The wrapper intercepts normal -c compiles and will return
 non-zero if the check fails.
   e.g.
$ clang -c test.cpp
Code difference detected with -g
--- /tmp/tmp5nv893.o
+++ /tmp/tmp6Vwjnc.o
@@ -1 +1 @@
-   0:       48 8b 05 51 0b 20 00    mov    0x200b51(%rip),%rax
+   0:       48 39 3d 51 0b 20 00    cmp    %rdi,0x200b51(%rip)

-To run LNT with Check CFC specify the absolute path to the wrapper to the --cc
 and --cxx options
   e.g.
   lnt runtest nt --cc <path to check_cfc>/clang \\
           --cxx <path to check_cfc>/clang++ ...

To add a new check:
-Create a new subclass of WrapperCheck
-Implement the perform_check() method. This should perform the alternate compile
 and do the comparison.
-Add the new check to check_cfc.cfg. The check has the same name as the
 subclass.
"""

from __future__ import absolute_import, division, print_function

import imp
import os
import platform
import shutil
import subprocess
import sys
import tempfile

try:
    import configparser
except ImportError:
    import ConfigParser as configparser
import io

import obj_diff


def is_windows():
    """Returns True if running on Windows."""
    return platform.system() == "Windows"


class WrapperStepException(Exception):
    """Exception type to be used when a step other than the original compile
    fails."""

    def __init__(self, msg, stdout, stderr):
        self.msg = msg
        self.stdout = stdout
        self.stderr = stderr


class WrapperCheckException(Exception):
    """Exception type to be used when a comparison check fails."""

    def __init__(self, msg):
        self.msg = msg


def main_is_frozen():
    """Returns True when running as a py2exe executable."""
    return (
        hasattr(sys, "frozen")
        or hasattr(sys, "importers")  # new py2exe
        or imp.is_frozen("__main__")  # old py2exe
    )  # tools/freeze


def get_main_dir():
    """Get the directory that the script or executable is located in."""
    if main_is_frozen():
        return os.path.dirname(sys.executable)
    return os.path.dirname(sys.argv[0])


def remove_dir_from_path(path_var, directory):
    """Remove the specified directory from path_var, a string representing
    PATH"""
    pathlist = path_var.split(os.pathsep)
    norm_directory = os.path.normpath(os.path.normcase(directory))
    pathlist = [
        x for x in pathlist if os.path.normpath(os.path.normcase(x)) != norm_directory
    ]
    return os.pathsep.join(pathlist)


def path_without_wrapper():
    """Returns the PATH variable modified to remove the path to this program."""
    scriptdir = get_main_dir()
    path = os.environ["PATH"]
    return remove_dir_from_path(path, scriptdir)


def flip_dash_g(args):
    """Search for -g in args. If it exists then return args without. If not then
    add it."""
    if "-g" in args:
        # Return args without any -g
        return [x for x in args if x != "-g"]
    else:
        # No -g, add one
        return args + ["-g"]


def derive_output_file(args):
    """Derive output file from the input file (if just one) or None
    otherwise."""
    infile = get_input_file(args)
    if infile is None:
        return None
    else:
        return "{}.o".format(os.path.splitext(infile)[0])


def get_output_file(args):
    """Return the output file specified by this command or None if not
    specified."""
    grabnext = False
    for arg in args:
        if grabnext:
            return arg
        if arg == "-o":
            # Specified as a separate arg
            grabnext = True
        elif arg.startswith("-o"):
            # Specified conjoined with -o
            return arg[2:]
    assert not grabnext

    return None


def is_output_specified(args):
    """Return true is output file is specified in args."""
    return get_output_file(args) is not None


def replace_output_file(args, new_name):
    """Replaces the specified name of an output file with the specified name.
    Assumes that the output file name is specified in the command line args."""
    replaceidx = None
    attached = False
    for idx, val in enumerate(args):
        if val == "-o":
            replaceidx = idx + 1
            attached = False
        elif val.startswith("-o"):
            replaceidx = idx
            attached = True

    if replaceidx is None:
        raise Exception
    replacement = new_name
    if attached:
        replacement = "-o" + new_name
    args[replaceidx] = replacement
    return args


def add_output_file(args, output_file):
    """Append an output file to args, presuming not already specified."""
    return args + ["-o", output_file]


def set_output_file(args, output_file):
    """Set the output file within the arguments. Appends or replaces as
    appropriate."""
    if is_output_specified(args):
        args = replace_output_file(args, output_file)
    else:
        args = add_output_file(args, output_file)
    return args


gSrcFileSuffixes = (".c", ".cpp", ".cxx", ".c++", ".cp", ".cc")


def get_input_file(args):
    """Return the input file string if it can be found (and there is only
    one)."""
    inputFiles = list()
    for arg in args:
        testarg = arg
        quotes = ('"', "'")
        while testarg.endswith(quotes):
            testarg = testarg[:-1]
        testarg = os.path.normcase(testarg)

        # Test if it is a source file
        if testarg.endswith(gSrcFileSuffixes):
            inputFiles.append(arg)
    if len(inputFiles) == 1:
        return inputFiles[0]
    else:
        return None


def set_input_file(args, input_file):
    """Replaces the input file with that specified."""
    infile = get_input_file(args)
    if infile:
        infile_idx = args.index(infile)
        args[infile_idx] = input_file
        return args
    else:
        # Could not find input file
        assert False


def is_normal_compile(args):
    """Check if this is a normal compile which will output an object file rather
    than a preprocess or link. args is a list of command line arguments."""
    compile_step = "-c" in args
    # Bitcode cannot be disassembled in the same way
    bitcode = "-flto" in args or "-emit-llvm" in args
    # Version and help are queries of the compiler and override -c if specified
    query = "--version" in args or "--help" in args
    # Options to output dependency files for make
    dependency = "-M" in args or "-MM" in args
    # Check if the input is recognised as a source file (this may be too
    # strong a restriction)
    input_is_valid = bool(get_input_file(args))
    return (
        compile_step and not bitcode and not query and not dependency and input_is_valid
    )


def run_step(command, my_env, error_on_failure):
    """Runs a step of the compilation. Reports failure as exception."""
    # Need to use shell=True on Windows as Popen won't use PATH otherwise.
    p = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=my_env,
        shell=is_windows(),
    )
    (stdout, stderr) = p.communicate()
    if p.returncode != 0:
        raise WrapperStepException(error_on_failure, stdout, stderr)


def get_temp_file_name(suffix):
    """Get a temporary file name with a particular suffix. Let the caller be
    responsible for deleting it."""
    tf = tempfile.NamedTemporaryFile(suffix=suffix, delete=False)
    tf.close()
    return tf.name


class WrapperCheck(object):
    """Base class for a check. Subclass this to add a check."""

    def __init__(self, output_file_a):
        """Record the base output file that will be compared against."""
        self._output_file_a = output_file_a

    def perform_check(self, arguments, my_env):
        """Override this to perform the modified compilation and required
        checks."""
        raise NotImplementedError("Please Implement this method")


class dash_g_no_change(WrapperCheck):
    def perform_check(self, arguments, my_env):
        """Check if different code is generated with/without the -g flag."""
        output_file_b = get_temp_file_name(".o")

        alternate_command = list(arguments)
        alternate_command = flip_dash_g(alternate_command)
        alternate_command = set_output_file(alternate_command, output_file_b)
        run_step(alternate_command, my_env, "Error compiling with -g")

        # Compare disassembly (returns first diff if differs)
        difference = obj_diff.compare_object_files(self._output_file_a, output_file_b)
        if difference:
            raise WrapperCheckException(
                "Code difference detected with -g\n{}".format(difference)
            )

        # Clean up temp file if comparison okay
        os.remove(output_file_b)


class dash_s_no_change(WrapperCheck):
    def perform_check(self, arguments, my_env):
        """Check if compiling to asm then assembling in separate steps results
        in different code than compiling to object directly."""
        output_file_b = get_temp_file_name(".o")

        alternate_command = arguments + ["-via-file-asm"]
        alternate_command = set_output_file(alternate_command, output_file_b)
        run_step(alternate_command, my_env, "Error compiling with -via-file-asm")

        # Compare if object files are exactly the same
        exactly_equal = obj_diff.compare_exact(self._output_file_a, output_file_b)
        if not exactly_equal:
            # Compare disassembly (returns first diff if differs)
            difference = obj_diff.compare_object_files(
                self._output_file_a, output_file_b
            )
            if difference:
                raise WrapperCheckException(
                    "Code difference detected with -S\n{}".format(difference)
                )

            # Code is identical, compare debug info
            dbgdifference = obj_diff.compare_debug_info(
                self._output_file_a, output_file_b
            )
            if dbgdifference:
                raise WrapperCheckException(
                    "Debug info difference detected with -S\n{}".format(dbgdifference)
                )

            raise WrapperCheckException("Object files not identical with -S\n")

        # Clean up temp file if comparison okay
        os.remove(output_file_b)


if __name__ == "__main__":
    # Create configuration defaults from list of checks
    default_config = """
[Checks]
"""

    # Find all subclasses of WrapperCheck
    checks = [cls.__name__ for cls in vars()["WrapperCheck"].__subclasses__()]

    for c in checks:
        default_config += "{} = false\n".format(c)

    config = configparser.RawConfigParser()
    config.readfp(io.BytesIO(default_config))
    scriptdir = get_main_dir()
    config_path = os.path.join(scriptdir, "check_cfc.cfg")
    try:
        config.read(os.path.join(config_path))
    except:
        print("Could not read config from {}, " "using defaults.".format(config_path))

    my_env = os.environ.copy()
    my_env["PATH"] = path_without_wrapper()

    arguments_a = list(sys.argv)

    # Prevent infinite loop if called with absolute path.
    arguments_a[0] = os.path.basename(arguments_a[0])

    # Basic correctness check
    enabled_checks = [
        check_name for check_name in checks if config.getboolean("Checks", check_name)
    ]
    checks_comma_separated = ", ".join(enabled_checks)
    print("Check CFC, checking: {}".format(checks_comma_separated))

    # A - original compilation
    output_file_orig = get_output_file(arguments_a)
    if output_file_orig is None:
        output_file_orig = derive_output_file(arguments_a)

    p = subprocess.Popen(arguments_a, env=my_env, shell=is_windows())
    p.communicate()
    if p.returncode != 0:
        sys.exit(p.returncode)

    if not is_normal_compile(arguments_a) or output_file_orig is None:
        # Bail out here if we can't apply checks in this case.
        # Does not indicate an error.
        # Maybe not straight compilation (e.g. -S or --version or -flto)
        # or maybe > 1 input files.
        sys.exit(0)

    # Sometimes we generate files which have very long names which can't be
    # read/disassembled. This will exit early if we can't find the file we
    # expected to be output.
    if not os.path.isfile(output_file_orig):
        sys.exit(0)

    # Copy output file to a temp file
    temp_output_file_orig = get_temp_file_name(".o")
    shutil.copyfile(output_file_orig, temp_output_file_orig)

    # Run checks, if they are enabled in config and if they are appropriate for
    # this command line.
    current_module = sys.modules[__name__]
    for check_name in checks:
        if config.getboolean("Checks", check_name):
            class_ = getattr(current_module, check_name)
            checker = class_(temp_output_file_orig)
            try:
                checker.perform_check(arguments_a, my_env)
            except WrapperCheckException as e:
                # Check failure
                print(
                    "{} {}".format(get_input_file(arguments_a), e.msg), file=sys.stderr
                )

                # Remove file to comply with build system expectations (no
                # output file if failed)
                os.remove(output_file_orig)
                sys.exit(1)

            except WrapperStepException as e:
                # Compile step failure
                print(e.msg, file=sys.stderr)
                print("*** stdout ***", file=sys.stderr)
                print(e.stdout, file=sys.stderr)
                print("*** stderr ***", file=sys.stderr)
                print(e.stderr, file=sys.stderr)

                # Remove file to comply with build system expectations (no
                # output file if failed)
                os.remove(output_file_orig)
                sys.exit(1)
