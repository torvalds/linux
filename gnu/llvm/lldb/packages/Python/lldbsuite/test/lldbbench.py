# System modules
import time

# Third-party modules

# LLDB modules
from .lldbtest import *


class Stopwatch(object):
    """Stopwatch provides a simple utility to start/stop your stopwatch multiple
    times.  Each start/stop is equal to a lap, with its elapsed time accumulated
    while measurement is in progress.

    When you're ready to start from scratch for another round of measurements,
    be sure to call the reset() method.

    For example,

    sw = Stopwatch()
    for i in range(1000):
        with sw:
            # Do some length operations...
            ...
    # Get the average time.
    avg_time = sw.avg()

    # Reset the stopwatch as we are about to perform other kind of operations.
    sw.reset()
    ...
    """

    #############################################################
    #
    # Context manager interfaces to support the 'with' statement.
    #
    #############################################################

    def __enter__(self):
        """
        Context management protocol on entry to the body of the with statement.
        """
        return self.start()

    def __exit__(self, type, value, tb):
        """
        Context management protocol on exit from the body of the with statement.
        """
        self.stop()

    def reset(self):
        self.__laps__ = 0
        self.__total_elapsed__ = 0.0
        self.__start__ = None
        self.__stop__ = None
        self.__elapsed__ = 0.0
        self.__nums__ = []

    def __init__(self):
        self.reset()

    def start(self):
        if self.__start__ is None:
            self.__start__ = time.time()
        else:
            raise Exception("start() already called, did you forget to stop() first?")
        # Return self to facilitate the context manager __enter__ protocol.
        return self

    def stop(self):
        if self.__start__ is not None:
            self.__stop__ = time.time()
            elapsed = self.__stop__ - self.__start__
            self.__total_elapsed__ += elapsed
            self.__laps__ += 1
            self.__nums__.append(elapsed)
            self.__start__ = None  # Reset __start__ to be None again.
        else:
            raise Exception("stop() called without first start()?")

    def laps(self):
        """Gets the number of laps. One lap is equal to a start/stop action."""
        return self.__laps__

    def avg(self):
        """Equal to total elapsed time divided by the number of laps."""
        return self.__total_elapsed__ / self.__laps__

    # def sigma(self):
    #    """Return the standard deviation of the available samples."""
    #    if self.__laps__ <= 0:
    #        return None
    #    return numpy.std(self.__nums__)

    def __str__(self):
        return "Avg: %f (Laps: %d, Total Elapsed Time: %f, min=%f, max=%f)" % (
            self.avg(),
            self.__laps__,
            self.__total_elapsed__,
            min(self.__nums__),
            max(self.__nums__),
        )


class BenchBase(TestBase):
    """
    Abstract base class for benchmark tests.
    """

    def setUp(self):
        """Fixture for unittest test case setup."""
        super(BenchBase, self).setUp()
        # TestBase.setUp(self)
        self.stopwatch = Stopwatch()

    def tearDown(self):
        """Fixture for unittest test case teardown."""
        super(BenchBase, self).tearDown()
        # TestBase.tearDown(self)
        del self.stopwatch
