# -*- coding: utf-8 -*-
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
""" This module implements the 'scan-build' command API.

To run the static analyzer against a build is done in multiple steps:

 -- Intercept: capture the compilation command during the build,
 -- Analyze:   run the analyzer against the captured commands,
 -- Report:    create a cover report from the analyzer outputs.  """

import re
import os
import os.path
import json
import logging
import multiprocessing
import tempfile
import functools
import subprocess
import contextlib
import datetime
import shutil
import glob
from collections import defaultdict

from libscanbuild import (
    command_entry_point,
    compiler_wrapper,
    wrapper_environment,
    run_build,
    run_command,
    CtuConfig,
)
from libscanbuild.arguments import (
    parse_args_for_scan_build,
    parse_args_for_analyze_build,
)
from libscanbuild.intercept import capture
from libscanbuild.report import document
from libscanbuild.compilation import split_command, classify_source, compiler_language
from libscanbuild.clang import (
    get_version,
    get_arguments,
    get_triple_arch,
    ClangErrorException,
)
from libscanbuild.shell import decode

__all__ = ["scan_build", "analyze_build", "analyze_compiler_wrapper"]

scanbuild_dir = os.path.dirname(os.path.realpath(__import__("sys").argv[0]))

COMPILER_WRAPPER_CC = os.path.join(scanbuild_dir, "..", "libexec", "analyze-cc")
COMPILER_WRAPPER_CXX = os.path.join(scanbuild_dir, "..", "libexec", "analyze-c++")

CTU_EXTDEF_MAP_FILENAME = "externalDefMap.txt"
CTU_TEMP_DEFMAP_FOLDER = "tmpExternalDefMaps"


@command_entry_point
def scan_build():
    """Entry point for scan-build command."""

    args = parse_args_for_scan_build()
    # will re-assign the report directory as new output
    with report_directory(
        args.output, args.keep_empty, args.output_format
    ) as args.output:
        # Run against a build command. there are cases, when analyzer run
        # is not required. But we need to set up everything for the
        # wrappers, because 'configure' needs to capture the CC/CXX values
        # for the Makefile.
        if args.intercept_first:
            # Run build command with intercept module.
            exit_code = capture(args)
            # Run the analyzer against the captured commands.
            if need_analyzer(args.build):
                govern_analyzer_runs(args)
        else:
            # Run build command and analyzer with compiler wrappers.
            environment = setup_environment(args)
            exit_code = run_build(args.build, env=environment)
        # Cover report generation and bug counting.
        number_of_bugs = document(args)
        # Set exit status as it was requested.
        return number_of_bugs if args.status_bugs else exit_code


@command_entry_point
def analyze_build():
    """Entry point for analyze-build command."""

    args = parse_args_for_analyze_build()
    # will re-assign the report directory as new output
    with report_directory(
        args.output, args.keep_empty, args.output_format
    ) as args.output:
        # Run the analyzer against a compilation db.
        govern_analyzer_runs(args)
        # Cover report generation and bug counting.
        number_of_bugs = document(args)
        # Set exit status as it was requested.
        return number_of_bugs if args.status_bugs else 0


def need_analyzer(args):
    """Check the intent of the build command.

    When static analyzer run against project configure step, it should be
    silent and no need to run the analyzer or generate report.

    To run `scan-build` against the configure step might be necessary,
    when compiler wrappers are used. That's the moment when build setup
    check the compiler and capture the location for the build process."""

    return len(args) and not re.search(r"configure|autogen", args[0])


def prefix_with(constant, pieces):
    """From a sequence create another sequence where every second element
    is from the original sequence and the odd elements are the prefix.

    eg.: prefix_with(0, [1,2,3]) creates [0, 1, 0, 2, 0, 3]"""

    return [elem for piece in pieces for elem in [constant, piece]]


def get_ctu_config_from_args(args):
    """CTU configuration is created from the chosen phases and dir."""

    return (
        CtuConfig(
            collect=args.ctu_phases.collect,
            analyze=args.ctu_phases.analyze,
            dir=args.ctu_dir,
            extdef_map_cmd=args.extdef_map_cmd,
        )
        if hasattr(args, "ctu_phases") and hasattr(args.ctu_phases, "dir")
        else CtuConfig(collect=False, analyze=False, dir="", extdef_map_cmd="")
    )


