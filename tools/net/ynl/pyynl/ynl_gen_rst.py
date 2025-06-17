#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# -*- coding: utf-8; mode: python -*-

"""
    Script to auto generate the documentation for Netlink specifications.

    :copyright:  Copyright (C) 2023  Breno Leitao <leitao@debian.org>
    :license:    GPL Version 2, June 1991 see linux/COPYING for details.

    This script performs extensive parsing to the Linux kernel's netlink YAML
    spec files, in an effort to avoid needing to heavily mark up the original
    YAML file. It uses the library code from scripts/lib.
"""

import os.path
import pathlib
import sys
import argparse
import logging

sys.path.append(pathlib.Path(__file__).resolve().parent.as_posix())
from lib import YnlDocGenerator    # pylint: disable=C0413

def parse_arguments() -> argparse.Namespace:
    """Parse arguments from user"""
    parser = argparse.ArgumentParser(description="Netlink RST generator")

    parser.add_argument("-v", "--verbose", action="store_true")
    parser.add_argument("-o", "--output", help="Output file name")

    # Index and input are mutually exclusive
    group = parser.add_mutually_exclusive_group()
    group.add_argument("-i", "--input", help="YAML file name")

    args = parser.parse_args()

    if args.verbose:
        logging.basicConfig(level=logging.DEBUG)

    if args.input and not os.path.isfile(args.input):
        logging.warning("%s is not a valid file.", args.input)
        sys.exit(-1)

    if not args.output:
        logging.error("No output file specified.")
        sys.exit(-1)

    if os.path.isfile(args.output):
        logging.debug("%s already exists. Overwriting it.", args.output)

    return args


def write_to_rstfile(content: str, filename: str) -> None:
    """Write the generated content into an RST file"""
    logging.debug("Saving RST file to %s", filename)

    with open(filename, "w", encoding="utf-8") as rst_file:
        rst_file.write(content)


def main() -> None:
    """Main function that reads the YAML files and generates the RST files"""

    args = parse_arguments()

    parser = YnlDocGenerator()

    if args.input:
        logging.debug("Parsing %s", args.input)
        try:
            content = parser.parse_yaml_file(os.path.join(args.input))
        except Exception as exception:
            logging.warning("Failed to parse %s.", args.input)
            logging.warning(exception)
            sys.exit(-1)

        write_to_rstfile(content, args.output)


if __name__ == "__main__":
    main()
