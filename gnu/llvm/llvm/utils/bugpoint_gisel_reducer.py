#!/usr/bin/env python

"""Reduces GlobalISel failures.

This script is a utility to reduce tests that GlobalISel
fails to compile.

It runs llc to get the error message using a regex and creates
a custom command to check that specific error. Then, it runs bugpoint
with the custom command.

"""
from __future__ import print_function
import argparse
import re
import subprocess
import sys
import tempfile
import os


def log(msg):
    print(msg)


def hr():
    log("-" * 50)


def log_err(msg):
    print("ERROR: {}".format(msg), file=sys.stderr)


def check_path(path):
    if not os.path.exists(path):
        log_err("{} does not exist.".format(path))
        raise
    return path


def check_bin(build_dir, bin_name):
    file_name = "{}/bin/{}".format(build_dir, bin_name)
    return check_path(file_name)


def run_llc(llc, irfile):
    pr = subprocess.Popen(
        [llc, "-o", "-", "-global-isel", "-pass-remarks-missed=gisel", irfile],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    out, err = pr.communicate()
    res = pr.wait()
    if res == 0:
        return 0
    re_err = re.compile(
        r"LLVM ERROR: ([a-z\s]+):.*(G_INTRINSIC[_A-Z]* <intrinsic:@[a-zA-Z0-9\.]+>|G_[A-Z_]+)"
    )
    match = re_err.match(err)
    if not match:
        return 0
    else:
        return [match.group(1), match.group(2)]


def run_bugpoint(bugpoint_bin, llc_bin, opt_bin, tmp, ir_file):
    compileCmd = "-compile-command={} -c {} {}".format(
        os.path.realpath(__file__), llc_bin, tmp
    )
    pr = subprocess.Popen(
        [
            bugpoint_bin,
            "-compile-custom",
            compileCmd,
            "-opt-command={}".format(opt_bin),
            ir_file,
        ]
    )
    res = pr.wait()
    if res != 0:
        log_err("Unable to reduce the test.")
        raise


def run_bugpoint_check():
    path_to_llc = sys.argv[2]
    path_to_err = sys.argv[3]
    path_to_ir = sys.argv[4]
    with open(path_to_err, "r") as f:
        err = f.read()
        res = run_llc(path_to_llc, path_to_ir)
        if res == 0:
            return 0
        log("GlobalISed failed, {}: {}".format(res[0], res[1]))
        if res != err.split(";"):
            return 0
        else:
            return 1


def main():
    # Check if this is called by bugpoint.
    if len(sys.argv) == 5 and sys.argv[1] == "-c":
        sys.exit(run_bugpoint_check())

    # Parse arguments.
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawTextHelpFormatter
    )
    parser.add_argument("BuildDir", help="Path to LLVM build directory")
    parser.add_argument("IRFile", help="Path to the input IR file")
    args = parser.parse_args()

    # Check if the binaries exist.
    build_dir = check_path(args.BuildDir)
    ir_file = check_path(args.IRFile)
    llc_bin = check_bin(build_dir, "llc")
    opt_bin = check_bin(build_dir, "opt")
    bugpoint_bin = check_bin(build_dir, "bugpoint")

    # Run llc to see if GlobalISel fails.
    log("Running llc...")
    res = run_llc(llc_bin, ir_file)
    if res == 0:
        log_err("Expected failure")
        raise
    hr()
    log("GlobalISel failed, {}: {}.".format(res[0], res[1]))
    tmp = tempfile.NamedTemporaryFile()
    log("Writing error to {} for bugpoint.".format(tmp.name))
    tmp.write(";".join(res))
    tmp.flush()
    hr()

    # Run bugpoint.
    log("Running bugpoint...")
    run_bugpoint(bugpoint_bin, llc_bin, opt_bin, tmp.name, ir_file)
    hr()
    log("Done!")
    hr()
    output_file = "bugpoint-reduced-simplified.bc"
    log("Run llvm-dis to disassemble the output:")
    log("$ {}/bin/llvm-dis -o - {}".format(build_dir, output_file))
    log("Run llc to reproduce the problem:")
    log(
        "$ {}/bin/llc -o - -global-isel "
        "-pass-remarks-missed=gisel {}".format(build_dir, output_file)
    )


if __name__ == "__main__":
    main()
