"""
Test discovery functions.
"""

import copy
import os
import sys

from lit.TestingConfig import TestingConfig
from lit import LitConfig, Test, util


def chooseConfigFileFromDir(dir, config_names):
    for name in config_names:
        p = os.path.join(dir, name)
        if os.path.exists(p):
            return p
    return None


def dirContainsTestSuite(path, lit_config):
    cfgpath = chooseConfigFileFromDir(path, lit_config.site_config_names)
    if not cfgpath:
        cfgpath = chooseConfigFileFromDir(path, lit_config.config_names)
    return cfgpath


def getTestSuite(item, litConfig, cache):
    """getTestSuite(item, litConfig, cache) -> (suite, relative_path)

    Find the test suite containing @arg item.

    @retval (None, ...) - Indicates no test suite contains @arg item.
    @retval (suite, relative_path) - The suite that @arg item is in, and its
    relative path inside that suite.
    """

    def search1(path):
        # Check for a site config or a lit config.
        cfgpath = dirContainsTestSuite(path, litConfig)

        # If we didn't find a config file, keep looking.
        if not cfgpath:
            parent, base = os.path.split(path)
            if parent == path:
                return (None, ())

            ts, relative = search(parent)
            return (ts, relative + (base,))

        # This is a private builtin parameter which can be used to perform
        # translation of configuration paths.  Specifically, this parameter
        # can be set to a dictionary that the discovery process will consult
        # when it finds a configuration it is about to load.  If the given
        # path is in the map, the value of that key is a path to the
        # configuration to load instead.
        config_map = litConfig.params.get("config_map")
        if config_map:
            cfgpath = util.abs_path_preserve_drive(cfgpath)
            target = config_map.get(os.path.normcase(cfgpath))
            if target:
                cfgpath = target

        # We found a test suite, create a new config for it and load it.
        if litConfig.debug:
            litConfig.note("loading suite config %r" % cfgpath)

        cfg = TestingConfig.fromdefaults(litConfig)
        cfg.load_from_path(cfgpath, litConfig)
        source_root = util.abs_path_preserve_drive(cfg.test_source_root or path)
        exec_root = util.abs_path_preserve_drive(cfg.test_exec_root or path)
        return Test.TestSuite(cfg.name, source_root, exec_root, cfg), ()

    def search(path):
        # Check for an already instantiated test suite.
        real_path = util.abs_path_preserve_drive(path)
        res = cache.get(real_path)
        if res is None:
            cache[real_path] = res = search1(path)
        return res

    # Canonicalize the path.
    item = os.path.normpath(os.path.join(os.getcwd(), item))

    # Skip files and virtual components.
    components = []
    while not os.path.isdir(item):
        parent, base = os.path.split(item)
        if parent == item:
            return (None, ())
        components.append(base)
        item = parent
    components.reverse()

    ts, relative = search(item)
    return ts, tuple(relative + tuple(components))


def getLocalConfig(ts, path_in_suite, litConfig, cache):
    def search1(path_in_suite):
        # Get the parent config.
        if not path_in_suite:
            parent = ts.config
        else:
            parent = search(path_in_suite[:-1])

        # Check if there is a local configuration file.
        source_path = ts.getSourcePath(path_in_suite)
        cfgpath = chooseConfigFileFromDir(source_path, litConfig.local_config_names)

        # If not, just reuse the parent config.
        if not cfgpath:
            return parent

        # Otherwise, copy the current config and load the local configuration
        # file into it.
        config = copy.deepcopy(parent)
        if litConfig.debug:
            litConfig.note("loading local config %r" % cfgpath)
        config.load_from_path(cfgpath, litConfig)
        return config

    def search(path_in_suite):
        key = (ts, path_in_suite)
        res = cache.get(key)
        if res is None:
            cache[key] = res = search1(path_in_suite)
        return res

    return search(path_in_suite)


def getTests(path, litConfig, testSuiteCache, localConfigCache):
    # Find the test suite for this input and its relative path.
    ts, path_in_suite = getTestSuite(path, litConfig, testSuiteCache)
    if ts is None:
        litConfig.warning("unable to find test suite for %r" % path)
        return (), ()

    if litConfig.debug:
        litConfig.note("resolved input %r to %r::%r" % (path, ts.name, path_in_suite))

    return ts, getTestsInSuite(
        ts,
        path_in_suite,
        litConfig,
        testSuiteCache,
        localConfigCache,
    )


