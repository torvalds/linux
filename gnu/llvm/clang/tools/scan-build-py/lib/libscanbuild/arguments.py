# -*- coding: utf-8 -*-
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
""" This module parses and validates arguments for command-line interfaces.

It uses argparse module to create the command line parser. (This library is
in the standard python library since 3.2 and backported to 2.7, but not
earlier.)

It also implements basic validation methods, related to the command.
Validations are mostly calling specific help methods, or mangling values.
"""
from __future__ import absolute_import, division, print_function

import os
import sys
import argparse
import logging
import tempfile
from libscanbuild import reconfigure_logging, CtuConfig
from libscanbuild.clang import get_checkers, is_ctu_capable

__all__ = [
    "parse_args_for_intercept_build",
    "parse_args_for_analyze_build",
    "parse_args_for_scan_build",
]


def parse_args_for_intercept_build():
    """Parse and validate command-line arguments for intercept-build."""

    parser = create_intercept_parser()
    args = parser.parse_args()

    reconfigure_logging(args.verbose)
    logging.debug("Raw arguments %s", sys.argv)

    # short validation logic
    if not args.build:
        parser.error(message="missing build command")

    logging.debug("Parsed arguments: %s", args)
    return args


def parse_args_for_analyze_build():
    """Parse and validate command-line arguments for analyze-build."""

    from_build_command = False
    parser = create_analyze_parser(from_build_command)
    args = parser.parse_args()

    reconfigure_logging(args.verbose)
    logging.debug("Raw arguments %s", sys.argv)

    normalize_args_for_analyze(args, from_build_command)
    validate_args_for_analyze(parser, args, from_build_command)
    logging.debug("Parsed arguments: %s", args)
    return args


def parse_args_for_scan_build():
    """Parse and validate command-line arguments for scan-build."""

    from_build_command = True
    parser = create_analyze_parser(from_build_command)
    args = parser.parse_args()

    reconfigure_logging(args.verbose)
    logging.debug("Raw arguments %s", sys.argv)

    normalize_args_for_analyze(args, from_build_command)
    validate_args_for_analyze(parser, args, from_build_command)
    logging.debug("Parsed arguments: %s", args)
    return args


def normalize_args_for_analyze(args, from_build_command):
    """Normalize parsed arguments for analyze-build and scan-build.

    :param args: Parsed argument object. (Will be mutated.)
    :param from_build_command: Boolean value tells is the command suppose
    to run the analyzer against a build command or a compilation db."""

    # make plugins always a list. (it might be None when not specified.)
    if args.plugins is None:
        args.plugins = []

    # make exclude directory list unique and absolute.
    uniq_excludes = set(os.path.abspath(entry) for entry in args.excludes)
    args.excludes = list(uniq_excludes)

    # because shared codes for all tools, some common used methods are
    # expecting some argument to be present. so, instead of query the args
    # object about the presence of the flag, we fake it here. to make those
    # methods more readable. (it's an arguable choice, took it only for those
    # which have good default value.)
    if from_build_command:
        # add cdb parameter invisibly to make report module working.
        args.cdb = "compile_commands.json"

    # Make ctu_dir an abspath as it is needed inside clang
    if (
        not from_build_command
        and hasattr(args, "ctu_phases")
        and hasattr(args.ctu_phases, "dir")
    ):
        args.ctu_dir = os.path.abspath(args.ctu_dir)


def validate_args_for_analyze(parser, args, from_build_command):
    """Command line parsing is done by the argparse module, but semantic
    validation still needs to be done. This method is doing it for
    analyze-build and scan-build commands.

    :param parser: The command line parser object.
    :param args: Parsed argument object.
    :param from_build_command: Boolean value tells is the command suppose
    to run the analyzer against a build command or a compilation db.
    :return: No return value, but this call might throw when validation
    fails."""

    if args.help_checkers_verbose:
        print_checkers(get_checkers(args.clang, args.plugins))
        parser.exit(status=0)
    elif args.help_checkers:
        print_active_checkers(get_checkers(args.clang, args.plugins))
        parser.exit(status=0)
    elif from_build_command and not args.build:
        parser.error(message="missing build command")
    elif not from_build_command and not os.path.exists(args.cdb):
        parser.error(message="compilation database is missing")

    # If the user wants CTU mode
    if (
        not from_build_command
        and hasattr(args, "ctu_phases")
        and hasattr(args.ctu_phases, "dir")
    ):
        # If CTU analyze_only, the input directory should exist
        if (
            args.ctu_phases.analyze
            and not args.ctu_phases.collect
            and not os.path.exists(args.ctu_dir)
        ):
            parser.error(message="missing CTU directory")
        # Check CTU capability via checking clang-extdef-mapping
        if not is_ctu_capable(args.extdef_map_cmd):
            parser.error(
                message="""This version of clang does not support CTU
            functionality or clang-extdef-mapping command not found."""
            )


