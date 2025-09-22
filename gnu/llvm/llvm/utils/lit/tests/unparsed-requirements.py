# RUN: %{python} %s %{inputs}/unparsed-requirements

import sys
from lit.Test import Result, Test, TestSuite
from lit.TestRunner import parseIntegratedTestScript
from lit.TestingConfig import TestingConfig

config = TestingConfig(
    None,
    "config",
    [".txt"],
    None,
    [],
    [],
    False,
    sys.argv[1],
    sys.argv[1],
    [],
    [],
    True,
)
suite = TestSuite("suite", sys.argv[1], sys.argv[1], config)

test = Test(suite, ["test.py"], config)
test.requires = ["meow"]
test.unsupported = ["alpha"]
test.xfails = ["foo"]

parseIntegratedTestScript(test)

error_count = 0
if test.requires != ["meow", "woof", "quack"]:
    error_count += 1
if test.unsupported != ["alpha", "beta", "gamma"]:
    error_count += 1
if test.xfails != ["foo", "bar", "baz"]:
    error_count += 1
exit(error_count)
