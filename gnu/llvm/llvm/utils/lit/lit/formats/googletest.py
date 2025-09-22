from __future__ import absolute_import
import json
import math
import os
import shlex
import subprocess
import sys

import lit.Test
import lit.TestRunner
import lit.util
from .base import TestFormat

kIsWindows = sys.platform in ["win32", "cygwin"]


class GoogleTest(TestFormat):
    def __init__(self, test_sub_dirs, test_suffix, run_under=[]):
        self.seen_executables = set()
        self.test_sub_dirs = str(test_sub_dirs).split(";")

        # On Windows, assume tests will also end in '.exe'.
        exe_suffix = str(test_suffix)
        if kIsWindows:
            exe_suffix += ".exe"

        # Also check for .py files for testing purposes.
        self.test_suffixes = {exe_suffix, test_suffix + ".py"}
        self.run_under = run_under

    def get_num_tests(self, path, litConfig, localConfig):
        list_test_cmd = self.prepareCmd(
            [path, "--gtest_list_tests", "--gtest_filter=-*DISABLED_*"]
        )
        try:
            out = subprocess.check_output(list_test_cmd, env=localConfig.environment)
        except subprocess.CalledProcessError as exc:
            litConfig.warning(
                "unable to discover google-tests in %r: %s. Process output: %s"
                % (path, sys.exc_info()[1], exc.output)
            )
            return None
        return sum(
            map(
                lambda line: lit.util.to_string(line).startswith("  "),
                out.splitlines(False),
            )
        )

    def getTestsInDirectory(self, testSuite, path_in_suite, litConfig, localConfig):
        init_shard_size = 512  # number of tests in a shard
        core_count = lit.util.usable_core_count()
        source_path = testSuite.getSourcePath(path_in_suite)
        for subdir in self.test_sub_dirs:
            dir_path = os.path.join(source_path, subdir)
            if not os.path.isdir(dir_path):
                continue
            for fn in lit.util.listdir_files(dir_path, suffixes=self.test_suffixes):
                # Discover the tests in this executable.
                execpath = os.path.join(source_path, subdir, fn)
                if execpath in self.seen_executables:
                    litConfig.warning(
                        "Skip adding %r since it has been added to the test pool"
                        % execpath
                    )
                    continue
                else:
                    self.seen_executables.add(execpath)
                num_tests = self.get_num_tests(execpath, litConfig, localConfig)
                if num_tests is not None:
                    if litConfig.gtest_sharding:
                        # Compute the number of shards.
                        shard_size = init_shard_size
                        nshard = int(math.ceil(num_tests / shard_size))
                        while nshard < core_count and shard_size > 1:
                            shard_size = shard_size // 2
                            nshard = int(math.ceil(num_tests / shard_size))

                        # Create one lit test for each shard.
                        for idx in range(nshard):
                            testPath = path_in_suite + (
                                subdir,
                                fn,
                                str(idx),
                                str(nshard),
                            )
                            json_file = (
                                "-".join(
                                    [
                                        execpath,
                                        testSuite.config.name,
                                        str(os.getpid()),
                                        str(idx),
                                        str(nshard),
                                    ]
                                )
                                + ".json"
                            )
                            yield lit.Test.Test(
                                testSuite,
                                testPath,
                                localConfig,
                                file_path=execpath,
                                gtest_json_file=json_file,
                            )
                    else:
                        testPath = path_in_suite + (subdir, fn)
                        json_file = (
                            "-".join(
                                [
                                    execpath,
                                    testSuite.config.name,
                                    str(os.getpid()),
                                ]
                            )
                            + ".json"
                        )
                        yield lit.Test.Test(
                            testSuite,
                            testPath,
                            localConfig,
                            file_path=execpath,
                            gtest_json_file=json_file,
                        )
                else:
                    # This doesn't look like a valid gtest file.  This can
                    # have a number of causes, none of them good.  For
                    # instance, we could have created a broken executable.
                    # Alternatively, someone has cruft in their test
                    # directory.  If we don't return a test here, then no
                    # failures will get reported, so return a dummy test name
                    # so that the failure is reported later.
                    testPath = path_in_suite + (
                        subdir,
                        fn,
                        "failed_to_discover_tests_from_gtest",
                    )
                    yield lit.Test.Test(
                        testSuite, testPath, localConfig, file_path=execpath
                    )

    def execute(self, test, litConfig):
        if test.gtest_json_file is None:
            return lit.Test.FAIL, ""

        testPath = test.getSourcePath()
        from lit.cl_arguments import TestOrder

        use_shuffle = TestOrder(litConfig.order) == TestOrder.RANDOM
        shard_env = {
            "GTEST_OUTPUT": "json:" + test.gtest_json_file,
            "GTEST_SHUFFLE": "1" if use_shuffle else "0",
        }
        if litConfig.gtest_sharding:
            testPath, testName = os.path.split(test.getSourcePath())
            while not os.path.exists(testPath):
                # Handle GTest parameterized and typed tests, whose name includes
                # some '/'s.
                testPath, namePrefix = os.path.split(testPath)
                testName = namePrefix + "/" + testName

            testName, total_shards = os.path.split(testName)
            testName, shard_idx = os.path.split(testName)
            shard_env.update(
                {
                    "GTEST_TOTAL_SHARDS": os.environ.get(
                        "GTEST_TOTAL_SHARDS", total_shards
                    ),
                    "GTEST_SHARD_INDEX": os.environ.get("GTEST_SHARD_INDEX", shard_idx),
                }
            )
        test.config.environment.update(shard_env)

        cmd = [testPath]
        cmd = self.prepareCmd(cmd)
        if litConfig.useValgrind:
            cmd = litConfig.valgrindArgs + cmd

        if litConfig.noExecute:
            return lit.Test.PASS, ""

        def get_shard_header(shard_env):
            shard_envs = " ".join([k + "=" + v for k, v in shard_env.items()])
            return f"Script(shard):\n--\n%s %s\n--\n" % (shard_envs, " ".join(cmd))

        shard_header = get_shard_header(shard_env)

        try:
            out, _, exitCode = lit.util.executeCommand(
                cmd,
                env=test.config.environment,
                timeout=litConfig.maxIndividualTestTime,
                redirect_stderr=True,
            )
        except lit.util.ExecuteCommandTimeoutException as e:
            stream_msg = f"\n{e.out}\n--\nexit: {e.exitCode}\n--\n"
            return (
                lit.Test.TIMEOUT,
                f"{shard_header}{stream_msg}Reached "
                f"timeout of {litConfig.maxIndividualTestTime} seconds",
            )

        if not os.path.exists(test.gtest_json_file):
            errmsg = f"shard JSON output does not exist: %s" % (test.gtest_json_file)
            stream_msg = f"\n{out}\n--\nexit: {exitCode}\n--\n"
            return lit.Test.FAIL, shard_header + stream_msg + errmsg

        if exitCode == 0:
            return lit.Test.PASS, ""

        def get_test_stdout(test_name):
            res = []
            header = f"[ RUN      ] " + test_name
            footer = f"[  FAILED  ] " + test_name
            in_range = False
            for l in out.splitlines():
                if l.startswith(header):
                    in_range = True
                elif l.startswith(footer):
                    return f"" if len(res) == 0 else "\n".join(res)
                elif in_range:
                    res.append(l)
            assert False, f"gtest did not report the result for " + test_name

        found_failed_test = False

        with open(test.gtest_json_file, encoding="utf-8") as f:
            jf = json.load(f)

            if use_shuffle:
                shard_env["GTEST_RANDOM_SEED"] = str(jf["random_seed"])
            output = get_shard_header(shard_env) + "\n"

            for testcase in jf["testsuites"]:
                for testinfo in testcase["testsuite"]:
                    result = testinfo["result"]
                    if result == "SUPPRESSED" or result == "SKIPPED":
                        continue
                    testname = testcase["name"] + "." + testinfo["name"]
                    header = f"Script:\n--\n%s --gtest_filter=%s\n--\n" % (
                        " ".join(cmd),
                        testname,
                    )
                    if "failures" in testinfo:
                        found_failed_test = True
                        output += header
                        test_out = get_test_stdout(testname)
                        if test_out:
                            output += test_out + "\n\n"
                        for fail in testinfo["failures"]:
                            output += fail["failure"] + "\n"
                        output += "\n"
                    elif result != "COMPLETED":
                        output += header
                        output += "unresolved test result\n"

        # In some situations, like running tests with sanitizers, all test passes but
        # the shard could still fail due to memory issues.
        if not found_failed_test:
            output += f"\n{out}\n--\nexit: {exitCode}\n--\n"

        return lit.Test.FAIL, output

    def prepareCmd(self, cmd):
        """Insert interpreter if needed.

        It inserts the python exe into the command if cmd[0] ends in .py or caller
        specified run_under.
        We cannot rely on the system to interpret shebang lines for us on
        Windows, so add the python executable to the command if this is a .py
        script.
        """
        if cmd[0].endswith(".py"):
            cmd = [sys.executable] + cmd
        if self.run_under:
            if isinstance(self.run_under, list):
                cmd = self.run_under + cmd
            else:
                cmd = shlex.split(self.run_under) + cmd
        return cmd

    @staticmethod
    def post_process_shard_results(selected_tests, discovered_tests):
        def remove_gtest(tests):
            return [t for t in tests if t.gtest_json_file is None]

        discovered_tests = remove_gtest(discovered_tests)
        gtests = [t for t in selected_tests if t.gtest_json_file]
        selected_tests = remove_gtest(selected_tests)
        for test in gtests:
            # In case gtest has bugs such that no JSON file was emitted.
            if not os.path.exists(test.gtest_json_file):
                selected_tests.append(test)
                discovered_tests.append(test)
                continue

            start_time = test.result.start or 0.0

            has_failure_in_shard = False

            # Load json file to retrieve results.
            with open(test.gtest_json_file, encoding="utf-8") as f:
                try:
                    testsuites = json.load(f)["testsuites"]
                except json.JSONDecodeError as e:
                    raise RuntimeError(
                        "Failed to parse json file: "
                        + test.gtest_json_file
                        + "\n"
                        + e.doc
                    )
                for testcase in testsuites:
                    for testinfo in testcase["testsuite"]:
                        # Ignore disabled tests.
                        if testinfo["result"] == "SUPPRESSED":
                            continue

                        testPath = test.path_in_suite[:-2] + (
                            testcase["name"],
                            testinfo["name"],
                        )
                        subtest = lit.Test.Test(
                            test.suite, testPath, test.config, test.file_path
                        )

                        testname = testcase["name"] + "." + testinfo["name"]
                        header = f"Script:\n--\n%s --gtest_filter=%s\n--\n" % (
                            test.file_path,
                            testname,
                        )

                        output = ""
                        if testinfo["result"] == "SKIPPED":
                            returnCode = lit.Test.SKIPPED
                        elif "failures" in testinfo:
                            has_failure_in_shard = True
                            returnCode = lit.Test.FAIL
                            output = header
                            for fail in testinfo["failures"]:
                                output += fail["failure"] + "\n"
                        elif testinfo["result"] == "COMPLETED":
                            returnCode = lit.Test.PASS
                        else:
                            returnCode = lit.Test.UNRESOLVED
                            output = header + "unresolved test result\n"

                        elapsed_time = float(testinfo["time"][:-1])
                        res = lit.Test.Result(returnCode, output, elapsed_time)
                        res.pid = test.result.pid or 0
                        res.start = start_time
                        start_time = start_time + elapsed_time
                        subtest.setResult(res)

                        selected_tests.append(subtest)
                        discovered_tests.append(subtest)
            os.remove(test.gtest_json_file)

            if not has_failure_in_shard and test.isFailure():
                selected_tests.append(test)
                discovered_tests.append(test)

        return selected_tests, discovered_tests