def get_ctu_config_from_json(ctu_conf_json):
    """CTU configuration is created from the chosen phases and dir."""

    ctu_config = json.loads(ctu_conf_json)
    # Recover namedtuple from json when coming from analyze-cc or analyze-c++
    return CtuConfig(
        collect=ctu_config[0],
        analyze=ctu_config[1],
        dir=ctu_config[2],
        extdef_map_cmd=ctu_config[3],
    )


def create_global_ctu_extdef_map(extdef_map_lines):
    """Takes iterator of individual external definition maps and creates a
    global map keeping only unique names. We leave conflicting names out of
    CTU.

    :param extdef_map_lines: Contains the id of a definition (mangled name) and
    the originating source (the corresponding AST file) name.
    :type extdef_map_lines: Iterator of str.
    :returns: Mangled name - AST file pairs.
    :rtype: List of (str, str) tuples.
    """

    mangled_to_asts = defaultdict(set)

    for line in extdef_map_lines:
        mangled_name, ast_file = line.strip().split(" ", 1)
        mangled_to_asts[mangled_name].add(ast_file)

    mangled_ast_pairs = []

    for mangled_name, ast_files in mangled_to_asts.items():
        if len(ast_files) == 1:
            mangled_ast_pairs.append((mangled_name, next(iter(ast_files))))

    return mangled_ast_pairs


def merge_ctu_extdef_maps(ctudir):
    """Merge individual external definition maps into a global one.

    As the collect phase runs parallel on multiple threads, all compilation
    units are separately mapped into a temporary file in CTU_TEMP_DEFMAP_FOLDER.
    These definition maps contain the mangled names and the source
    (AST generated from the source) which had their definition.
    These files should be merged at the end into a global map file:
    CTU_EXTDEF_MAP_FILENAME."""

    def generate_extdef_map_lines(extdefmap_dir):
        """Iterate over all lines of input files in a determined order."""

        files = glob.glob(os.path.join(extdefmap_dir, "*"))
        files.sort()
        for filename in files:
            with open(filename, "r") as in_file:
                for line in in_file:
                    yield line

    def write_global_map(arch, mangled_ast_pairs):
        """Write (mangled name, ast file) pairs into final file."""

        extern_defs_map_file = os.path.join(ctudir, arch, CTU_EXTDEF_MAP_FILENAME)
        with open(extern_defs_map_file, "w") as out_file:
            for mangled_name, ast_file in mangled_ast_pairs:
                out_file.write("%s %s\n" % (mangled_name, ast_file))

    triple_arches = glob.glob(os.path.join(ctudir, "*"))
    for triple_path in triple_arches:
        if os.path.isdir(triple_path):
            triple_arch = os.path.basename(triple_path)
            extdefmap_dir = os.path.join(ctudir, triple_arch, CTU_TEMP_DEFMAP_FOLDER)

            extdef_map_lines = generate_extdef_map_lines(extdefmap_dir)
            mangled_ast_pairs = create_global_ctu_extdef_map(extdef_map_lines)
            write_global_map(triple_arch, mangled_ast_pairs)

            # Remove all temporary files
            shutil.rmtree(extdefmap_dir, ignore_errors=True)


def run_analyzer_parallel(args):
    """Runs the analyzer against the given compilation database."""

    def exclude(filename, directory):
        """Return true when any excluded directory prefix the filename."""
        if not os.path.isabs(filename):
            # filename is either absolute or relative to directory. Need to turn
            # it to absolute since 'args.excludes' are absolute paths.
            filename = os.path.normpath(os.path.join(directory, filename))
        return any(
            re.match(r"^" + exclude_directory, filename)
            for exclude_directory in args.excludes
        )

    consts = {
        "clang": args.clang,
        "output_dir": args.output,
        "output_format": args.output_format,
        "output_failures": args.output_failures,
        "direct_args": analyzer_params(args),
        "force_debug": args.force_debug,
        "ctu": get_ctu_config_from_args(args),
    }

    logging.debug("run analyzer against compilation database")
    with open(args.cdb, "r") as handle:
        generator = (
            dict(cmd, **consts)
            for cmd in json.load(handle)
            if not exclude(cmd["file"], cmd["directory"])
        )
        # when verbose output requested execute sequentially
        pool = multiprocessing.Pool(1 if args.verbose > 2 else None)
        for current in pool.imap_unordered(run, generator):
            if current is not None:
                # display error message from the static analyzer
                for line in current["error_output"]:
                    logging.info(line.rstrip())
        pool.close()
        pool.join()