def create_intercept_parser():
    """Creates a parser for command-line arguments to 'intercept'."""

    parser = create_default_parser()
    parser_add_cdb(parser)

    parser_add_prefer_wrapper(parser)
    parser_add_compilers(parser)

    advanced = parser.add_argument_group("advanced options")
    group = advanced.add_mutually_exclusive_group()
    group.add_argument(
        "--append",
        action="store_true",
        help="""Extend existing compilation database with new entries.
        Duplicate entries are detected and not present in the final output.
        The output is not continuously updated, it's done when the build
        command finished. """,
    )

    parser.add_argument(
        dest="build", nargs=argparse.REMAINDER, help="""Command to run."""
    )
    return parser


def create_analyze_parser(from_build_command):
    """Creates a parser for command-line arguments to 'analyze'."""

    parser = create_default_parser()

    if from_build_command:
        parser_add_prefer_wrapper(parser)
        parser_add_compilers(parser)

        parser.add_argument(
            "--intercept-first",
            action="store_true",
            help="""Run the build commands first, intercept compiler
            calls and then run the static analyzer afterwards.
            Generally speaking it has better coverage on build commands.
            With '--override-compiler' it use compiler wrapper, but does
            not run the analyzer till the build is finished.""",
        )
    else:
        parser_add_cdb(parser)

    parser.add_argument(
        "--status-bugs",
        action="store_true",
        help="""The exit status of '%(prog)s' is the same as the executed
        build command. This option ignores the build exit status and sets to
        be non zero if it found potential bugs or zero otherwise.""",
    )
    parser.add_argument(
        "--exclude",
        metavar="<directory>",
        dest="excludes",
        action="append",
        default=[],
        help="""Do not run static analyzer against files found in this
        directory. (You can specify this option multiple times.)
        Could be useful when project contains 3rd party libraries.""",
    )

    output = parser.add_argument_group("output control options")
    output.add_argument(
        "--output",
        "-o",
        metavar="<path>",
        default=tempfile.gettempdir(),
        help="""Specifies the output directory for analyzer reports.
        Subdirectory will be created if default directory is targeted.""",
    )
    output.add_argument(
        "--keep-empty",
        action="store_true",
        help="""Don't remove the build results directory even if no issues
        were reported.""",
    )
    output.add_argument(
        "--html-title",
        metavar="<title>",
        help="""Specify the title used on generated HTML pages.
        If not specified, a default title will be used.""",
    )
    format_group = output.add_mutually_exclusive_group()
    format_group.add_argument(
        "--plist",
        "-plist",
        dest="output_format",
        const="plist",
        default="html",
        action="store_const",
        help="""Cause the results as a set of .plist files.""",
    )
    format_group.add_argument(
        "--plist-html",
        "-plist-html",
        dest="output_format",
        const="plist-html",
        default="html",
        action="store_const",
        help="""Cause the results as a set of .html and .plist files.""",
    )
    format_group.add_argument(
        "--plist-multi-file",
        "-plist-multi-file",
        dest="output_format",
        const="plist-multi-file",
        default="html",
        action="store_const",
        help="""Cause the results as a set of .plist files with extra
        information on related files.""",
    )
    format_group.add_argument(
        "--sarif",
        "-sarif",
        dest="output_format",
        const="sarif",
        default="html",
        action="store_const",
        help="""Cause the results as a result.sarif file.""",
    )
    format_group.add_argument(
        "--sarif-html",
        "-sarif-html",
        dest="output_format",
        const="sarif-html",
        default="html",
        action="store_const",
        help="""Cause the results as a result.sarif file and .html files.""",
    )

    advanced = parser.add_argument_group("advanced options")
    advanced.add_argument(
        "--use-analyzer",
        metavar="<path>",
        dest="clang",
        default="clang",
        help="""'%(prog)s' uses the 'clang' executable relative to itself for
        static analysis. One can override this behavior with this option by
        using the 'clang' packaged with Xcode (on OS X) or from the PATH.""",
    )
    advanced.add_argument(
        "--no-failure-reports",
        "-no-failure-reports",
        dest="output_failures",
        action="store_false",
        help="""Do not create a 'failures' subdirectory that includes analyzer
        crash reports and preprocessed source files.""",
    )
    parser.add_argument(
        "--analyze-headers",
        action="store_true",
        help="""Also analyze functions in #included files. By default, such
        functions are skipped unless they are called by functions within the
        main source file.""",
    )
    advanced.add_argument(
        "--stats",
        "-stats",
        action="store_true",
        help="""Generates visitation statistics for the project.""",
    )
    advanced.add_argument(
        "--internal-stats",
        action="store_true",
        help="""Generate internal analyzer statistics.""",
    )
    advanced.add_argument(
        "--maxloop",
        "-maxloop",
        metavar="<loop count>",
        type=int,
        help="""Specify the number of times a block can be visited before
        giving up. Increase for more comprehensive coverage at a cost of
        speed.""",
    )
    advanced.add_argument(
        "--store",
        "-store",
        metavar="<model>",
        dest="store_model",
        choices=["region", "basic"],
        help="""Specify the store model used by the analyzer. 'region'
        specifies a field- sensitive store model. 'basic' which is far less
        precise but can more quickly analyze code. 'basic' was the default
        store model for checker-0.221 and earlier.""",
    )
    advanced.add_argument(
        "--constraints",
        "-constraints",
        metavar="<model>",
        dest="constraints_model",
        choices=["range", "basic"],
        help="""Specify the constraint engine used by the analyzer. Specifying
        'basic' uses a simpler, less powerful constraint model used by
        checker-0.160 and earlier.""",
    )
    advanced.add_argument(
        "--analyzer-config",
        "-analyzer-config",
        metavar="<options>",
        help="""Provide options to pass through to the analyzer's
        -analyzer-config flag. Several options are separated with comma:
        'key1=val1,key2=val2'

        Available options:
            stable-report-filename=true or false (default)

        Switch the page naming to:
        report-<filename>-<function/method name>-<id>.html
        instead of report-XXXXXX.html""",
    )
    advanced.add_argument(
        "--force-analyze-debug-code",
        dest="force_debug",
        action="store_true",
        help="""Tells analyzer to enable assertions in code even if they were
        disabled during compilation, enabling more precise results.""",
    )

    plugins = parser.add_argument_group("checker options")
    plugins.add_argument(
        "--load-plugin",
        "-load-plugin",
        metavar="<plugin library>",
        dest="plugins",
        action="append",
        help="""Loading external checkers using the clang plugin interface.""",
    )
    plugins.add_argument(
        "--enable-checker",
        "-enable-checker",
        metavar="<checker name>",
        action=AppendCommaSeparated,
        help="""Enable specific checker.""",
    )
    plugins.add_argument(
        "--disable-checker",
        "-disable-checker",
        metavar="<checker name>",
        action=AppendCommaSeparated,
        help="""Disable specific checker.""",
    )
    plugins.add_argument(
        "--help-checkers",
        action="store_true",
        help="""A default group of checkers is run unless explicitly disabled.
        Exactly which checkers constitute the default group is a function of
        the operating system in use. These can be printed with this flag.""",
    )
    plugins.add_argument(
        "--help-checkers-verbose",
        action="store_true",
        help="""Print all available checkers and mark the enabled ones.""",
    )

    if from_build_command:
        parser.add_argument(
            dest="build", nargs=argparse.REMAINDER, help="""Command to run."""
        )
    else:
        ctu = parser.add_argument_group("cross translation unit analysis")
        ctu_mutex_group = ctu.add_mutually_exclusive_group()
        ctu_mutex_group.add_argument(
            "--ctu",
            action="store_const",
            const=CtuConfig(collect=True, analyze=True, dir="", extdef_map_cmd=""),
            dest="ctu_phases",
            help="""Perform cross translation unit (ctu) analysis (both collect
            and analyze phases) using default <ctu-dir> for temporary output.
            At the end of the analysis, the temporary directory is removed.""",
        )
        ctu.add_argument(
            "--ctu-dir",
            metavar="<ctu-dir>",
            dest="ctu_dir",
            default="ctu-dir",
            help="""Defines the temporary directory used between ctu
            phases.""",
        )
        ctu_mutex_group.add_argument(
            "--ctu-collect-only",
            action="store_const",
            const=CtuConfig(collect=True, analyze=False, dir="", extdef_map_cmd=""),
            dest="ctu_phases",
            help="""Perform only the collect phase of ctu.
            Keep <ctu-dir> for further use.""",
        )
        ctu_mutex_group.add_argument(
            "--ctu-analyze-only",
            action="store_const",
            const=CtuConfig(collect=False, analyze=True, dir="", extdef_map_cmd=""),
            dest="ctu_phases",
            help="""Perform only the analyze phase of ctu. <ctu-dir> should be
            present and will not be removed after analysis.""",
        )
        ctu.add_argument(
            "--use-extdef-map-cmd",
            metavar="<path>",
            dest="extdef_map_cmd",
            default="clang-extdef-mapping",
            help="""'%(prog)s' uses the 'clang-extdef-mapping' executable
            relative to itself for generating external definition maps for
            static analysis. One can override this behavior with this option
            by using the 'clang-extdef-mapping' packaged with Xcode (on OS X)
            or from the PATH.""",
        )
    return parser


