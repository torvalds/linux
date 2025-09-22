#!/usr/bin/env python3
# ===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===##
"""Script to bisect over files in an rsp file.

This is mostly used for detecting which file contains a miscompile between two
compiler revisions. It does this by bisecting over an rsp file. Between two
build directories, this script will make the rsp file reference the current
build directory's version of some set of the rsp's object files/libraries, and
reference the other build directory's version of the same files for the
remaining set of object files/libraries.

Build the target in two separate directories with the two compiler revisions,
keeping the rsp file around since ninja by default deletes the rsp file after
building.
$ ninja -d keeprsp mytarget

Create a script to build the target and run an interesting test. Get the
command to build the target via
$ ninja -t commands | grep mytarget
The command to build the target should reference the rsp file.
This script doesn't care if the test script returns 0 or 1 for specifically the
successful or failing test, just that the test script returns a different
return code for success vs failure.
Since the command that `ninja -t commands` is run from the build directory,
usually the test script cd's to the build directory.

$ rsp_bisect.py --test=path/to/test_script --rsp=path/to/build/target.rsp
    --other_rel_path=../Other
where --other_rel_path is the relative path from the first build directory to
the other build directory. This is prepended to files in the rsp.


For a full example, if the foo target is suspected to contain a miscompile in
some file, have two different build directories, buildgood/ and buildbad/ and
run
$ ninja -d keeprsp foo
in both so we have two versions of all relevant object files that may contain a
miscompile, one built by a good compiler and one by a bad compiler.

In buildgood/, run
$ ninja -t commands | grep '-o .*foo'
to get the command to link the files together. It may look something like
  clang -o foo @foo.rsp

Now create a test script that runs the link step and whatever test reproduces a
miscompile and returns a non-zero exit code when there is a miscompile. For
example
```
  #!/bin/bash
  # immediately bail out of script if any command returns a non-zero return code
  set -e
  clang -o foo @foo.rsp
  ./foo
```

With buildgood/ as the working directory, run
$ path/to/llvm-project/llvm/utils/rsp_bisect.py \
    --test=path/to/test_script --rsp=./foo.rsp --other_rel_path=../buildbad/
If rsp_bisect is successful, it will print the first file in the rsp file that
when using the bad build directory's version causes the test script to return a
different return code. foo.rsp.0 and foo.rsp.1 will also be written. foo.rsp.0
will be a copy of foo.rsp with the relevant file using the version in
buildgood/, and foo.rsp.1 will be a copy of foo.rsp with the relevant file
using the version in buildbad/.

"""

import argparse
import os
import subprocess
import sys


def is_path(s):
    return "/" in s


def run_test(test):
    """Runs the test and returns whether it was successful or not."""
    return subprocess.run([test], capture_output=True).returncode == 0


def modify_rsp(rsp_entries, other_rel_path, modify_after_num):
    """Create a modified rsp file for use in bisection.

    Returns a new list from rsp.
    For each file in rsp after the first modify_after_num files, prepend
    other_rel_path.
    """
    ret = []
    for r in rsp_entries:
        if is_path(r):
            if modify_after_num == 0:
                r = os.path.join(other_rel_path, r)
            else:
                modify_after_num -= 1
        ret.append(r)
    assert modify_after_num == 0
    return ret


def test_modified_rsp(test, modified_rsp_entries, rsp_path):
    """Write the rsp file to disk and run the test."""
    with open(rsp_path, "w") as f:
        f.write(" ".join(modified_rsp_entries))
    return run_test(test)


def bisect(test, zero_result, rsp_entries, num_files_in_rsp, other_rel_path, rsp_path):
    """Bisect over rsp entries.

    Args:
        zero_result: the test result when modify_after_num is 0.

    Returns:
        The index of the file in the rsp file where the test result changes.
    """
    lower = 0
    upper = num_files_in_rsp
    while lower != upper - 1:
        assert lower < upper - 1
        mid = int((lower + upper) / 2)
        assert lower != mid and mid != upper
        print("Trying {} ({}-{})".format(mid, lower, upper))
        result = test_modified_rsp(
            test, modify_rsp(rsp_entries, other_rel_path, mid), rsp_path
        )
        if zero_result == result:
            lower = mid
        else:
            upper = mid
    return upper


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--test", help="Binary to test if current setup is good or bad", required=True
    )
    parser.add_argument("--rsp", help="rsp file", required=True)
    parser.add_argument(
        "--other-rel-path",
        help="Relative path from current build directory to other build "
        + 'directory, e.g. from "out/Default" to "out/Other" specify "../Other"',
        required=True,
    )
    args = parser.parse_args()

    with open(args.rsp, "r") as f:
        rsp_entries = f.read()
    rsp_entries = rsp_entries.split()
    num_files_in_rsp = sum(1 for a in rsp_entries if is_path(a))
    if num_files_in_rsp == 0:
        print("No files in rsp?")
        return 1
    print("{} files in rsp".format(num_files_in_rsp))

    try:
        print("Initial testing")
        test0 = test_modified_rsp(
            args.test, modify_rsp(rsp_entries, args.other_rel_path, 0), args.rsp
        )
        test_all = test_modified_rsp(
            args.test,
            modify_rsp(rsp_entries, args.other_rel_path, num_files_in_rsp),
            args.rsp,
        )

        if test0 == test_all:
            print("Test returned same exit code for both build directories")
            return 1

        print("First build directory returned " + ("0" if test_all else "1"))

        result = bisect(
            args.test,
            test0,
            rsp_entries,
            num_files_in_rsp,
            args.other_rel_path,
            args.rsp,
        )
        print(
            "First file change: {} ({})".format(
                list(filter(is_path, rsp_entries))[result - 1], result
            )
        )

        rsp_out_0 = args.rsp + ".0"
        rsp_out_1 = args.rsp + ".1"
        with open(rsp_out_0, "w") as f:
            f.write(" ".join(modify_rsp(rsp_entries, args.other_rel_path, result - 1)))
        with open(rsp_out_1, "w") as f:
            f.write(" ".join(modify_rsp(rsp_entries, args.other_rel_path, result)))
        print(
            "Bisection point rsp files written to {} and {}".format(
                rsp_out_0, rsp_out_1
            )
        )
    finally:
        # Always make sure to write the original rsp file contents back so it's
        # less of a pain to rerun this script.
        with open(args.rsp, "w") as f:
            f.write(" ".join(rsp_entries))


if __name__ == "__main__":
    sys.exit(main())
