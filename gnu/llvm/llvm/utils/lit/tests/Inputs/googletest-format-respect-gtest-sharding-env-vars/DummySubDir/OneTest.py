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

output = """\
{
"random_seed": 123,
"testsuites": [
    {
        "name": "FirstTest",
        "testsuite": [
            {
                "name": "subTestA",
                "result": "COMPLETED",
                "time": "0.001s"
            },
            {
                "name": "subTestB",
                "result": "COMPLETED",
                "time": "0.001s",
                "failures": [
                    {
                        "failure": "I am subTest B, I FAIL\\nAnd I have two lines of output",
                        "type": ""
                    }
                ]
            },
            {
                "name": "subTestC",
                "result": "SKIPPED",
                "time": "0.001s"
            },
            {
                "name": "subTestD",
                "result": "UNRESOLVED",
                "time": "0.001s"
            }
        ]
    },
    {
        "name": "ParameterizedTest/0",
        "testsuite": [
            {
                "name": "subTest",
                "result": "COMPLETED",
                "time": "0.001s"
            }
        ]
    },
    {
        "name": "ParameterizedTest/1",
        "testsuite": [
            {
                "name": "subTest",
                "result": "COMPLETED",
                "time": "0.001s"
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
with open(json_filename, "w", encoding="utf-8") as f:
    if os.environ["GTEST_TOTAL_SHARDS"] == "1":
        print("[ RUN      ] FirstTest.subTestB", flush=True)
        print("I am subTest B output", file=sys.stderr, flush=True)
        print("[  FAILED  ] FirstTest.subTestB (8 ms)", flush=True)

        f.write(output)
        exit_code = 1
    else:
        f.write(dummy_output)
        exit_code = 0

sys.exit(exit_code)