def create_default_parser():
    """Creates command line parser for all build wrapper commands."""

    parser = argparse.ArgumentParser(
        formatter_class=argparse.ArgumentDefaultsHelpFormatter
    )

    parser.add_argument(
        "--verbose",
        "-v",
        action="count",
        default=0,
        help="""Enable verbose output from '%(prog)s'. A second, third and
        fourth flags increases verbosity.""",
    )
    return parser


def parser_add_cdb(parser):
    parser.add_argument(
        "--cdb",
        metavar="<file>",
        default="compile_commands.json",
        help="""The JSON compilation database.""",
    )


def parser_add_prefer_wrapper(parser):
    parser.add_argument(
        "--override-compiler",
        action="store_true",
        help="""Always resort to the compiler wrapper even when better
        intercept methods are available.""",
    )


def parser_add_compilers(parser):
    parser.add_argument(
        "--use-cc",
        metavar="<path>",
        dest="cc",
        default=os.getenv("CC", "cc"),
        help="""When '%(prog)s' analyzes a project by interposing a compiler
        wrapper, which executes a real compiler for compilation and do other
        tasks (record the compiler invocation). Because of this interposing,
        '%(prog)s' does not know what compiler your project normally uses.
        Instead, it simply overrides the CC environment variable, and guesses
        your default compiler.

        If you need '%(prog)s' to use a specific compiler for *compilation*
        then you can use this option to specify a path to that compiler.""",
    )
    parser.add_argument(
        "--use-c++",
        metavar="<path>",
        dest="cxx",
        default=os.getenv("CXX", "c++"),
        help="""This is the same as "--use-cc" but for C++ code.""",
    )


class AppendCommaSeparated(argparse.Action):
    """argparse Action class to support multiple comma separated lists."""

    def __call__(self, __parser, namespace, values, __option_string):
        # getattr(obj, attr, default) does not really returns default but none
        if getattr(namespace, self.dest, None) is None:
            setattr(namespace, self.dest, [])
        # once it's fixed we can use as expected
        actual = getattr(namespace, self.dest)
        actual.extend(values.split(","))
        setattr(namespace, self.dest, actual)


def print_active_checkers(checkers):
    """Print active checkers to stdout."""

    for name in sorted(name for name, (_, active) in checkers.items() if active):
        print(name)


def print_checkers(checkers):
    """Print verbose checker help to stdout."""

    print("")
    print("available checkers:")
    print("")
    for name in sorted(checkers.keys()):
        description, active = checkers[name]
        prefix = "+" if active else " "
        if len(name) > 30:
            print(" {0} {1}".format(prefix, name))
            print(" " * 35 + description)
        else:
            print(" {0} {1: <30}  {2}".format(prefix, name, description))
    print("")
    print('NOTE: "+" indicates that an analysis is enabled by default.')
    print("")
