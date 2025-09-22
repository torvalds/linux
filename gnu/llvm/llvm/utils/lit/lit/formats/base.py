from __future__ import absolute_import
import os

import lit.Test
import lit.util


class TestFormat(object):
    def getTestsForPath(self, testSuite, path_in_suite, litConfig, localConfig):
        """
        Given the path to a test in the test suite, generates the Lit tests associated
        to that path. There can be zero, one or more tests. For example, some testing
        formats allow expanding a single path in the test suite into multiple Lit tests
        (e.g. they are generated on the fly).

        Note that this method is only used when Lit needs to actually perform test
        discovery, which is not the case for configs with standalone tests.
        """
        yield lit.Test.Test(testSuite, path_in_suite, localConfig)

###


class FileBasedTest(TestFormat):
    def getTestsForPath(self, testSuite, path_in_suite, litConfig, localConfig):
        """
        Expand each path in a test suite to a Lit test using that path and assuming
        it is a file containing the test. File extensions excluded by the configuration
        or not contained in the allowed extensions are ignored.
        """
        filename = path_in_suite[-1]

        # Ignore dot files and excluded tests.
        if filename.startswith(".") or filename in localConfig.excludes:
            return

        base, ext = os.path.splitext(filename)
        if ext in localConfig.suffixes:
            yield lit.Test.Test(testSuite, path_in_suite, localConfig)

    def getTestsInDirectory(self, testSuite, path_in_suite, litConfig, localConfig):
        source_path = testSuite.getSourcePath(path_in_suite)
        for filename in os.listdir(source_path):
            filepath = os.path.join(source_path, filename)
            if not os.path.isdir(filepath):
                for t in self.getTestsForPath(testSuite, path_in_suite + (filename,), litConfig, localConfig):
                    yield t


###

# Check exit code of a simple executable with no input
class ExecutableTest(FileBasedTest):
    def execute(self, test, litConfig):
        if test.config.unsupported:
            return lit.Test.UNSUPPORTED

        out, err, exitCode = lit.util.executeCommand(test.getSourcePath())

        if not exitCode:
            return lit.Test.PASS, ""

        return lit.Test.FAIL, out + err
