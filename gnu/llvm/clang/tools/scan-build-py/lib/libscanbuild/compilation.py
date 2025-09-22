# -*- coding: utf-8 -*-
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
""" This module is responsible for to parse a compiler invocation. """

import re
import os
import collections

__all__ = ["split_command", "classify_source", "compiler_language"]

# Ignored compiler options map for compilation database creation.
# The map is used in `split_command` method. (Which does ignore and classify
# parameters.) Please note, that these are not the only parameters which
# might be ignored.
#
# Keys are the option name, value number of options to skip
IGNORED_FLAGS = {
    # compiling only flag, ignored because the creator of compilation
    # database will explicitly set it.
    "-c": 0,
    # preprocessor macros, ignored because would cause duplicate entries in
    # the output (the only difference would be these flags). this is actual
    # finding from users, who suffered longer execution time caused by the
    # duplicates.
    "-MD": 0,
    "-MMD": 0,
    "-MG": 0,
    "-MP": 0,
    "-MF": 1,
    "-MT": 1,
    "-MQ": 1,
    # linker options, ignored because for compilation database will contain
    # compilation commands only. so, the compiler would ignore these flags
    # anyway. the benefit to get rid of them is to make the output more
    # readable.
    "-static": 0,
    "-shared": 0,
    "-s": 0,
    "-rdynamic": 0,
    "-l": 1,
    "-L": 1,
    "-u": 1,
    "-z": 1,
    "-T": 1,
    "-Xlinker": 1,
}

# Known C/C++ compiler executable name patterns
COMPILER_PATTERNS = frozenset(
    [
        re.compile(r"^(intercept-|analyze-|)c(c|\+\+)$"),
        re.compile(r"^([^-]*-)*[mg](cc|\+\+)(-\d+(\.\d+){0,2})?$"),
        re.compile(r"^([^-]*-)*clang(\+\+)?(-\d+(\.\d+){0,2})?$"),
        re.compile(r"^llvm-g(cc|\+\+)$"),
    ]
)


def split_command(command):
    """Returns a value when the command is a compilation, None otherwise.

    The value on success is a named tuple with the following attributes:

        files:    list of source files
        flags:    list of compile options
        compiler: string value of 'c' or 'c++'"""

    # the result of this method
    result = collections.namedtuple("Compilation", ["compiler", "flags", "files"])
    result.compiler = compiler_language(command)
    result.flags = []
    result.files = []
    # quit right now, if the program was not a C/C++ compiler
    if not result.compiler:
        return None
    # iterate on the compile options
    args = iter(command[1:])
    for arg in args:
        # quit when compilation pass is not involved
        if arg in {"-E", "-S", "-cc1", "-M", "-MM", "-###"}:
            return None
        # ignore some flags
        elif arg in IGNORED_FLAGS:
            count = IGNORED_FLAGS[arg]
            for _ in range(count):
                next(args)
        elif re.match(r"^-(l|L|Wl,).+", arg):
            pass
        # some parameters could look like filename, take as compile option
        elif arg in {"-D", "-I"}:
            result.flags.extend([arg, next(args)])
        # parameter which looks source file is taken...
        elif re.match(r"^[^-].+", arg) and classify_source(arg):
            result.files.append(arg)
        # and consider everything else as compile option.
        else:
            result.flags.append(arg)
    # do extra check on number of source files
    return result if result.files else None


def classify_source(filename, c_compiler=True):
    """Return the language from file name extension."""

    mapping = {
        ".c": "c" if c_compiler else "c++",
        ".i": "c-cpp-output" if c_compiler else "c++-cpp-output",
        ".ii": "c++-cpp-output",
        ".m": "objective-c",
        ".mi": "objective-c-cpp-output",
        ".mm": "objective-c++",
        ".mii": "objective-c++-cpp-output",
        ".C": "c++",
        ".cc": "c++",
        ".CC": "c++",
        ".cp": "c++",
        ".cpp": "c++",
        ".cxx": "c++",
        ".c++": "c++",
        ".C++": "c++",
        ".txx": "c++",
    }

    __, extension = os.path.splitext(os.path.basename(filename))
    return mapping.get(extension)


def compiler_language(command):
    """A predicate to decide the command is a compiler call or not.

    Returns 'c' or 'c++' when it match. None otherwise."""

    cplusplus = re.compile(r"^(.+)(\+\+)(-.+|)$")

    if command:
        executable = os.path.basename(command[0])
        if any(pattern.match(executable) for pattern in COMPILER_PATTERNS):
            return "c++" if cplusplus.match(executable) else "c"
    return None