def govern_analyzer_runs(args):
    """Governs multiple runs in CTU mode or runs once in normal mode."""

    ctu_config = get_ctu_config_from_args(args)
    # If we do a CTU collect (1st phase) we remove all previous collection
    # data first.
    if ctu_config.collect:
        shutil.rmtree(ctu_config.dir, ignore_errors=True)

    # If the user asked for a collect (1st) and analyze (2nd) phase, we do an
    # all-in-one run where we deliberately remove collection data before and
    # also after the run. If the user asks only for a single phase data is
    # left so multiple analyze runs can use the same data gathered by a single
    # collection run.
    if ctu_config.collect and ctu_config.analyze:
        # CTU strings are coming from args.ctu_dir and extdef_map_cmd,
        # so we can leave it empty
        args.ctu_phases = CtuConfig(
            collect=True, analyze=False, dir="", extdef_map_cmd=""
        )
        run_analyzer_parallel(args)
        merge_ctu_extdef_maps(ctu_config.dir)
        args.ctu_phases = CtuConfig(
            collect=False, analyze=True, dir="", extdef_map_cmd=""
        )
        run_analyzer_parallel(args)
        shutil.rmtree(ctu_config.dir, ignore_errors=True)
    else:
        # Single runs (collect or analyze) are launched from here.
        run_analyzer_parallel(args)
        if ctu_config.collect:
            merge_ctu_extdef_maps(ctu_config.dir)


def setup_environment(args):
    """Set up environment for build command to interpose compiler wrapper."""

    environment = dict(os.environ)
    environment.update(wrapper_environment(args))
    environment.update(
        {
            "CC": COMPILER_WRAPPER_CC,
            "CXX": COMPILER_WRAPPER_CXX,
            "ANALYZE_BUILD_CLANG": args.clang if need_analyzer(args.build) else "",
            "ANALYZE_BUILD_REPORT_DIR": args.output,
            "ANALYZE_BUILD_REPORT_FORMAT": args.output_format,
            "ANALYZE_BUILD_REPORT_FAILURES": "yes" if args.output_failures else "",
            "ANALYZE_BUILD_PARAMETERS": " ".join(analyzer_params(args)),
            "ANALYZE_BUILD_FORCE_DEBUG": "yes" if args.force_debug else "",
            "ANALYZE_BUILD_CTU": json.dumps(get_ctu_config_from_args(args)),
        }
    )
    return environment


@command_entry_point
def analyze_compiler_wrapper():
    """Entry point for `analyze-cc` and `analyze-c++` compiler wrappers."""

    return compiler_wrapper(analyze_compiler_wrapper_impl)


def analyze_compiler_wrapper_impl(result, execution):
    """Implements analyzer compiler wrapper functionality."""

    # don't run analyzer when compilation fails. or when it's not requested.
    if result or not os.getenv("ANALYZE_BUILD_CLANG"):
        return

    # check is it a compilation?
    compilation = split_command(execution.cmd)
    if compilation is None:
        return
    # collect the needed parameters from environment, crash when missing
    parameters = {
        "clang": os.getenv("ANALYZE_BUILD_CLANG"),
        "output_dir": os.getenv("ANALYZE_BUILD_REPORT_DIR"),
        "output_format": os.getenv("ANALYZE_BUILD_REPORT_FORMAT"),
        "output_failures": os.getenv("ANALYZE_BUILD_REPORT_FAILURES"),
        "direct_args": os.getenv("ANALYZE_BUILD_PARAMETERS", "").split(" "),
        "force_debug": os.getenv("ANALYZE_BUILD_FORCE_DEBUG"),
        "directory": execution.cwd,
        "command": [execution.cmd[0], "-c"] + compilation.flags,
        "ctu": get_ctu_config_from_json(os.getenv("ANALYZE_BUILD_CTU")),
    }
    # call static analyzer against the compilation
    for source in compilation.files:
        parameters.update({"file": source})
        logging.debug("analyzer parameters %s", parameters)
        current = run(parameters)
        # display error message from the static analyzer
        if current is not None:
            for line in current["error_output"]:
                logging.info(line.rstrip())


