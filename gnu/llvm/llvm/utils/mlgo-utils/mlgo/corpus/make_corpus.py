# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Tool for making a corpus from arbitrary bitcode.

To create a corpus from a set of bitcode files in an input directory, run
the following command:

PYTHONPATH=$PYTHONPATH:. python3 ./compiler_opt/tools/make_corpus.py \
  --input_dir=<path to input directory> \
  --output_dir=<path to output directory> \
  --default_args="<list of space separated flags>"
"""

import argparse
import logging

from mlgo.corpus import make_corpus_lib


def parse_args_and_run():
    parser = argparse.ArgumentParser(
        description="A tool for making a corpus from arbitrary bitcode"
    )
    parser.add_argument("--input_dir", type=str, help="The input directory.")
    parser.add_argument("--output_dir", type=str, help="The output directory.")
    parser.add_argument(
        "--default_args",
        type=str,
        help="The compiler flags to compile with when using downstream tooling.",
        default="",
        nargs="?",
    )
    args = parser.parse_args()
    main(args)


def main(args):
    logging.warning(
        "Using this tool does not guarantee that the bitcode is taken at "
        "the correct stage for consumption during model training. Make "
        "sure to validate assumptions about where the bitcode is coming "
        "from before using it in production."
    )
    relative_paths = make_corpus_lib.load_bitcode_from_directory(args.input_dir)
    make_corpus_lib.copy_bitcode(relative_paths, args.input_dir, args.output_dir)
    make_corpus_lib.write_corpus_manifest(
        relative_paths, args.output_dir, args.default_args.split()
    )


if __name__ == "__main__":
    parse_args_and_run()
