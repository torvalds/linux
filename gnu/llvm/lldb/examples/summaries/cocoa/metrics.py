"""
Objective-C runtime wrapper for use by LLDB Python formatters

Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
See https://llvm.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""
import lldb
import time
import datetime
import inspect


class TimeMetrics:
    @staticmethod
    def generate(label=None):
        return TimeMetrics(label)

    def __init__(self, lbl=None):
        self.label = "" if lbl is None else lbl
        pass

    def __enter__(self):
        caller = inspect.stack()[1]
        self.function = str(caller)
        self.enter_time = time.clock()

    def __exit__(self, a, b, c):
        self.exit_time = time.clock()
        print(
            "It took "
            + str(self.exit_time - self.enter_time)
            + " time units to run through "
            + self.function
            + self.label
        )
        return False


class Counter:
    def __init__(self):
        self.count = 0
        self.list = []

    def update(self, name):
        self.count = self.count + 1
        # avoid getting the full dump of this ValueObject just to save its
        # metrics
        if isinstance(name, lldb.SBValue):
            self.list.append(name.GetName())
        else:
            self.list.append(str(name))

    def __str__(self):
        return str(self.count) + " times, for items [" + str(self.list) + "]"


class MetricsPrinter_Verbose:
    def __init__(self, metrics):
        self.metrics = metrics

    def __str__(self):
        string = ""
        for key, value in self.metrics.metrics.items():
            string = string + "metric " + str(key) + ": " + str(value) + "\n"
        return string


class MetricsPrinter_Compact:
    def __init__(self, metrics):
        self.metrics = metrics

    def __str__(self):
        string = ""
        for key, value in self.metrics.metrics.items():
            string = (
                string
                + "metric "
                + str(key)
                + " was hit "
                + str(value.count)
                + " times\n"
            )
        return string


class Metrics:
    def __init__(self):
        self.metrics = {}

    def add_metric(self, name):
        self.metrics[name] = Counter()

    def metric_hit(self, metric, trigger):
        self.metrics[metric].update(trigger)

    def __getitem__(self, key):
        return self.metrics[key]

    def __getattr__(self, name):
        if name == "compact":
            return MetricsPrinter_Compact(self)
        if name == "verbose":
            return MetricsPrinter_Verbose(self)
        raise AttributeError(
            "%r object has no attribute %r" % (type(self).__name__, name)
        )

    def __str__(self):
        return str(self.verbose)

    def metric_success(self, metric):
        total_count = 0
        metric_count = self[metric].count
        for key, value in self.metrics.items():
            total_count = total_count + value.count
        if total_count > 0:
            return metric_count / float(total_count)
        return 0
