#!/usr/bin/env python

"""
Static Analyzer qualification infrastructure.

The goal is to test the analyzer against different projects,
check for failures, compare results, and measure performance.

Repository Directory will contain sources of the projects as well as the
information on how to build them and the expected output.
Repository Directory structure:
   - ProjectMap file
   - Historical Performance Data
   - Project Dir1
     - ReferenceOutput
   - Project Dir2
     - ReferenceOutput
   ..
Note that the build tree must be inside the project dir.

To test the build of the analyzer one would:
   - Copy over a copy of the Repository Directory. (TODO: Prefer to ensure that
     the build directory does not pollute the repository to min network
     traffic).
   - Build all projects, until error. Produce logs to report errors.
   - Compare results.

The files which should be kept around for failure investigations:
   RepositoryCopy/Project DirI/ScanBuildResults
   RepositoryCopy/Project DirI/run_static_analyzer.log

Assumptions (TODO: shouldn't need to assume these.):
   The script is being run from the Repository Directory.
   The compiler for scan-build and scan-build are in the PATH.
   export PATH=/Users/zaks/workspace/c2llvm/build/Release+Asserts/bin:$PATH

For more logging, set the  env variables:
   zaks:TI zaks$ export CCC_ANALYZER_LOG=1
   zaks:TI zaks$ export CCC_ANALYZER_VERBOSE=1

The list of checkers tested are hardcoded in the Checkers variable.
For testing additional checkers, use the SA_ADDITIONAL_CHECKERS environment
variable. It should contain a comma separated list.
"""
import CmpRuns
import SATestUtils as utils
from ProjectMap import DownloadType, ProjectInfo

import glob
import logging
import math
import multiprocessing
import os
import plistlib
import shutil
import sys
import threading
import time
import zipfile

from queue import Queue

# mypy has problems finding InvalidFileException in the module
# and this is we can shush that false positive
from plistlib import InvalidFileException  # type:ignore
from subprocess import CalledProcessError, check_call
from typing import Dict, IO, List, NamedTuple, Optional, TYPE_CHECKING, Tuple


###############################################################################
# Helper functions.
###############################################################################


class StreamToLogger:
    def __init__(self, logger: logging.Logger, log_level: int = logging.INFO):
        self.logger = logger
        self.log_level = log_level

    def write(self, message: str):
        # Rstrip in order not to write an extra newline.
        self.logger.log(self.log_level, message.rstrip())

    def flush(self):
        pass

    def fileno(self) -> int:
        return 0


LOCAL = threading.local()


def init_logger(name: str):
    # TODO: use debug levels for VERBOSE messages
    logger = logging.getLogger(name)
    logger.setLevel(logging.DEBUG)
    LOCAL.stdout = StreamToLogger(logger, logging.INFO)
    LOCAL.stderr = StreamToLogger(logger, logging.ERROR)


init_logger("main")


def stderr(message: str):
    LOCAL.stderr.write(message)


def stdout(message: str):
    LOCAL.stdout.write(message)


logging.basicConfig(format="%(asctime)s:%(levelname)s:%(name)s: %(message)s")


###############################################################################
# Configuration setup.
###############################################################################


# Find Clang for static analysis.
if "CC" in os.environ:
    cc_candidate: Optional[str] = os.environ["CC"]
else:
    cc_candidate = utils.which("clang", os.environ["PATH"])
if not cc_candidate:
    stderr("Error: cannot find 'clang' in PATH")
    sys.exit(1)

CLANG = cc_candidate

# Number of jobs.
MAX_JOBS = int(math.ceil(multiprocessing.cpu_count() * 0.75))

# Names of the project specific scripts.
# The script that downloads the project.
DOWNLOAD_SCRIPT = "download_project.sh"
# The script that needs to be executed before the build can start.
CLEANUP_SCRIPT = "cleanup_run_static_analyzer.sh"
# This is a file containing commands for scan-build.
BUILD_SCRIPT = "run_static_analyzer.cmd"

# A comment in a build script which disables wrapping.
NO_PREFIX_CMD = "#NOPREFIX"

# The log file name.
LOG_DIR_NAME = "Logs"
BUILD_LOG_NAME = "run_static_analyzer.log"
# Summary file - contains the summary of the failures. Ex: This info can be be
# displayed when buildbot detects a build failure.
NUM_OF_FAILURES_IN_SUMMARY = 10