@contextlib.contextmanager
def report_directory(hint, keep, output_format):
    """Responsible for the report directory.

    hint -- could specify the parent directory of the output directory.
    keep -- a boolean value to keep or delete the empty report directory."""

    stamp_format = "scan-build-%Y-%m-%d-%H-%M-%S-%f-"
    stamp = datetime.datetime.now().strftime(stamp_format)
    parent_dir = os.path.abspath(hint)
    if not os.path.exists(parent_dir):
        os.makedirs(parent_dir)
    name = tempfile.mkdtemp(prefix=stamp, dir=parent_dir)

    logging.info("Report directory created: %s", name)

    try:
        yield name
    finally:
        args = (name,)
        if os.listdir(name):
            if output_format not in ["sarif", "sarif-html"]:  # FIXME:
                # 'scan-view' currently does not support sarif format.
                msg = "Run 'scan-view %s' to examine bug reports."
            elif output_format == "sarif-html":
                msg = (
                    "Run 'scan-view %s' to examine bug reports or see "
                    "merged sarif results at %s/results-merged.sarif."
                )
                args = (name, name)
            else:
                msg = "View merged sarif results at %s/results-merged.sarif."
            keep = True
        else:
            if keep:
                msg = "Report directory '%s' contains no report, but kept."
            else:
                msg = "Removing directory '%s' because it contains no report."
        logging.warning(msg, *args)

        if not keep:
            os.rmdir(name)


def analyzer_params(args):
    """A group of command line arguments can mapped to command
    line arguments of the analyzer. This method generates those."""

    result = []

    if args.constraints_model:
        result.append("-analyzer-constraints={0}".format(args.constraints_model))
    if args.internal_stats:
        result.append("-analyzer-stats")
    if args.analyze_headers:
        result.append("-analyzer-opt-analyze-headers")
    if args.stats:
        result.append("-analyzer-checker=debug.Stats")
    if args.maxloop:
        result.extend(["-analyzer-max-loop", str(args.maxloop)])
    if args.output_format:
        result.append("-analyzer-output={0}".format(args.output_format))
    if args.analyzer_config:
        result.extend(["-analyzer-config", args.analyzer_config])
    if args.verbose >= 4:
        result.append("-analyzer-display-progress")
    if args.plugins:
        result.extend(prefix_with("-load", args.plugins))
    if args.enable_checker:
        checkers = ",".join(args.enable_checker)
        result.extend(["-analyzer-checker", checkers])
    if args.disable_checker:
        checkers = ",".join(args.disable_checker)
        result.extend(["-analyzer-disable-checker", checkers])

    return prefix_with("-Xclang", result)


def require(required):
    """Decorator for checking the required values in state.

    It checks the required attributes in the passed state and stop when
    any of those is missing."""

    def decorator(function):
        @functools.wraps(function)
        def wrapper(*args, **kwargs):
            for key in required:
                if key not in args[0]:
                    raise KeyError(
                        "{0} not passed to {1}".format(key, function.__name__)
                    )

            return function(*args, **kwargs)

        return wrapper

    return decorator


@require(
    [
        "command",  # entry from compilation database
        "directory",  # entry from compilation database
        "file",  # entry from compilation database
        "clang",  # clang executable name (and path)
        "direct_args",  # arguments from command line
        "force_debug",  # kill non debug macros
        "output_dir",  # where generated report files shall go
        "output_format",  # it's 'plist', 'html', 'plist-html', 'plist-multi-file', 'sarif', or 'sarif-html'
        "output_failures",  # generate crash reports or not
        "ctu",
    ]
)  # ctu control options
def run(opts):
    """Entry point to run (or not) static analyzer against a single entry
    of the compilation database.

    This complex task is decomposed into smaller methods which are calling
    each other in chain. If the analysis is not possible the given method
    just return and break the chain.

    The passed parameter is a python dictionary. Each method first check
    that the needed parameters received. (This is done by the 'require'
    decorator. It's like an 'assert' to check the contract between the
    caller and the called method.)"""

    try:
        command = opts.pop("command")
        command = command if isinstance(command, list) else decode(command)
        logging.debug("Run analyzer against '%s'", command)
        opts.update(classify_parameters(command))

        return arch_check(opts)
    except Exception:
        logging.error("Problem occurred during analysis.", exc_info=1)
        return None


