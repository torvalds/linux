#!/usr/bin/env python

desc = """
A script to extract ConstraintElimination's reproducer remarks. The extracted
modules are written as textual LLVM IR to files named reproducerXXXX.ll in the
current directory.
"""

import optrecord
import argparse

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=desc)
    parser.add_argument(
        "yaml_dirs_or_files",
        nargs="+",
        help="List of optimization record files or directories searched "
        "for optimization record files.",
    )

    args = parser.parse_args()

    print_progress = False
    jobs = 1

    files = optrecord.find_opt_files(*args.yaml_dirs_or_files)
    if not files:
        parser.error("No *.opt.yaml files found")
        sys.exit(1)

    all_remarks, file_remarks, _ = optrecord.gather_results(files, jobs, True)

    i = 0
    for r in all_remarks:
        if r[1] != "constraint-elimination" or r[2] != "Reproducer":
            continue
        with open("reproducer{}.ll".format(i), "wt") as f:
            f.write(r[7][1][0][1])
        i += 1
