#!/usr/bin/env python

from __future__ import absolute_import, division, print_function

import argparse
import difflib
import filecmp
import os
import subprocess
import sys

disassembler = "objdump"


def keep_line(line):
    """Returns true for lines that should be compared in the disassembly
    output."""
    return "file format" not in line


def disassemble(objfile):
    """Disassemble object to a file."""
    p = subprocess.Popen(
        [disassembler, "-d", objfile], stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    (out, err) = p.communicate()
    if p.returncode or err:
        print("Disassemble failed: {}".format(objfile))
        sys.exit(1)
    return [line for line in out.split(os.linesep) if keep_line(line)]


def dump_debug(objfile):
    """Dump all of the debug info from a file."""
    p = subprocess.Popen(
        [disassembler, "-WliaprmfsoRt", objfile],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    (out, err) = p.communicate()
    if p.returncode or err:
        print("Dump debug failed: {}".format(objfile))
        sys.exit(1)
    return [line for line in out.split(os.linesep) if keep_line(line)]


def first_diff(a, b, fromfile, tofile):
    """Returns the first few lines of a difference, if there is one.  Python
    diff can be very slow with large objects and the most interesting changes
    are the first ones. Truncate data before sending to difflib.  Returns None
    is there is no difference."""

    # Find first diff
    first_diff_idx = None
    for idx, val in enumerate(a):
        if val != b[idx]:
            first_diff_idx = idx
            break

    if first_diff_idx == None:
        # No difference
        return None

    # Diff to first line of diff plus some lines
    context = 3
    diff = difflib.unified_diff(
        a[: first_diff_idx + context], b[: first_diff_idx + context], fromfile, tofile
    )
    difference = "\n".join(diff)
    if first_diff_idx + context < len(a):
        difference += "\n*** Diff truncated ***"
    return difference


def compare_object_files(objfilea, objfileb):
    """Compare disassembly of two different files.
    Allowing unavoidable differences, such as filenames.
    Return the first difference if the disassembly differs, or None.
    """
    disa = disassemble(objfilea)
    disb = disassemble(objfileb)
    return first_diff(disa, disb, objfilea, objfileb)


def compare_debug_info(objfilea, objfileb):
    """Compare debug info of two different files.
    Allowing unavoidable differences, such as filenames.
    Return the first difference if the debug info differs, or None.
    If there are differences in the code, there will almost certainly be differences in the debug info too.
    """
    dbga = dump_debug(objfilea)
    dbgb = dump_debug(objfileb)
    return first_diff(dbga, dbgb, objfilea, objfileb)


def compare_exact(objfilea, objfileb):
    """Byte for byte comparison between object files.
    Returns True if equal, False otherwise.
    """
    return filecmp.cmp(objfilea, objfileb)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("objfilea", nargs=1)
    parser.add_argument("objfileb", nargs=1)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    diff = compare_object_files(args.objfilea[0], args.objfileb[0])
    if diff:
        print("Difference detected")
        if args.verbose:
            print(diff)
        sys.exit(1)
    else:
        print("The same")