@require(
    [
        "clang",
        "directory",
        "flags",
        "file",
        "output_dir",
        "language",
        "error_output",
        "exit_code",
    ]
)
def report_failure(opts):
    """Create report when analyzer failed.

    The major report is the preprocessor output. The output filename generated
    randomly. The compiler output also captured into '.stderr.txt' file.
    And some more execution context also saved into '.info.txt' file."""

    def extension():
        """Generate preprocessor file extension."""

        mapping = {"objective-c++": ".mii", "objective-c": ".mi", "c++": ".ii"}
        return mapping.get(opts["language"], ".i")

    def destination():
        """Creates failures directory if not exits yet."""

        failures_dir = os.path.join(opts["output_dir"], "failures")
        if not os.path.isdir(failures_dir):
            os.makedirs(failures_dir)
        return failures_dir

    # Classify error type: when Clang terminated by a signal it's a 'Crash'.
    # (python subprocess Popen.returncode is negative when child terminated
    # by signal.) Everything else is 'Other Error'.
    error = "crash" if opts["exit_code"] < 0 else "other_error"
    # Create preprocessor output file name. (This is blindly following the
    # Perl implementation.)
    (handle, name) = tempfile.mkstemp(
        suffix=extension(), prefix="clang_" + error + "_", dir=destination()
    )
    os.close(handle)
    # Execute Clang again, but run the syntax check only.
    cwd = opts["directory"]
    cmd = (
        [opts["clang"], "-fsyntax-only", "-E"]
        + opts["flags"]
        + [opts["file"], "-o", name]
    )
    try:
        cmd = get_arguments(cmd, cwd)
        run_command(cmd, cwd=cwd)
    except subprocess.CalledProcessError:
        pass
    except ClangErrorException:
        pass
    # write general information about the crash
    with open(name + ".info.txt", "w") as handle:
        handle.write(opts["file"] + os.linesep)
        handle.write(error.title().replace("_", " ") + os.linesep)
        handle.write(" ".join(cmd) + os.linesep)
        handle.write(" ".join(os.uname()) + os.linesep)
        handle.write(get_version(opts["clang"]))
        handle.close()
    # write the captured output too
    with open(name + ".stderr.txt", "w") as handle:
        handle.writelines(opts["error_output"])
        handle.close()


@require(
    [
        "clang",
        "directory",
        "flags",
        "direct_args",
        "file",
        "output_dir",
        "output_format",
    ]
)
def run_analyzer(opts, continuation=report_failure):
    """It assembles the analysis command line and executes it. Capture the
    output of the analysis and returns with it. If failure reports are
    requested, it calls the continuation to generate it."""

    def target():
        """Creates output file name for reports."""
        if opts["output_format"] in {"plist", "plist-html", "plist-multi-file"}:
            (handle, name) = tempfile.mkstemp(
                prefix="report-", suffix=".plist", dir=opts["output_dir"]
            )
            os.close(handle)
            return name
        elif opts["output_format"] in {"sarif", "sarif-html"}:
            (handle, name) = tempfile.mkstemp(
                prefix="result-", suffix=".sarif", dir=opts["output_dir"]
            )
            os.close(handle)
            return name
        return opts["output_dir"]

    try:
        cwd = opts["directory"]
        cmd = get_arguments(
            [opts["clang"], "--analyze"]
            + opts["direct_args"]
            + opts["flags"]
            + [opts["file"], "-o", target()],
            cwd,
        )
        output = run_command(cmd, cwd=cwd)
        return {"error_output": output, "exit_code": 0}
    except subprocess.CalledProcessError as ex:
        result = {"error_output": ex.output, "exit_code": ex.returncode}
        if opts.get("output_failures", False):
            opts.update(result)
            continuation(opts)
        return result
    except ClangErrorException as ex:
        result = {"error_output": ex.error, "exit_code": 0}
        if opts.get("output_failures", False):
            opts.update(result)
            continuation(opts)
        return result


