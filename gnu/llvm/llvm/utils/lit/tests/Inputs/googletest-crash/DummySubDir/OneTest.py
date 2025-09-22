#!/usr/bin/env python

import os
import sys

if len(sys.argv) == 3 and sys.argv[1] == "--gtest_list_tests":
    if sys.argv[2] != "--gtest_filter=-*DISABLED_*":
        raise ValueError("unexpected argument: %s" % (sys.argv[2]))
    print(
        """\
FirstTest.
  subTestA
  subTestB
  subTestC
  subTestD
ParameterizedTest/0.
  subTest
ParameterizedTest/1.
  subTest"""
    )
    sys.exit(0)
elif len(sys.argv) != 1:
    # sharding and json output are specified using environment variables
    raise ValueError("unexpected argument: %r" % (" ".join(sys.argv[1:])))

for e in ["GTEST_TOTAL_SHARDS", "GTEST_SHARD_INDEX", "GTEST_OUTPUT"]:
    if e not in os.environ:
        raise ValueError("missing environment variables: " + e)

if not os.environ["GTEST_OUTPUT"].startswith("json:"):
    raise ValueError("must emit json output: " + os.environ["GTEST_OUTPUT"])

dummy_output = """\
{
"testsuites": [
]
}"""

if os.environ["GTEST_SHARD_INDEX"] == "0":
    print(
        """\
[----------] 4 test from FirstTest
[ RUN      ] FirstTest.subTestA
[       OK ] FirstTest.subTestA (18 ms)
[ RUN      ] FirstTest.subTestB""",
        flush=True,
    )
    print("I am about to crash", file=sys.stderr, flush=True)
    exit_code = 1
else:
    json_filename = os.environ["GTEST_OUTPUT"].split(":", 1)[1]
    with open(json_filename, "w", encoding="utf-8") as f:
        f.write(dummy_output)
    exit_code = 0

sys.exit(exit_code)
