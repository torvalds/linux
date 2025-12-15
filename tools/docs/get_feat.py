#!/usr/bin/env python3
# pylint: disable=R0902,R0911,R0912,R0914,R0915
# Copyright(c) 2025: Mauro Carvalho Chehab <mchehab@kernel.org>.
# SPDX-License-Identifier: GPL-2.0


"""
Parse the Linux Feature files and produce a ReST book.
"""

import argparse
import os
import subprocess
import sys

from pprint import pprint

LIB_DIR = "../../tools/lib/python"
SRC_DIR = os.path.dirname(os.path.realpath(__file__))

sys.path.insert(0, os.path.join(SRC_DIR, LIB_DIR))

from feat.parse_features import ParseFeature                # pylint: disable=C0413

SRCTREE = os.path.join(os.path.dirname(os.path.realpath(__file__)), "../..")
DEFAULT_DIR = "Documentation/features"


class GetFeature:
    """Helper class to parse feature parsing parameters"""

    @staticmethod
    def get_current_arch():
        """Detects the current architecture"""

        proc = subprocess.run(["uname", "-m"], check=True,
                              capture_output=True, text=True)

        arch = proc.stdout.strip()
        if arch in ["x86_64", "i386"]:
            arch = "x86"
        elif arch == "s390x":
            arch = "s390"

        return arch

    def run_parser(self, args):
        """Execute the feature parser"""

        feat = ParseFeature(args.directory, args.debug, args.enable_fname)
        data = feat.parse()

        if args.debug > 2:
            pprint(data)

        return feat

    def run_rest(self, args):
        """
        Generate tables in ReST format. Three types of tables are
        supported, depending on the calling arguments:

        - neither feature nor arch is passed: generates a full matrix;
        - arch provided: generates a table of supported tables for the
          guiven architecture, eventually filtered by feature;
        - only feature provided: generates a table with feature details,
          showing what architectures it is implemented.
        """

        feat = self.run_parser(args)

        if args.arch:
            rst = feat.output_arch_table(args.arch, args.feat)
        elif args.feat:
            rst = feat.output_feature(args.feat)
        else:
            rst = feat.output_matrix()

        print(rst)

    def run_current(self, args):
        """
        Instead of using a --arch parameter, get feature for the current
        architecture.
        """

        args.arch = self.get_current_arch()

        self.run_rest(args)

    def run_list(self, args):
        """
        Generate a list of features for a given architecture, in a format
        parseable by other scripts. The output format is not ReST.
        """

        if not args.arch:
            args.arch = self.get_current_arch()

        feat = self.run_parser(args)
        msg = feat.list_arch_features(args.arch, args.feat)

        print(msg)

    def parse_arch(self, parser):
        """Add a --arch parsing argument"""

        parser.add_argument("--arch",
                            help="Output features for an specific"
                                 " architecture, optionally filtering for a "
                                 "single specific feature.")

    def parse_feat(self, parser):
        """Add a --feat parsing argument"""

        parser.add_argument("--feat", "--feature",
                            help="Output features for a single specific "
                                  "feature.")


    def current_args(self, subparsers):
        """Implementscurrent argparse subparser"""

        parser = subparsers.add_parser("current",
                                       formatter_class=argparse.RawTextHelpFormatter,
                                       description="Output table in ReST "
                                                   "compatible ASCII format "
                                                   "with features for this "
                                                   "machine's architecture")

        self.parse_feat(parser)
        parser.set_defaults(func=self.run_current)

    def rest_args(self, subparsers):
        """Implement rest argparse subparser"""

        parser = subparsers.add_parser("rest",
                                       formatter_class=argparse.RawTextHelpFormatter,
                                       description="Output table(s) in ReST "
                                                   "compatible ASCII format "
                                                   "with features in ReST "
                                                   "markup language. The "
                                                   "output is affected by "
                                                   "--arch or --feat/--feature"
                                                   " flags.")

        self.parse_arch(parser)
        self.parse_feat(parser)
        parser.set_defaults(func=self.run_rest)

    def list_args(self, subparsers):
        """Implement list argparse subparser"""

        parser = subparsers.add_parser("list",
                                       formatter_class=argparse.RawTextHelpFormatter,
                                       description="List features for this "
                                                   "machine's architecture, "
                                                   "using an easier to parse "
                                                   "format. The output is "
                                                   "affected by --arch flag.")

        self.parse_arch(parser)
        self.parse_feat(parser)
        parser.set_defaults(func=self.run_list)

    def validate_args(self, subparsers):
        """Implement validate argparse subparser"""

        parser = subparsers.add_parser("validate",
                                       formatter_class=argparse.RawTextHelpFormatter,
                                       description="Validate the contents of "
                                                   "the files under "
                                                   f"{DEFAULT_DIR}.")

        parser.set_defaults(func=self.run_parser)

    def parser(self):
        """
        Create an arparse with common options and several subparsers
        """
        parser = argparse.ArgumentParser(formatter_class=argparse.RawTextHelpFormatter)

        parser.add_argument("-d", "--debug", action="count", default=0,
                            help="Put the script in verbose mode, useful for "
                                 "debugging. Can be called multiple times, to "
                                 "increase verbosity.")

        parser.add_argument("--directory", "--dir", default=DEFAULT_DIR,
                            help="Changes the location of the Feature files. "
                                 f"By default, it uses the {DEFAULT_DIR} "
                                 "directory.")

        parser.add_argument("--enable-fname", action="store_true",
                            help="Prints the file name of the feature files. "
                                 "This can be used in order to track "
                                 "dependencies during documentation build.")

        subparsers = parser.add_subparsers()

        self.current_args(subparsers)
        self.rest_args(subparsers)
        self.list_args(subparsers)
        self.validate_args(subparsers)

        args = parser.parse_args()

        return args


def main():
    """Main program"""

    feat = GetFeature()

    args = feat.parser()

    if "func" in args:
        args.func(args)
    else:
        sys.exit(f"Please specify a valid command for {sys.argv[0]}")


# Call main method
if __name__ == "__main__":
    main()