def extdef_map_list_src_to_ast(extdef_src_list):
    """Turns textual external definition map list with source files into an
    external definition map list with ast files."""

    extdef_ast_list = []
    for extdef_src_txt in extdef_src_list:
        mangled_name, path = extdef_src_txt.split(" ", 1)
        # Normalize path on windows as well
        path = os.path.splitdrive(path)[1]
        # Make relative path out of absolute
        path = path[1:] if path[0] == os.sep else path
        ast_path = os.path.join("ast", path + ".ast")
        extdef_ast_list.append(mangled_name + " " + ast_path)
    return extdef_ast_list


@require(["clang", "directory", "flags", "direct_args", "file", "ctu"])
def ctu_collect_phase(opts):
    """Preprocess source by generating all data needed by CTU analysis."""

    def generate_ast(triple_arch):
        """Generates ASTs for the current compilation command."""

        args = opts["direct_args"] + opts["flags"]
        ast_joined_path = os.path.join(
            opts["ctu"].dir,
            triple_arch,
            "ast",
            os.path.realpath(opts["file"])[1:] + ".ast",
        )
        ast_path = os.path.abspath(ast_joined_path)
        ast_dir = os.path.dirname(ast_path)
        if not os.path.isdir(ast_dir):
            try:
                os.makedirs(ast_dir)
            except OSError:
                # In case an other process already created it.
                pass
        ast_command = [opts["clang"], "-emit-ast"]
        ast_command.extend(args)
        ast_command.append("-w")
        ast_command.append(opts["file"])
        ast_command.append("-o")
        ast_command.append(ast_path)
        logging.debug("Generating AST using '%s'", ast_command)
        run_command(ast_command, cwd=opts["directory"])

    def map_extdefs(triple_arch):
        """Generate external definition map file for the current source."""

        args = opts["direct_args"] + opts["flags"]
        extdefmap_command = [opts["ctu"].extdef_map_cmd]
        extdefmap_command.append(opts["file"])
        extdefmap_command.append("--")
        extdefmap_command.extend(args)
        logging.debug(
            "Generating external definition map using '%s'", extdefmap_command
        )
        extdef_src_list = run_command(extdefmap_command, cwd=opts["directory"])
        extdef_ast_list = extdef_map_list_src_to_ast(extdef_src_list)
        extern_defs_map_folder = os.path.join(
            opts["ctu"].dir, triple_arch, CTU_TEMP_DEFMAP_FOLDER
        )
        if not os.path.isdir(extern_defs_map_folder):
            try:
                os.makedirs(extern_defs_map_folder)
            except OSError:
                # In case an other process already created it.
                pass
        if extdef_ast_list:
            with tempfile.NamedTemporaryFile(
                mode="w", dir=extern_defs_map_folder, delete=False
            ) as out_file:
                out_file.write("\n".join(extdef_ast_list) + "\n")

    cwd = opts["directory"]
    cmd = (
        [opts["clang"], "--analyze"]
        + opts["direct_args"]
        + opts["flags"]
        + [opts["file"]]
    )
    triple_arch = get_triple_arch(cmd, cwd)
    generate_ast(triple_arch)
    map_extdefs(triple_arch)


@require(["ctu"])
def dispatch_ctu(opts, continuation=run_analyzer):
    """Execute only one phase of 2 phases of CTU if needed."""

    ctu_config = opts["ctu"]

    if ctu_config.collect or ctu_config.analyze:
        assert ctu_config.collect != ctu_config.analyze
        if ctu_config.collect:
            return ctu_collect_phase(opts)
        if ctu_config.analyze:
            cwd = opts["directory"]
            cmd = (
                [opts["clang"], "--analyze"]
                + opts["direct_args"]
                + opts["flags"]
                + [opts["file"]]
            )
            triarch = get_triple_arch(cmd, cwd)
            ctu_options = [
                "ctu-dir=" + os.path.join(ctu_config.dir, triarch),
                "experimental-enable-naive-ctu-analysis=true",
            ]
            analyzer_options = prefix_with("-analyzer-config", ctu_options)
            direct_options = prefix_with("-Xanalyzer", analyzer_options)
            opts["direct_args"].extend(direct_options)

    return continuation(opts)