# The scan-build result directory.
OUTPUT_DIR_NAME = "ScanBuildResults"
REF_PREFIX = "Ref"

# The name of the directory storing the cached project source. If this
# directory does not exist, the download script will be executed.
# That script should create the "CachedSource" directory and download the
# project source into it.
CACHED_SOURCE_DIR_NAME = "CachedSource"

# The name of the directory containing the source code that will be analyzed.
# Each time a project is analyzed, a fresh copy of its CachedSource directory
# will be copied to the PatchedSource directory and then the local patches
# in PATCHFILE_NAME will be applied (if PATCHFILE_NAME exists).
PATCHED_SOURCE_DIR_NAME = "PatchedSource"

# The name of the patchfile specifying any changes that should be applied
# to the CachedSource before analyzing.
PATCHFILE_NAME = "changes_for_analyzer.patch"

# The list of checkers used during analyzes.
# Currently, consists of all the non-experimental checkers, plus a few alpha
# checkers we don't want to regress on.
CHECKERS = ",".join(
    [
        "alpha.unix.SimpleStream",
        "alpha.security.taint",
        "cplusplus.NewDeleteLeaks",
        "core",
        "cplusplus",
        "deadcode",
        "security",
        "unix",
        "osx",
        "nullability",
    ]
)

VERBOSE = 0


###############################################################################
# Test harness logic.
###############################################################################


def run_cleanup_script(directory: str, build_log_file: IO):
    """
    Run pre-processing script if any.
    """
    cwd = os.path.join(directory, PATCHED_SOURCE_DIR_NAME)
    script_path = os.path.join(directory, CLEANUP_SCRIPT)

    utils.run_script(
        script_path,
        build_log_file,
        cwd,
        out=LOCAL.stdout,
        err=LOCAL.stderr,
        verbose=VERBOSE,
    )


class TestInfo(NamedTuple):
    """
    Information about a project and settings for its analysis.
    """

    project: ProjectInfo
    override_compiler: bool = False
    extra_analyzer_config: str = ""
    extra_checkers: str = ""
    is_reference_build: bool = False
    strictness: int = 0


# typing package doesn't have a separate type for Queue, but has a generic stub
# We still want to have a type-safe checked project queue, for this reason,
# we specify generic type for mypy.
#
# It is a common workaround for this situation:
# https://mypy.readthedocs.io/en/stable/common_issues.html#using-classes-that-are-generic-in-stubs-but-not-at-runtime
if TYPE_CHECKING:
    TestQueue = Queue[TestInfo]  # this is only processed by mypy
else:
    TestQueue = Queue  # this will be executed at runtime


class RegressionTester:
    """
    A component aggregating all of the project testing.
    """

    def __init__(
        self,
        jobs: int,
        projects: List[ProjectInfo],
        override_compiler: bool,
        extra_analyzer_config: str,
        extra_checkers: str,
        regenerate: bool,
        strictness: bool,
    ):
        self.jobs = jobs
        self.projects = projects
        self.override_compiler = override_compiler
        self.extra_analyzer_config = extra_analyzer_config
        self.extra_checkers = extra_checkers
        self.regenerate = regenerate
        self.strictness = strictness

    def test_all(self) -> bool:
        projects_to_test: List[TestInfo] = []

        # Test the projects.
        for project in self.projects:
            projects_to_test.append(
                TestInfo(
                    project,
                    self.override_compiler,
                    self.extra_analyzer_config,
                    self.extra_checkers,
                    self.regenerate,
                    self.strictness,
                )
            )
        if self.jobs <= 1:
            return self._single_threaded_test_all(projects_to_test)
        else:
            return self._multi_threaded_test_all(projects_to_test)

    def _single_threaded_test_all(self, projects_to_test: List[TestInfo]) -> bool:
        """
        Run all projects.
        :return: whether tests have passed.
        """
        success = True
        for project_info in projects_to_test:
            tester = ProjectTester(project_info)
            success &= tester.test()
        return success

    def _multi_threaded_test_all(self, projects_to_test: List[TestInfo]) -> bool:
        """
        Run each project in a separate thread.

        This is OK despite GIL, as testing is blocked
        on launching external processes.

        :return: whether tests have passed.
        """
        tasks_queue = TestQueue()

        for project_info in projects_to_test:
            tasks_queue.put(project_info)

        results_differ = threading.Event()
        failure_flag = threading.Event()

        for _ in range(self.jobs):
            T = TestProjectThread(tasks_queue, results_differ, failure_flag)
            T.start()

        # Required to handle Ctrl-C gracefully.
        while tasks_queue.unfinished_tasks:
            time.sleep(0.1)  # Seconds.
            if failure_flag.is_set():
                stderr("Test runner crashed\n")
                sys.exit(1)
        return not results_differ.is_set()


