#!/usr/bin/env python3
# ===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===##

import os
import subprocess
import sys
import tempfile

cur_dir = os.path.dirname(os.path.realpath(__file__))
bisect_script = os.path.join(cur_dir, "..", "rsp_bisect.py")
test1 = os.path.join(cur_dir, "test_script.py")
test2 = os.path.join(cur_dir, "test_script_inv.py")
rsp = os.path.join(cur_dir, "rsp")


def run_bisect(success, test_script):
    args = [
        bisect_script,
        "--test",
        test_script,
        "--rsp",
        rsp,
        "--other-rel-path",
        "../Other",
    ]
    res = subprocess.run(args, capture_output=True, encoding="UTF-8")
    if len(sys.argv) > 1 and sys.argv[1] == "-v":
        print("Ran {} with return code {}".format(args, res.returncode))
        print("Stdout:")
        print(res.stdout)
        print("Stderr:")
        print(res.stderr)
    if res.returncode != (0 if success else 1):
        print(res.stdout)
        print(res.stderr)
        raise AssertionError("unexpected bisection return code for " + str(args))
    return res.stdout


# Test that an empty rsp file fails.
with open(rsp, "w") as f:
    pass

run_bisect(False, test1)

# Test that an rsp file without any paths fails.
with open(rsp, "w") as f:
    f.write("hello\nfoo\n")

run_bisect(False, test1)

# Test that an rsp file with one path succeeds.
with open(rsp, "w") as f:
    f.write("./foo\n")

output = run_bisect(True, test1)
assert "./foo" in output

# Test that an rsp file with one path and one extra arg succeeds.
with open(rsp, "w") as f:
    f.write("hello\n./foo\n")

output = run_bisect(True, test1)
assert "./foo" in output

# Test that an rsp file with three paths and one extra arg succeeds.
with open(rsp, "w") as f:
    f.write("hello\n./foo\n./bar\n./baz\n")

output = run_bisect(True, test1)
assert "./foo" in output

with open(rsp, "w") as f:
    f.write("hello\n./bar\n./foo\n./baz\n")

output = run_bisect(True, test1)
assert "./foo" in output

with open(rsp, "w") as f:
    f.write("hello\n./bar\n./baz\n./foo\n")

output = run_bisect(True, test1)
assert "./foo" in output

output = run_bisect(True, test2)
assert "./foo" in output

with open(rsp + ".0", "r") as f:
    contents = f.read()
    assert " ../Other/./foo" in contents

with open(rsp + ".1", "r") as f:
    contents = f.read()
    assert " ./foo" in contents

os.remove(rsp)
os.remove(rsp + ".0")
os.remove(rsp + ".1")

print("Success!")
