import unittest

import lit.discovery
import lit.LitConfig
import lit.worker

"""
TestCase adaptor for providing a Python 'unittest' compatible interface to 'lit'
tests.
"""


class UnresolvedError(RuntimeError):
    pass


class LitTestCase(unittest.TestCase):
    def __init__(self, test, lit_config):
        unittest.TestCase.__init__(self)
        self._test = test
        self._lit_config = lit_config

    def id(self):
        return self._test.getFullName()

    def shortDescription(self):
        return self._test.getFullName()

    def runTest(self):
        # Run the test.
        result = lit.worker._execute(self._test, self._lit_config)

        # Adapt the result to unittest.
        if result.code is lit.Test.UNRESOLVED:
            raise UnresolvedError(result.output)
        elif result.code.isFailure:
            self.fail(result.output)


def load_test_suite(inputs):
    import platform

    windows = platform.system() == "Windows"

    # Create the global config object.
    lit_config = lit.LitConfig.LitConfig(
        progname="lit",
        path=[],
        quiet=False,
        useValgrind=False,
        valgrindLeakCheck=False,
        valgrindArgs=[],
        noExecute=False,
        debug=False,
        isWindows=windows,
        order="smart",
        params={},
    )

    # Perform test discovery.
    tests = lit.discovery.find_tests_for_inputs(lit_config, inputs)
    test_adaptors = [LitTestCase(t, lit_config) for t in tests]

    # Return a unittest test suite which just runs the tests in order.
    return unittest.TestSuite(test_adaptors)