class ProjectTester:
    """
    A component aggregating testing for one project.
    """

    def __init__(self, test_info: TestInfo, silent: bool = False):
        self.project = test_info.project
        self.override_compiler = test_info.override_compiler
        self.extra_analyzer_config = test_info.extra_analyzer_config
        self.extra_checkers = test_info.extra_checkers
        self.is_reference_build = test_info.is_reference_build
        self.strictness = test_info.strictness
        self.silent = silent

    def test(self) -> bool:
        """
        Test a given project.
        :return tests_passed: Whether tests have passed according
        to the :param strictness: criteria.
        """
        if not self.project.enabled:
            self.out(f" \n\n--- Skipping disabled project {self.project.name}\n")
            return True

        self.out(f" \n\n--- Building project {self.project.name}\n")

        start_time = time.time()

        project_dir = self.get_project_dir()
        self.vout(f"  Build directory: {project_dir}.\n")

        # Set the build results directory.
        output_dir = self.get_output_dir()

        self.build(project_dir, output_dir)
        check_build(output_dir)

        if self.is_reference_build:
            cleanup_reference_results(output_dir)
            passed = True
        else:
            passed = run_cmp_results(project_dir, self.strictness)

        self.out(
            f"Completed tests for project {self.project.name} "
            f"(time: {time.time() - start_time:.2f}).\n"
        )

        return passed

    def get_project_dir(self) -> str:
        return os.path.join(os.path.abspath(os.curdir), self.project.name)

    def get_output_dir(self) -> str:
        if self.is_reference_build:
            dirname = REF_PREFIX + OUTPUT_DIR_NAME
        else:
            dirname = OUTPUT_DIR_NAME

        return os.path.join(self.get_project_dir(), dirname)

    def build(self, directory: str, output_dir: str) -> Tuple[float, int]:
        build_log_path = get_build_log_path(output_dir)

        self.out(f"Log file: {build_log_path}\n")
        self.out(f"Output directory: {output_dir}\n")

        remove_log_file(output_dir)

        # Clean up scan build results.
        if os.path.exists(output_dir):
            self.vout(f"  Removing old results: {output_dir}\n")

            shutil.rmtree(output_dir)

        assert not os.path.exists(output_dir)
        os.makedirs(os.path.join(output_dir, LOG_DIR_NAME))

        # Build and analyze the project.
        with open(build_log_path, "w+") as build_log_file:
            if self.project.mode == 1:
                self._download_and_patch(directory, build_log_file)
                run_cleanup_script(directory, build_log_file)
                build_time, memory = self.scan_build(
                    directory, output_dir, build_log_file
                )
            else:
                build_time, memory = self.analyze_preprocessed(directory, output_dir)

            if self.is_reference_build:
                run_cleanup_script(directory, build_log_file)
                normalize_reference_results(directory, output_dir, self.project.mode)

        self.out(
            f"Build complete (time: {utils.time_to_str(build_time)}, "
            f"peak memory: {utils.memory_to_str(memory)}). "
            f"See the log for more details: {build_log_path}\n"
        )

        return build_time, memory

    def scan_build(
        self, directory: str, output_dir: str, build_log_file: IO
    ) -> Tuple[float, int]:
        """
        Build the project with scan-build by reading in the commands and
        prefixing them with the scan-build options.
        """
        build_script_path = os.path.join(directory, BUILD_SCRIPT)
        if not os.path.exists(build_script_path):
            stderr(f"Error: build script is not defined: " f"{build_script_path}\n")
            sys.exit(1)

        all_checkers = CHECKERS
        if "SA_ADDITIONAL_CHECKERS" in os.environ:
            all_checkers = all_checkers + "," + os.environ["SA_ADDITIONAL_CHECKERS"]
        if self.extra_checkers != "":
            all_checkers += "," + self.extra_checkers

        # Run scan-build from within the patched source directory.
        cwd = os.path.join(directory, PATCHED_SOURCE_DIR_NAME)

        options = f"--use-analyzer '{CLANG}' "
        options += f"-plist-html -o '{output_dir}' "
        options += f"-enable-checker {all_checkers} "
        options += "--keep-empty "
        options += f"-analyzer-config '{self.generate_config()}' "

        if self.override_compiler:
            options += "--override-compiler "

        extra_env: Dict[str, str] = {}

        execution_time = 0.0
        peak_memory = 0

        try:
            command_file = open(build_script_path, "r")
            command_prefix = "scan-build " + options + " "

            for command in command_file:
                command = command.strip()

                if len(command) == 0:
                    continue

                # Custom analyzer invocation specified by project.
                # Communicate required information using environment variables
                # instead.
                if command == NO_PREFIX_CMD:
                    command_prefix = ""
                    extra_env["OUTPUT"] = output_dir
                    extra_env["CC"] = CLANG
                    extra_env["ANALYZER_CONFIG"] = self.generate_config()
                    continue

                if command.startswith("#"):
                    continue

                # If using 'make', auto imply a -jX argument
                # to speed up analysis.  xcodebuild will
                # automatically use the maximum number of cores.
                if (
                    command.startswith("make ") or command == "make"
                ) and "-j" not in command:
                    command += f" -j{MAX_JOBS}"

                command_to_run = command_prefix + command

                self.vout(f"  Executing: {command_to_run}\n")

                time, mem = utils.check_and_measure_call(
                    command_to_run,
                    cwd=cwd,
                    stderr=build_log_file,
                    stdout=build_log_file,
                    env=dict(os.environ, **extra_env),
                    shell=True,
                )

                execution_time += time
                peak_memory = max(peak_memory, mem)

        except CalledProcessError:
            stderr("Error: scan-build failed. Its output was: \n")
            build_log_file.seek(0)
            shutil.copyfileobj(build_log_file, LOCAL.stderr)
            sys.exit(1)

        return execution_time, peak_memory

    def analyze_preprocessed(
        self, directory: str, output_dir: str
    ) -> Tuple[float, int]:
        """
        Run analysis on a set of preprocessed files.
        """
        if os.path.exists(os.path.join(directory, BUILD_SCRIPT)):
            stderr(
                f"Error: The preprocessed files project "
                f"should not contain {BUILD_SCRIPT}\n"
            )
            raise Exception()

        prefix = CLANG + " --analyze "

        prefix += "--analyzer-output plist "
        prefix += " -Xclang -analyzer-checker=" + CHECKERS
        prefix += " -fcxx-exceptions -fblocks "
        prefix += " -Xclang -analyzer-config "
        prefix += f"-Xclang {self.generate_config()} "

        if self.project.mode == 2:
            prefix += "-std=c++11 "

        plist_path = os.path.join(directory, output_dir, "date")
        fail_path = os.path.join(plist_path, "failures")
        os.makedirs(fail_path)

        execution_time = 0.0
        peak_memory = 0

        for full_file_name in glob.glob(directory + "/*"):
            file_name = os.path.basename(full_file_name)
            failed = False

            # Only run the analyzes on supported files.
            if utils.has_no_extension(file_name):
                continue
            if not utils.is_valid_single_input_file(file_name):
                stderr(f"Error: Invalid single input file {full_file_name}.\n")
                raise Exception()

            # Build and call the analyzer command.
            plist_basename = os.path.join(plist_path, file_name)
            output_option = f"-o '{plist_basename}.plist' "
            command = f"{prefix}{output_option}'{file_name}'"

            log_path = os.path.join(fail_path, file_name + ".stderr.txt")
            with open(log_path, "w+") as log_file:
                try:
                    self.vout(f"  Executing: {command}\n")

                    time, mem = utils.check_and_measure_call(
                        command,
                        cwd=directory,
                        stderr=log_file,
                        stdout=log_file,
                        shell=True,
                    )

                    execution_time += time
                    peak_memory = max(peak_memory, mem)

                except CalledProcessError as e:
                    stderr(
                        f"Error: Analyzes of {full_file_name} failed. "
                        f"See {log_file.name} for details. "
                        f"Error code {e.returncode}.\n"
                    )
                    failed = True

                # If command did not fail, erase the log file.
                if not failed:
                    os.remove(log_file.name)

        return execution_time, peak_memory

    def generate_config(self) -> str:
        out = "serialize-stats=true,stable-report-filename=true"

        if self.extra_analyzer_config:
            out += "," + self.extra_analyzer_config

        return out

    def _download_and_patch(self, directory: str, build_log_file: IO):
        """
        Download the project and apply the local patchfile if it exists.
        """
        cached_source = os.path.join(directory, CACHED_SOURCE_DIR_NAME)

        # If the we don't already have the cached source, run the project's
        # download script to download it.
        if not os.path.exists(cached_source):
            self._download(directory, build_log_file)
            if not os.path.exists(cached_source):
                stderr(f"Error: '{cached_source}' not found after download.\n")
                exit(1)

        patched_source = os.path.join(directory, PATCHED_SOURCE_DIR_NAME)

        # Remove potentially stale patched source.
        if os.path.exists(patched_source):
            shutil.rmtree(patched_source)

        # Copy the cached source and apply any patches to the copy.
        shutil.copytree(cached_source, patched_source, symlinks=True)
        self._apply_patch(directory, build_log_file)

    def _download(self, directory: str, build_log_file: IO):
        """
        Run the script to download the project, if it exists.
        """
        if self.project.source == DownloadType.GIT:
            self._download_from_git(directory, build_log_file)
        elif self.project.source == DownloadType.ZIP:
            self._unpack_zip(directory, build_log_file)
        elif self.project.source == DownloadType.SCRIPT:
            self._run_download_script(directory, build_log_file)
        else:
            raise ValueError(
                f"Unknown source type '{self.project.source}' is found "
                f"for the '{self.project.name}' project"
            )

    def _download_from_git(self, directory: str, build_log_file: IO):
        repo = self.project.origin
        cached_source = os.path.join(directory, CACHED_SOURCE_DIR_NAME)

        check_call(
            f"git clone --recursive {repo} {cached_source}",
            cwd=directory,
            stderr=build_log_file,
            stdout=build_log_file,
            shell=True,
        )
        check_call(
            f"git checkout --quiet {self.project.commit}",
            cwd=cached_source,
            stderr=build_log_file,
            stdout=build_log_file,
            shell=True,
        )

    def _unpack_zip(self, directory: str, build_log_file: IO):
        zip_files = list(glob.glob(directory + "/*.zip"))

        if len(zip_files) == 0:
            raise ValueError(
                f"Couldn't find any zip files to unpack for the "
                f"'{self.project.name}' project"
            )

        if len(zip_files) > 1:
            raise ValueError(
                f"Couldn't decide which of the zip files ({zip_files}) "
                f"for the '{self.project.name}' project to unpack"
            )

        with zipfile.ZipFile(zip_files[0], "r") as zip_file:
            zip_file.extractall(os.path.join(directory, CACHED_SOURCE_DIR_NAME))

    @staticmethod
    def _run_download_script(directory: str, build_log_file: IO):
        script_path = os.path.join(directory, DOWNLOAD_SCRIPT)
        utils.run_script(
            script_path,
            build_log_file,
            directory,
            out=LOCAL.stdout,
            err=LOCAL.stderr,
            verbose=VERBOSE,
        )

    def _apply_patch(self, directory: str, build_log_file: IO):
        patchfile_path = os.path.join(directory, PATCHFILE_NAME)
        patched_source = os.path.join(directory, PATCHED_SOURCE_DIR_NAME)

        if not os.path.exists(patchfile_path):
            self.out("  No local patches.\n")
            return

        self.out("  Applying patch.\n")
        try:
            check_call(
                f"patch -p1 < '{patchfile_path}'",
                cwd=patched_source,
                stderr=build_log_file,
                stdout=build_log_file,
                shell=True,
            )

        except CalledProcessError:
            stderr(f"Error: Patch failed. " f"See {build_log_file.name} for details.\n")
            sys.exit(1)

    def out(self, what: str):
        if not self.silent:
            stdout(what)

    def vout(self, what: str):
        if VERBOSE >= 1:
            self.out(what)


