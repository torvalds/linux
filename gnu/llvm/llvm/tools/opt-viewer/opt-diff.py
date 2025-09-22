#!/usr/bin/env python

from __future__ import print_function

desc = """Generate the difference of two YAML files into a new YAML file (works on
pair of directories too).  A new attribute 'Added' is set to True or False
depending whether the entry is added or removed from the first input to the
next.

The tools requires PyYAML."""

import yaml

# Try to use the C parser.
try:
    from yaml import CLoader as Loader
except ImportError:
    from yaml import Loader

import optrecord
import argparse
from collections import defaultdict

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=desc)
    parser.add_argument(
        "yaml_dir_or_file_1",
        help="An optimization record file or a directory searched for optimization "
        "record files that are used as the old version for the comparison",
    )
    parser.add_argument(
        "yaml_dir_or_file_2",
        help="An optimization record file or a directory searched for optimization "
        "record files that are used as the new version for the comparison",
    )
    parser.add_argument(
        "--jobs",
        "-j",
        default=None,
        type=int,
        help="Max job count (defaults to %(default)s, the current CPU count)",
    )
    parser.add_argument(
        "--max-size",
        "-m",
        default=100000,
        type=int,
        help="Maximum number of remarks stored in an output file",
    )
    parser.add_argument(
        "--no-progress-indicator",
        "-n",
        action="store_true",
        default=False,
        help="Do not display any indicator of how many YAML files were read.",
    )
    parser.add_argument("--output", "-o", default="diff{}.opt.yaml")
    args = parser.parse_args()

    files1 = optrecord.find_opt_files(args.yaml_dir_or_file_1)
    files2 = optrecord.find_opt_files(args.yaml_dir_or_file_2)

    print_progress = not args.no_progress_indicator
    all_remarks1, _, _ = optrecord.gather_results(files1, args.jobs, print_progress)
    all_remarks2, _, _ = optrecord.gather_results(files2, args.jobs, print_progress)

    added = set(all_remarks2.values()) - set(all_remarks1.values())
    removed = set(all_remarks1.values()) - set(all_remarks2.values())

    for r in added:
        r.Added = True
    for r in removed:
        r.Added = False

    result = list(added | removed)
    for r in result:
        r.recover_yaml_structure()

    for i in range(0, len(result), args.max_size):
        with open(args.output.format(i / args.max_size), "w") as stream:
            yaml.dump_all(result[i : i + args.max_size], stream)
