#!/usr/bin/env python

import os
import sys

if len(sys.argv) == 3 and sys.argv[1] == "--gtest_list_tests":
    if sys.argv[2] != "--gtest_filter=-*DISABLED_*":
        raise ValueError("unexpected argument: %s" % (sys.argv[2]))
    print(
        """\
T.
  QuickSubTest
  InfiniteLoopSubTest
"""
    )
    sys.exit(0)
elif len(sys.argv) != 1:
    # sharding and json output are specified using environment variables
    raise ValueError("unexpected argument: %r" % (" ".join(sys.argv[1:])))

for e in ["GTEST_TOTAL_SHARDS", "GTEST_SHARD_INDEX", "GTEST_OUTPUT", "GTEST_FILTER"]:
    if e not in os.environ:
        raise ValueError("missing environment variables: " + e)

if not os.environ["GTEST_OUTPUT"].startswith("json:"):
    raise ValueError("must emit json output: " + os.environ["GTEST_OUTPUT"])

output = """\
{
"testsuites": [
    {
        "name": "T",
        "testsuite": [
            {
                "name": "QuickSubTest",
                "result": "COMPLETED",
                "time": "2s"
            }
        ]
    }
]
}"""

dummy_output = """\
{
"testsuites": [
]
}"""

json_filename = os.environ["GTEST_OUTPUT"].split(":", 1)[1]

if os.environ["GTEST_SHARD_INDEX"] == "0":
    test_name = os.environ["GTEST_FILTER"]
    if test_name == "QuickSubTest":
        with open(json_filename, "w", encoding="utf-8") as f:
            f.write(output)
        exit_code = 0
    elif test_name == "InfiniteLoopSubTest":
        while True:
            pass
    else:
        raise SystemExit("error: invalid test name: %r" % (test_name,))
else:
    with open(json_filename, "w", encoding="utf-8") as f:
        f.write(dummy_output)
    exit_code = 0

sys.exit(exit_code)
