import itertools
import os
from json import JSONEncoder

from lit.BooleanExpression import BooleanExpression
from lit.TestTimes import read_test_times

# Test result codes.


class ResultCode(object):
    """Test result codes."""

    # All result codes (including user-defined ones) in declaration order
    _all_codes = []

    @staticmethod
    def all_codes():
        return ResultCode._all_codes

    # We override __new__ and __getnewargs__ to ensure that pickling still
    # provides unique ResultCode objects in any particular instance.
    _instances = {}

    def __new__(cls, name, label, isFailure):
        res = cls._instances.get(name)
        if res is None:
            cls._instances[name] = res = super(ResultCode, cls).__new__(cls)
        return res

    def __getnewargs__(self):
        return (self.name, self.label, self.isFailure)

    def __init__(self, name, label, isFailure):
        self.name = name
        self.label = label
        self.isFailure = isFailure
        ResultCode._all_codes.append(self)

    def __repr__(self):
        return "%s%r" % (self.__class__.__name__, (self.name, self.isFailure))


# Successes
EXCLUDED = ResultCode("EXCLUDED", "Excluded", False)
SKIPPED = ResultCode("SKIPPED", "Skipped", False)
UNSUPPORTED = ResultCode("UNSUPPORTED", "Unsupported", False)
PASS = ResultCode("PASS", "Passed", False)
FLAKYPASS = ResultCode("FLAKYPASS", "Passed With Retry", False)
XFAIL = ResultCode("XFAIL", "Expectedly Failed", False)
# Failures
UNRESOLVED = ResultCode("UNRESOLVED", "Unresolved", True)
TIMEOUT = ResultCode("TIMEOUT", "Timed Out", True)
FAIL = ResultCode("FAIL", "Failed", True)
XPASS = ResultCode("XPASS", "Unexpectedly Passed", True)


# Test metric values.


class MetricValue(object):
    def format(self):
        """
        format() -> str

        Convert this metric to a string suitable for displaying as part of the
        console output.
        """
        raise RuntimeError("abstract method")

    def todata(self):
        """
        todata() -> json-serializable data

        Convert this metric to content suitable for serializing in the JSON test
        output.
        """
        raise RuntimeError("abstract method")


class IntMetricValue(MetricValue):
    def __init__(self, value):
        self.value = value

    def format(self):
        return str(self.value)

    def todata(self):
        return self.value


class RealMetricValue(MetricValue):
    def __init__(self, value):
        self.value = value

    def format(self):
        return "%.4f" % self.value

    def todata(self):
        return self.value


class JSONMetricValue(MetricValue):
    """
    JSONMetricValue is used for types that are representable in the output
    but that are otherwise uninterpreted.
    """

    def __init__(self, value):
        # Ensure the value is a serializable by trying to encode it.
        # WARNING: The value may change before it is encoded again, and may
        #          not be encodable after the change.
        try:
            e = JSONEncoder()
            e.encode(value)
        except TypeError:
            raise
        self.value = value

    def format(self):
        e = JSONEncoder(indent=2, sort_keys=True)
        return e.encode(self.value)

    def todata(self):
        return self.value


def toMetricValue(value):
    if isinstance(value, MetricValue):
        return value
    elif isinstance(value, int):
        return IntMetricValue(value)
    elif isinstance(value, float):
        return RealMetricValue(value)
    else:
        # 'long' is only present in python2
        try:
            if isinstance(value, long):
                return IntMetricValue(value)
        except NameError:
            pass

        # Try to create a JSONMetricValue and let the constructor throw
        # if value is not a valid type.
        return JSONMetricValue(value)


# Test results.


class Result(object):
    """Wrapper for the results of executing an individual test."""

    def __init__(self, code, output="", elapsed=None):
        # The result code.
        self.code = code
        # The test output.
        self.output = output
        # The wall timing to execute the test, if timing.
        self.elapsed = elapsed
        self.start = None
        self.pid = None
        # The metrics reported by this test.
        self.metrics = {}
        # The micro-test results reported by this test.
        self.microResults = {}

    def addMetric(self, name, value):
        """
        addMetric(name, value)

        Attach a test metric to the test result, with the given name and list of
        values. It is an error to attempt to attach the metrics with the same
        name multiple times.

        Each value must be an instance of a MetricValue subclass.
        """
        if name in self.metrics:
            raise ValueError("result already includes metrics for %r" % (name,))
        if not isinstance(value, MetricValue):
            raise TypeError("unexpected metric value: %r" % (value,))
        self.metrics[name] = value

    def addMicroResult(self, name, microResult):
        """
        addMicroResult(microResult)

        Attach a micro-test result to the test result, with the given name and
        result.  It is an error to attempt to attach a micro-test with the
        same name multiple times.

        Each micro-test result must be an instance of the Result class.
        """
        if name in self.microResults:
            raise ValueError("Result already includes microResult for %r" % (name,))
        if not isinstance(microResult, Result):
            raise TypeError("unexpected MicroResult value %r" % (microResult,))
        self.microResults[name] = microResult


# Test classes.


class TestSuite:
    """TestSuite - Information on a group of tests.

    A test suite groups together a set of logically related tests.
    """

    def __init__(self, name, source_root, exec_root, config):
        self.name = name
        self.source_root = source_root
        self.exec_root = exec_root
        # The test suite configuration.
        self.config = config

        self.test_times = read_test_times(self)

    def getSourcePath(self, components):
        return os.path.join(self.source_root, *components)

    def getExecPath(self, components):
        return os.path.join(self.exec_root, *components)


