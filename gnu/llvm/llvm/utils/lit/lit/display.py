import sys


def create_display(opts, tests, total_tests, workers):
    if opts.quiet:
        return NopDisplay()

    num_tests = len(tests)
    of_total = (" of %d" % total_tests) if (num_tests != total_tests) else ""
    header = "-- Testing: %d%s tests, %d workers --" % (num_tests, of_total, workers)

    progress_bar = None
    if opts.succinct and opts.useProgressBar:
        import lit.ProgressBar

        try:
            tc = lit.ProgressBar.TerminalController()
            progress_bar = lit.ProgressBar.ProgressBar(tc, header)
            header = None
        except ValueError:
            progress_bar = lit.ProgressBar.SimpleProgressBar("Testing: ")

    return Display(opts, tests, header, progress_bar)


class ProgressPredictor(object):
    def __init__(self, tests):
        self.completed = 0
        self.time_elapsed = 0.0
        self.predictable_tests_remaining = 0
        self.predictable_time_remaining = 0.0
        self.unpredictable_tests_remaining = 0

        for test in tests:
            if test.previous_elapsed:
                self.predictable_tests_remaining += 1
                self.predictable_time_remaining += test.previous_elapsed
            else:
                self.unpredictable_tests_remaining += 1

    def update(self, test):
        self.completed += 1
        self.time_elapsed += test.result.elapsed

        if test.previous_elapsed:
            self.predictable_tests_remaining -= 1
            self.predictable_time_remaining -= test.previous_elapsed
        else:
            self.unpredictable_tests_remaining -= 1

        # NOTE: median would be more precise, but might be too slow.
        average_test_time = (self.time_elapsed + self.predictable_time_remaining) / (
            self.completed + self.predictable_tests_remaining
        )
        unpredictable_time_remaining = (
            average_test_time * self.unpredictable_tests_remaining
        )
        total_time_remaining = (
            self.predictable_time_remaining + unpredictable_time_remaining
        )
        total_time = self.time_elapsed + total_time_remaining

        if total_time > 0:
            return self.time_elapsed / total_time
        return 0


class NopDisplay(object):
    def print_header(self):
        pass

    def update(self, test):
        pass

    def clear(self, interrupted):
        pass


class Display(object):
    def __init__(self, opts, tests, header, progress_bar):
        self.opts = opts
        self.num_tests = len(tests)
        self.header = header
        self.progress_predictor = ProgressPredictor(tests) if progress_bar else None
        self.progress_bar = progress_bar
        self.completed = 0

    def print_header(self):
        if self.header:
            print(self.header)
        if self.progress_bar:
            self.progress_bar.update(0.0, "")

    def update(self, test):
        self.completed += 1

        show_result = (
            test.isFailure()
            or self.opts.showAllOutput
            or (not self.opts.quiet and not self.opts.succinct)
        )
        if show_result:
            if self.progress_bar:
                self.progress_bar.clear(interrupted=False)
            self.print_result(test)

        if self.progress_bar:
            if test.isFailure():
                self.progress_bar.barColor = "RED"
            percent = self.progress_predictor.update(test)
            self.progress_bar.update(percent, test.getFullName())

    def clear(self, interrupted):
        if self.progress_bar:
            self.progress_bar.clear(interrupted)

    def print_result(self, test):
        # Show the test result line.
        test_name = test.getFullName()
        print(
            "%s: %s (%d of %d)"
            % (test.result.code.name, test_name, self.completed, self.num_tests)
        )

        # Show the test failure output, if requested.
        if (test.isFailure() and self.opts.showOutput) or self.opts.showAllOutput:
            if test.isFailure():
                print(
                    "%s TEST '%s' FAILED %s" % ("*" * 20, test.getFullName(), "*" * 20)
                )
            out = test.result.output
            # Encode/decode so that, when using Python 3.6.5 in Windows 10,
            # print(out) doesn't raise UnicodeEncodeError if out contains
            # special characters.  However, Python 2 might try to decode
            # as part of the encode call if out is already encoded, so skip
            # encoding if it raises UnicodeDecodeError.
            if sys.stdout.encoding:
                try:
                    out = out.encode(encoding=sys.stdout.encoding, errors="replace")
                except UnicodeDecodeError:
                    pass
                # Python 2 can raise UnicodeDecodeError here too in cases
                # where the stdout encoding is ASCII. Ignore decode errors
                # in this case.
                out = out.decode(encoding=sys.stdout.encoding, errors="ignore")
            print(out)
            print("*" * 20)

        # Report test metrics, if present.
        if test.result.metrics:
            print("%s TEST '%s' RESULTS %s" % ("*" * 10, test.getFullName(), "*" * 10))
            items = sorted(test.result.metrics.items())
            for metric_name, value in items:
                print("%s: %s " % (metric_name, value.format()))
            print("*" * 10)

        # Report micro-tests, if present
        if test.result.microResults:
            items = sorted(test.result.microResults.items())
            for micro_test_name, micro_test in items:
                print("%s MICRO-TEST: %s" % ("*" * 3, micro_test_name))

                if micro_test.metrics:
                    sorted_metrics = sorted(micro_test.metrics.items())
                    for metric_name, value in sorted_metrics:
                        print("    %s:  %s " % (metric_name, value.format()))

        # Ensure the output is flushed.
        sys.stdout.flush()