@require(["flags", "force_debug"])
def filter_debug_flags(opts, continuation=dispatch_ctu):
    """Filter out nondebug macros when requested."""

    if opts.pop("force_debug"):
        # lazy implementation just append an undefine macro at the end
        opts.update({"flags": opts["flags"] + ["-UNDEBUG"]})

    return continuation(opts)


@require(["language", "compiler", "file", "flags"])
def language_check(opts, continuation=filter_debug_flags):
    """Find out the language from command line parameters or file name
    extension. The decision also influenced by the compiler invocation."""

    accepted = frozenset(
        {
            "c",
            "c++",
            "objective-c",
            "objective-c++",
            "c-cpp-output",
            "c++-cpp-output",
            "objective-c-cpp-output",
        }
    )

    # language can be given as a parameter...
    language = opts.pop("language")
    compiler = opts.pop("compiler")
    # ... or find out from source file extension
    if language is None and compiler is not None:
        language = classify_source(opts["file"], compiler == "c")

    if language is None:
        logging.debug("skip analysis, language not known")
        return None
    elif language not in accepted:
        logging.debug("skip analysis, language not supported")
        return None
    else:
        logging.debug("analysis, language: %s", language)
        opts.update({"language": language, "flags": ["-x", language] + opts["flags"]})
        return continuation(opts)


@require(["arch_list", "flags"])
def arch_check(opts, continuation=language_check):
    """Do run analyzer through one of the given architectures."""

    disabled = frozenset({"ppc", "ppc64"})

    received_list = opts.pop("arch_list")
    if received_list:
        # filter out disabled architectures and -arch switches
        filtered_list = [a for a in received_list if a not in disabled]
        if filtered_list:
            # There should be only one arch given (or the same multiple
            # times). If there are multiple arch are given and are not
            # the same, those should not change the pre-processing step.
            # But that's the only pass we have before run the analyzer.
            current = filtered_list.pop()
            logging.debug("analysis, on arch: %s", current)

            opts.update({"flags": ["-arch", current] + opts["flags"]})
            return continuation(opts)
        else:
            logging.debug("skip analysis, found not supported arch")
            return None
    else:
        logging.debug("analysis, on default arch")
        return continuation(opts)


# To have good results from static analyzer certain compiler options shall be
# omitted. The compiler flag filtering only affects the static analyzer run.
#
# Keys are the option name, value number of options to skip
IGNORED_FLAGS = {
    "-c": 0,  # compile option will be overwritten
    "-fsyntax-only": 0,  # static analyzer option will be overwritten
    "-o": 1,  # will set up own output file
    # flags below are inherited from the perl implementation.
    "-g": 0,
    "-save-temps": 0,
    "-install_name": 1,
    "-exported_symbols_list": 1,
    "-current_version": 1,
    "-compatibility_version": 1,
    "-init": 1,
    "-e": 1,
    "-seg1addr": 1,
    "-bundle_loader": 1,
    "-multiply_defined": 1,
    "-sectorder": 3,
    "--param": 1,
    "--serialize-diagnostics": 1,
}


def classify_parameters(command):
    """Prepare compiler flags (filters some and add others) and take out
    language (-x) and architecture (-arch) flags for future processing."""

    result = {
        "flags": [],  # the filtered compiler flags
        "arch_list": [],  # list of architecture flags
        "language": None,  # compilation language, None, if not specified
        "compiler": compiler_language(command),  # 'c' or 'c++'
    }

    # iterate on the compile options
    args = iter(command[1:])
    for arg in args:
        # take arch flags into a separate basket
        if arg == "-arch":
            result["arch_list"].append(next(args))
        # take language
        elif arg == "-x":
            result["language"] = next(args)
        # parameters which looks source file are not flags
        elif re.match(r"^[^-].+", arg) and classify_source(arg):
            pass
        # ignore some flags
        elif arg in IGNORED_FLAGS:
            count = IGNORED_FLAGS[arg]
            for _ in range(count):
                next(args)
        # we don't care about extra warnings, but we should suppress ones
        # that we don't want to see.
        elif re.match(r"^-W.+", arg) and not re.match(r"^-Wno-.+", arg):
            pass
        # and consider everything else as compilation flag.
        else:
            result["flags"].append(arg)

    return result
