#!/usr/bin/env python3
# pylint: disable=R0903
# Copyright(c) 2025: Mauro Carvalho Chehab <mchehab@kernel.org>.
# SPDX-License-Identifier: GPL-2.0

"""
Parse ABI documentation and produce results from it.
"""

import argparse
import logging
import os
import sys

# Import Python modules

LIB_DIR = "lib/abi"
SRC_DIR = os.path.dirname(os.path.realpath(__file__))

sys.path.insert(0, os.path.join(SRC_DIR, LIB_DIR))

from abi_parser import AbiParser                # pylint: disable=C0413
from helpers import ABI_DIR, DEBUG_HELP         # pylint: disable=C0413

# Command line classes


REST_DESC = """
Produce output in ReST format.

The output is done on two sections:

- Symbols: show all parsed symbols in alphabetic order;
- Files: cross reference the content of each file with the symbols on it.
"""

class AbiRest:
    """Initialize an argparse subparser for rest output"""

    def __init__(self, subparsers):
        """Initialize argparse subparsers"""

        parser = subparsers.add_parser("rest",
                                       formatter_class=argparse.RawTextHelpFormatter,
                                       description=REST_DESC)

        parser.add_argument("--enable-lineno",  action="store_true",
                            help="enable lineno")
        parser.add_argument("--raw", action="store_true",
                            help="output text as contained in the ABI files. "
                                 "It not used, output will contain dynamically"
                                 " generated cross references when possible.")
        parser.add_argument("--no-file", action="store_true",
                            help="Don't the files section")
        parser.add_argument("--show-hints", help="Show-hints")

        parser.set_defaults(func=self.run)

    def run(self, args):
        """Run subparser"""

        parser = AbiParser(args.dir, debug=args.debug)
        parser.parse_abi()
        parser.check_issues()

        for msg in parser.doc(args.enable_lineno, args.raw, not args.no_file):
            print(msg)

class AbiValidate:
    """Initialize an argparse subparser for ABI validation"""

    def __init__(self, subparsers):
        """Initialize argparse subparsers"""

        parser = subparsers.add_parser("validate",
                                       formatter_class=argparse.ArgumentDefaultsHelpFormatter,
                                       description="list events")

        parser.set_defaults(func=self.run)

    def run(self, args):
        """Run subparser"""

        parser = AbiParser(args.dir, debug=args.debug)
        parser.parse_abi()
        parser.check_issues()


class AbiSearch:
    """Initialize an argparse subparser for ABI search"""

    def __init__(self, subparsers):
        """Initialize argparse subparsers"""

        parser = subparsers.add_parser("search",
                                       formatter_class=argparse.ArgumentDefaultsHelpFormatter,
                                       description="Search ABI using a regular expression")

        parser.add_argument("expression",
                            help="Case-insensitive search pattern for the ABI symbol")

        parser.set_defaults(func=self.run)

    def run(self, args):
        """Run subparser"""

        parser = AbiParser(args.dir, debug=args.debug)
        parser.parse_abi()
        parser.search_symbols(args.expression)


def main():
    """Main program"""

    parser = argparse.ArgumentParser(formatter_class=argparse.RawTextHelpFormatter)

    parser.add_argument("-d", "--debug", type=int, default=0, help="debug level")
    parser.add_argument("-D", "--dir", default=ABI_DIR, help=DEBUG_HELP)

    subparsers = parser.add_subparsers()

    AbiRest(subparsers)
    AbiValidate(subparsers)
    AbiSearch(subparsers)

    args = parser.parse_args()

    if args.debug:
        level = logging.DEBUG
    else:
        level = logging.INFO

    logging.basicConfig(level=level, format="[%(levelname)s] %(message)s")

    if "func" in args:
        args.func(args)
    else:
        sys.exit(f"Please specify a valid command for {sys.argv[0]}")


# Call main method
if __name__ == "__main__":
    main()