class Test:
    """Test - Information on a single test instance."""

    def __init__(
        self, suite, path_in_suite, config, file_path=None, gtest_json_file=None
    ):
        self.suite = suite
        self.path_in_suite = path_in_suite
        self.config = config
        self.file_path = file_path
        self.gtest_json_file = gtest_json_file

        # A list of conditions under which this test is expected to fail.
        # Each condition is a boolean expression of features, or '*'.
        # These can optionally be provided by test format handlers,
        # and will be honored when the test result is supplied.
        self.xfails = []

        # If true, ignore all items in self.xfails.
        self.xfail_not = False

        # A list of conditions that must be satisfied before running the test.
        # Each condition is a boolean expression of features. All of them
        # must be True for the test to run.
        self.requires = []

        # A list of conditions that prevent execution of the test.
        # Each condition is a boolean expression of features. All of them
        # must be False for the test to run.
        self.unsupported = []

        # An optional number of retries allowed before the test finally succeeds.
        # The test is run at most once plus the number of retries specified here.
        self.allowed_retries = getattr(config, "test_retry_attempts", 0)

        # The test result, once complete.
        self.result = None

        # The previous test failure state, if applicable.
        self.previous_failure = False

        # The previous test elapsed time, if applicable.
        self.previous_elapsed = 0.0

        if suite.test_times and "/".join(path_in_suite) in suite.test_times:
            time = suite.test_times["/".join(path_in_suite)]
            self.previous_elapsed = abs(time)
            self.previous_failure = time < 0

    def setResult(self, result):
        assert self.result is None, "result already set"
        assert isinstance(result, Result), "unexpected result type"
        try:
            expected_to_fail = self.isExpectedToFail()
        except ValueError as err:
            # Syntax error in an XFAIL line.
            result.code = UNRESOLVED
            result.output = str(err)
        else:
            if expected_to_fail:
                # pass -> unexpected pass
                if result.code is PASS:
                    result.code = XPASS
                # fail -> expected fail
                elif result.code is FAIL:
                    result.code = XFAIL
        self.result = result

    def isFailure(self):
        assert self.result
        return self.result.code.isFailure

    def getFullName(self):
        return self.suite.config.name + " :: " + "/".join(self.path_in_suite)

    def getFilePath(self):
        if self.file_path:
            return self.file_path
        return self.getSourcePath()

    def getSourcePath(self):
        return self.suite.getSourcePath(self.path_in_suite)

    def getExecPath(self):
        return self.suite.getExecPath(self.path_in_suite)

    def isExpectedToFail(self):
        """
        isExpectedToFail() -> bool

        Check whether this test is expected to fail in the current
        configuration. This check relies on the test xfails property which by
        some test formats may not be computed until the test has first been
        executed.
        Throws ValueError if an XFAIL line has a syntax error.
        """

        if self.xfail_not:
            return False

        features = self.config.available_features

        # Check if any of the xfails match an available feature.
        for item in self.xfails:
            # If this is the wildcard, it always fails.
            if item == "*":
                return True

            # If this is a True expression of features, it fails.
            try:
                if BooleanExpression.evaluate(item, features):
                    return True
            except ValueError as e:
                raise ValueError("Error in XFAIL list:\n%s" % str(e))

        return False

    def isWithinFeatureLimits(self):
        """
        isWithinFeatureLimits() -> bool

        A test is within the feature limits set by run_only_tests if
        1. the test's requirements ARE satisfied by the available features
        2. the test's requirements ARE NOT satisfied after the limiting
           features are removed from the available features

        Throws ValueError if a REQUIRES line has a syntax error.
        """

        if not self.config.limit_to_features:
            return True  # No limits. Run it.

        # Check the requirements as-is (#1)
        if self.getMissingRequiredFeatures():
            return False

        # Check the requirements after removing the limiting features (#2)
        featuresMinusLimits = [
            f
            for f in self.config.available_features
            if not f in self.config.limit_to_features
        ]
        if not self.getMissingRequiredFeaturesFromList(featuresMinusLimits):
            return False

        return True

    def getMissingRequiredFeaturesFromList(self, features):
        try:
            return [
                item
                for item in self.requires
                if not BooleanExpression.evaluate(item, features)
            ]
        except ValueError as e:
            raise ValueError("Error in REQUIRES list:\n%s" % str(e))

    def getMissingRequiredFeatures(self):
        """
        getMissingRequiredFeatures() -> list of strings

        Returns a list of features from REQUIRES that are not satisfied."
        Throws ValueError if a REQUIRES line has a syntax error.
        """

        features = self.config.available_features
        return self.getMissingRequiredFeaturesFromList(features)

    def getUnsupportedFeatures(self):
        """
        getUnsupportedFeatures() -> list of strings

        Returns a list of features from UNSUPPORTED that are present
        in the test configuration's features.
        Throws ValueError if an UNSUPPORTED line has a syntax error.
        """

        features = self.config.available_features

        try:
            return [
                item
                for item in self.unsupported
                if BooleanExpression.evaluate(item, features)
            ]
        except ValueError as e:
            raise ValueError("Error in UNSUPPORTED list:\n%s" % str(e))

    def getUsedFeatures(self):
        """
        getUsedFeatures() -> list of strings

        Returns a list of all features appearing in XFAIL, UNSUPPORTED and
        REQUIRES annotations for this test.
        """
        import lit.TestRunner

        parsed = lit.TestRunner._parseKeywords(
            self.getSourcePath(), require_script=False
        )
        feature_keywords = ("UNSUPPORTED:", "REQUIRES:", "XFAIL:")
        boolean_expressions = itertools.chain.from_iterable(
            parsed[k] or [] for k in feature_keywords
        )
        tokens = itertools.chain.from_iterable(
            BooleanExpression.tokenize(expr)
            for expr in boolean_expressions
            if expr != "*"
        )
        matchExpressions = set(filter(BooleanExpression.isMatchExpression, tokens))
        return matchExpressions