class TestProjectThread(threading.Thread):
    def __init__(
        self,
        tasks_queue: TestQueue,
        results_differ: threading.Event,
        failure_flag: threading.Event,
    ):
        """
        :param results_differ: Used to signify that results differ from
               the canonical ones.
        :param failure_flag: Used to signify a failure during the run.
        """
        self.tasks_queue = tasks_queue
        self.results_differ = results_differ
        self.failure_flag = failure_flag
        super().__init__()

        # Needed to gracefully handle interrupts with Ctrl-C
        self.daemon = True

    def run(self):
        while not self.tasks_queue.empty():
            try:
                test_info = self.tasks_queue.get()
                init_logger(test_info.project.name)

                tester = ProjectTester(test_info)
                if not tester.test():
                    self.results_differ.set()

                self.tasks_queue.task_done()

            except BaseException:
                self.failure_flag.set()
                raise


###############################################################################
# Utility functions.
###############################################################################


def check_build(output_dir: str):
    """
    Given the scan-build output directory, checks if the build failed
    (by searching for the failures directories). If there are failures, it
    creates a summary file in the output directory.

    """
    # Check if there are failures.
    failures = glob.glob(output_dir + "/*/failures/*.stderr.txt")
    total_failed = len(failures)

    if total_failed == 0:
        clean_up_empty_plists(output_dir)
        clean_up_empty_folders(output_dir)

        plists = glob.glob(output_dir + "/*/*.plist")
        stdout(
            f"Number of bug reports "
            f"(non-empty plist files) produced: {len(plists)}\n"
        )
        return

    stderr("Error: analysis failed.\n")
    stderr(f"Total of {total_failed} failures discovered.\n")

    if total_failed > NUM_OF_FAILURES_IN_SUMMARY:
        stderr(f"See the first {NUM_OF_FAILURES_IN_SUMMARY} below.\n")

    for index, failed_log_path in enumerate(failures, start=1):
        if index >= NUM_OF_FAILURES_IN_SUMMARY:
            break

        stderr(f"\n-- Error #{index} -----------\n")

        with open(failed_log_path, "r") as failed_log:
            shutil.copyfileobj(failed_log, LOCAL.stdout)

    if total_failed > NUM_OF_FAILURES_IN_SUMMARY:
        stderr("See the results folder for more.")

    sys.exit(1)


