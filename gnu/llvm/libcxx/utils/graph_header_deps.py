#!/usr/bin/env python
# ===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===##

import argparse

if __name__ == "__main__":
    """Converts a header dependency CSV file to Graphviz dot file.

    The header dependency CSV files are found on the directory
    libcxx/test/libcxx/transitive_includes
    """

    parser = argparse.ArgumentParser(
        description="""Converts a libc++ dependency CSV file to a Graphviz dot file.
For example:
  libcxx/utils/graph_header_deps.py libcxx/test/libcxx/transitive_includes/cxx20.csv | dot -Tsvg > graph.svg
""",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "input",
        default=None,
        metavar="FILE",
        help="The header dependency CSV file.",
    )
    options = parser.parse_args()

    print(
        """digraph includes {
graph [nodesep=0.5, ranksep=1];
node [shape=box, width=4];"""
    )
    with open(options.input, "r") as f:
        for line in f.readlines():
            elements = line.rstrip().split(" ")
            assert len(elements) == 2

            print(f'\t"{elements[0]}" -> "{elements[1]}"')

    print("}")
