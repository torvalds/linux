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
from abi_regex import AbiRegex                  # pylint: disable=C0413
from helpers import ABI_DIR, DEBUG_HELP         # pylint: disable=C0413
from system_symbols import SystemSymbols        # pylint: disable=C0413

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

        for t in parser.doc(args.raw, not args.no_file):
            if args.enable_lineno:
                print (f".. LINENO {t[1]}#{t[2]}\n\n")

            print(t[0])

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

UNDEFINED_DESC="""
Check undefined ABIs on local machine.

Read sysfs devnodes and check if the devnodes there are defined inside
ABI documentation.

The search logic tries to minimize the number of regular expressions to
search per each symbol.

By default, it runs on a single CPU, as Python support for CPU threads
is still experimental, and multi-process runs on Python is very slow.

On experimental tests, if the number of ABI symbols to search per devnode
is contained on a limit of ~150 regular expressions, using a single CPU
is a lot faster than using multiple processes. However, if the number of
regular expressions to check is at the order of ~30000, using multiple
CPUs speeds up the check.
"""

class AbiUndefined:
    """
    Initialize an argparse subparser for logic to check undefined ABI at
    the current machine's sysfs
    """

    def __init__(self, subparsers):
        """Initialize argparse subparsers"""

        parser = subparsers.add_parser("undefined",
                                       formatter_class=argparse.RawTextHelpFormatter,
                                       description=UNDEFINED_DESC)

        parser.add_argument("-S", "--sysfs-dir", default="/sys",
                            help="directory where sysfs is mounted")
        parser.add_argument("-s", "--search-string",
                            help="search string regular expression to limit symbol search")
        parser.add_argument("-H", "--show-hints", action="store_true",
                            help="Hints about definitions for missing ABI symbols.")
        parser.add_argument("-j", "--jobs", "--max-workers", type=int, default=1,
                            help="If bigger than one, enables multiprocessing.")
        parser.add_argument("-c", "--max-chunk-size", type=int, default=50,
                            help="Maximum number of chunk size")
        parser.add_argument("-f", "--found", action="store_true",
                            help="Also show found items. "
                                 "Helpful to debug the parser."),
        parser.add_argument("-d", "--dry-run", action="store_true",
                            help="Don't actually search for undefined. "
                                 "Helpful to debug the parser."),

        parser.set_defaults(func=self.run)

    def run(self, args):
        """Run subparser"""

        abi = AbiRegex(args.dir, debug=args.debug,
                       search_string=args.search_string)

        abi_symbols = SystemSymbols(abi=abi, hints=args.show_hints,
                                    sysfs=args.sysfs_dir)

        abi_symbols.check_undefined_symbols(dry_run=args.dry_run,
                                            found=args.found,
                                            max_workers=args.jobs,
                                            chunk_size=args.max_chunk_size)


def main():
    """Main program"""

    parser = argparse.ArgumentParser(formatter_class=argparse.RawTextHelpFormatter)

    parser.add_argument("-d", "--debug", type=int, default=0, help="debug level")
    parser.add_argument("-D", "--dir", default=ABI_DIR, help=DEBUG_HELP)

    subparsers = parser.add_subparsers()

    AbiRest(subparsers)
    AbiValidate(subparsers)
    AbiSearch(subparsers)
    AbiUndefined(subparsers)

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