def cleanup_reference_results(output_dir: str):
    """
    Delete html, css, and js files from reference results. These can
    include multiple copies of the benchmark source and so get very large.
    """
    extensions = ["html", "css", "js"]

    for extension in extensions:
        for file_to_rm in glob.glob(f"{output_dir}/*/*.{extension}"):
            file_to_rm = os.path.join(output_dir, file_to_rm)
            os.remove(file_to_rm)

    # Remove the log file. It leaks absolute path names.
    remove_log_file(output_dir)


def run_cmp_results(directory: str, strictness: int = 0) -> bool:
    """
    Compare the warnings produced by scan-build.
    strictness defines the success criteria for the test:
      0 - success if there are no crashes or analyzer failure.
      1 - success if there are no difference in the number of reported bugs.
      2 - success if all the bug reports are identical.

    :return success: Whether tests pass according to the strictness
    criteria.
    """
    tests_passed = True
    start_time = time.time()

    ref_dir = os.path.join(directory, REF_PREFIX + OUTPUT_DIR_NAME)
    new_dir = os.path.join(directory, OUTPUT_DIR_NAME)

    # We have to go one level down the directory tree.
    ref_list = glob.glob(ref_dir + "/*")
    new_list = glob.glob(new_dir + "/*")

    # Log folders are also located in the results dir, so ignore them.
    ref_log_dir = os.path.join(ref_dir, LOG_DIR_NAME)
    if ref_log_dir in ref_list:
        ref_list.remove(ref_log_dir)
    new_list.remove(os.path.join(new_dir, LOG_DIR_NAME))

    if len(ref_list) != len(new_list):
        stderr(f"Mismatch in number of results folders: " f"{ref_list} vs {new_list}")
        sys.exit(1)

    # There might be more then one folder underneath - one per each scan-build
    # command (Ex: one for configure and one for make).
    if len(ref_list) > 1:
        # Assume that the corresponding folders have the same names.
        ref_list.sort()
        new_list.sort()

    # Iterate and find the differences.
    num_diffs = 0
    for ref_dir, new_dir in zip(ref_list, new_list):
        assert ref_dir != new_dir

        if VERBOSE >= 1:
            stdout(f"  Comparing Results: {ref_dir} {new_dir}\n")

        patched_source = os.path.join(directory, PATCHED_SOURCE_DIR_NAME)

        ref_results = CmpRuns.ResultsDirectory(ref_dir)
        new_results = CmpRuns.ResultsDirectory(new_dir, patched_source)

        # Scan the results, delete empty plist files.
        (
            num_diffs,
            reports_in_ref,
            reports_in_new,
        ) = CmpRuns.dump_scan_build_results_diff(
            ref_results, new_results, delete_empty=False, out=LOCAL.stdout
        )

        if num_diffs > 0:
            stdout(f"Warning: {num_diffs} differences in diagnostics.\n")

        if strictness >= 2 and num_diffs > 0:
            stdout("Error: Diffs found in strict mode (2).\n")
            tests_passed = False

        elif strictness >= 1 and reports_in_ref != reports_in_new:
            stdout("Error: The number of results are different " " strict mode (1).\n")
            tests_passed = False

    stdout(
        f"Diagnostic comparison complete " f"(time: {time.time() - start_time:.2f}).\n"
    )

    return tests_passed


