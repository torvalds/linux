#!/usr/bin/env python

# ===- cindex-dump.py - cindex/Python Source Dump -------------*- python -*--===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===------------------------------------------------------------------------===#

"""
A simple command line tool for dumping a source file using the Clang Index
Library.
"""


def get_diag_info(diag):
    return {
        "severity": diag.severity,
        "location": diag.location,
        "spelling": diag.spelling,
        "ranges": diag.ranges,
        "fixits": diag.fixits,
    }


def get_cursor_id(cursor, cursor_list=[]):
    if not opts.showIDs:
        return None

    if cursor is None:
        return None

    # FIXME: This is really slow. It would be nice if the index API exposed
    # something that let us hash cursors.
    for i, c in enumerate(cursor_list):
        if cursor == c:
            return i
    cursor_list.append(cursor)
    return len(cursor_list) - 1


def get_info(node, depth=0):
    if opts.maxDepth is not None and depth >= opts.maxDepth:
        children = None
    else:
        children = [get_info(c, depth + 1) for c in node.get_children()]
    return {
        "id": get_cursor_id(node),
        "kind": node.kind,
        "usr": node.get_usr(),
        "spelling": node.spelling,
        "location": node.location,
        "extent.start": node.extent.start,
        "extent.end": node.extent.end,
        "is_definition": node.is_definition(),
        "definition id": get_cursor_id(node.get_definition()),
        "children": children,
    }


def main():
    from clang.cindex import Index
    from pprint import pprint

    from optparse import OptionParser, OptionGroup

    global opts

    parser = OptionParser("usage: %prog [options] {filename} [clang-args*]")
    parser.add_option(
        "",
        "--show-ids",
        dest="showIDs",
        help="Compute cursor IDs (very slow)",
        action="store_true",
        default=False,
    )
    parser.add_option(
        "",
        "--max-depth",
        dest="maxDepth",
        help="Limit cursor expansion to depth N",
        metavar="N",
        type=int,
        default=None,
    )
    parser.disable_interspersed_args()
    (opts, args) = parser.parse_args()

    if len(args) == 0:
        parser.error("invalid number arguments")

    index = Index.create()
    tu = index.parse(None, args)
    if not tu:
        parser.error("unable to load input")

    pprint(("diags", [get_diag_info(d) for d in tu.diagnostics]))
    pprint(("nodes", get_info(tu.cursor)))


if __name__ == "__main__":
    main()
