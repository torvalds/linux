# -*- Python -*- vim: set ft=python ts=4 sw=4 expandtab tw=79:
# Configuration file for the 'lit' test runner.
import os
import site

site.addsitedir(os.path.join(os.path.dirname(os.path.dirname(__file__)), "utils"))
from libcxx.test.googlebenchmark import GoogleBenchmark

# Tell pylint that we know config and lit_config exist somewhere.
if "PYLINT_IMPORT" in os.environ:
    config = object()
    lit_config = object()

# name: The name of this test suite.
config.name = "libc++ benchmarks"
config.suffixes = []

config.test_exec_root = os.path.join(config.libcxx_obj_root, "benchmarks")
config.test_source_root = config.test_exec_root

config.test_format = GoogleBenchmark(
    test_sub_dirs=".", test_suffix=".bench.out", benchmark_args=config.benchmark_args
)