def normalize_reference_results(directory: str, output_dir: str, build_mode: int):
    """
    Make the absolute paths relative in the reference results.
    """
    for dir_path, _, filenames in os.walk(output_dir):
        for filename in filenames:
            if not filename.endswith("plist"):
                continue

            plist = os.path.join(dir_path, filename)
            with open(plist, "rb") as plist_file:
                data = plistlib.load(plist_file)
            path_prefix = directory

            if build_mode == 1:
                path_prefix = os.path.join(directory, PATCHED_SOURCE_DIR_NAME)

            paths = [
                source[len(path_prefix) + 1 :]
                if source.startswith(path_prefix)
                else source
                for source in data["files"]
            ]
            data["files"] = paths

            # Remove transient fields which change from run to run.
            for diagnostic in data["diagnostics"]:
                if "HTMLDiagnostics_files" in diagnostic:
                    diagnostic.pop("HTMLDiagnostics_files")

            if "clang_version" in data:
                data.pop("clang_version")

            with open(plist, "wb") as plist_file:
                plistlib.dump(data, plist_file)


def get_build_log_path(output_dir: str) -> str:
    return os.path.join(output_dir, LOG_DIR_NAME, BUILD_LOG_NAME)


def remove_log_file(output_dir: str):
    build_log_path = get_build_log_path(output_dir)

    # Clean up the log file.
    if os.path.exists(build_log_path):
        if VERBOSE >= 1:
            stdout(f"  Removing log file: {build_log_path}\n")

        os.remove(build_log_path)


def clean_up_empty_plists(output_dir: str):
    """
    A plist file is created for each call to the analyzer(each source file).
    We are only interested on the once that have bug reports,
    so delete the rest.
    """
    for plist in glob.glob(output_dir + "/*/*.plist"):
        plist = os.path.join(output_dir, plist)

        try:
            with open(plist, "rb") as plist_file:
                data = plistlib.load(plist_file)
            # Delete empty reports.
            if not data["files"]:
                os.remove(plist)
                continue

        except InvalidFileException as e:
            stderr(f"Error parsing plist file {plist}: {str(e)}")
            continue


def clean_up_empty_folders(output_dir: str):
    """
    Remove empty folders from results, as git would not store them.
    """
    subdirs = glob.glob(output_dir + "/*")
    for subdir in subdirs:
        if not os.listdir(subdir):
            os.removedirs(subdir)


if __name__ == "__main__":
    print("SATestBuild.py should not be used on its own.")
    print("Please use 'SATest.py build' instead")
    sys.exit(1)
