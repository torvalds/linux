import os


def read_test_times(suite):
    test_times = {}
    test_times_file = os.path.join(suite.exec_root, ".lit_test_times.txt")
    if not os.path.exists(test_times_file):
        test_times_file = os.path.join(suite.source_root, ".lit_test_times.txt")
    if os.path.exists(test_times_file):
        with open(test_times_file, "r") as time_file:
            for line in time_file:
                time, path = line.split(maxsplit=1)
                test_times[path.strip("\n")] = float(time)
    return test_times


def record_test_times(tests, lit_config):
    times_by_suite = {}
    for t in tests:
        assert t.suite.test_times is None
        if t.result.elapsed is None:
            continue
        if not t.suite.exec_root in times_by_suite:
            times_by_suite[t.suite.exec_root] = read_test_times(t.suite)
        time = -t.result.elapsed if t.isFailure() else t.result.elapsed
        # The "path" here is only used as a key into a dictionary. It is never
        # used as an actual path to a filesystem API, therefore we use '/' as
        # the canonical separator so that Unix and Windows machines can share
        # timing data.
        times_by_suite[t.suite.exec_root]["/".join(t.path_in_suite)] = time

    for s, value in times_by_suite.items():
        try:
            path = os.path.join(s, ".lit_test_times.txt")
            with open(path, "w") as time_file:
                for name, time in value.items():
                    time_file.write(("%e" % time) + " " + name + "\n")
        except:
            lit_config.warning("Could not save test time: " + path)
            continue