def getTestsInSuite(
    ts, path_in_suite, litConfig, testSuiteCache, localConfigCache
):
    # Check that the source path exists (errors here are reported by the
    # caller).
    source_path = ts.getSourcePath(path_in_suite)
    if not os.path.exists(source_path):
        return

    # Check if the user named a test directly.
    if not os.path.isdir(source_path):
        test_dir_in_suite = path_in_suite[:-1]
        lc = getLocalConfig(ts, test_dir_in_suite, litConfig, localConfigCache)

        # If we don't have a test format or if we are running standalone tests,
        # always "find" the test itself. Otherwise, we might find no tests at
        # all, which is considered an error but isn't an error with standalone
        # tests.
        tests = [Test.Test(ts, path_in_suite, lc)] if lc.test_format is None or lc.standalone_tests else \
                lc.test_format.getTestsForPath(ts, path_in_suite, litConfig, lc)

        for test in tests:
            yield test
        return

    # Otherwise we have a directory to search for tests, start by getting the
    # local configuration.
    lc = getLocalConfig(ts, path_in_suite, litConfig, localConfigCache)

    # Directory contains tests to be run standalone. Do not try to discover.
    if lc.standalone_tests:
        if lc.suffixes or lc.excludes:
            litConfig.warning(
                "standalone_tests set in LIT config but suffixes or excludes"
                " are also set"
            )
        return

    # Search for tests.
    if lc.test_format is not None:
        for res in lc.test_format.getTestsInDirectory(ts, path_in_suite, litConfig, lc):
            yield res

    # Search subdirectories.
    for filename in os.listdir(source_path):
        # FIXME: This doesn't belong here?
        if filename in ("Output", ".svn", ".git") or filename in lc.excludes:
            continue

        # Ignore non-directories.
        file_sourcepath = os.path.join(source_path, filename)
        if not os.path.isdir(file_sourcepath):
            continue

        # Check for nested test suites, first in the execpath in case there is a
        # site configuration and then in the source path.
        subpath = path_in_suite + (filename,)
        file_execpath = ts.getExecPath(subpath)
        if dirContainsTestSuite(file_execpath, litConfig):
            sub_ts, subpath_in_suite = getTestSuite(
                file_execpath, litConfig, testSuiteCache
            )
        elif dirContainsTestSuite(file_sourcepath, litConfig):
            sub_ts, subpath_in_suite = getTestSuite(
                file_sourcepath, litConfig, testSuiteCache
            )
        else:
            sub_ts = None

        # If the this directory recursively maps back to the current test suite,
        # disregard it (this can happen if the exec root is located inside the
        # current test suite, for example).
        if sub_ts is ts:
            continue

        # Otherwise, load from the nested test suite, if present.
        if sub_ts is not None:
            subiter = getTestsInSuite(
                sub_ts,
                subpath_in_suite,
                litConfig,
                testSuiteCache,
                localConfigCache,
            )
        else:
            subiter = getTestsInSuite(
                ts,
                subpath,
                litConfig,
                testSuiteCache,
                localConfigCache,
            )

        N = 0
        for res in subiter:
            N += 1
            yield res
        if sub_ts and not N:
            litConfig.warning("test suite %r contained no tests" % sub_ts.name)


def find_tests_for_inputs(lit_config, inputs):
    """
    find_tests_for_inputs(lit_config, inputs) -> [Test]

    Given a configuration object and a list of input specifiers, find all the
    tests to execute.
    """

    # Load the tests from the inputs.
    tests = []
    test_suite_cache = {}
    local_config_cache = {}
    for input in inputs:
        prev = len(tests)
        tests.extend(
            getTests(
                input,
                lit_config,
                test_suite_cache,
                local_config_cache,
            )[1]
        )
        if prev == len(tests):
            lit_config.warning("input %r contained no tests" % input)

    # This data is no longer needed but keeping it around causes awful
    # performance problems while the test suites run.
    for k, suite in test_suite_cache.items():
        if suite[0]:
            suite[0].test_times = None

    # If there were any errors during test discovery, exit now.
    if lit_config.numErrors:
        sys.stderr.write("%d errors, exiting.\n" % lit_config.numErrors)
        sys.exit(2)

    return tests
