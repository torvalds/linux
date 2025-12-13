#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2016, 2025 by Mauro Carvalho Chehab <mchehab@kernel.org>.
# pylint: disable=C0103

"""
Convert a C header or source file ``FILE_IN``, into a ReStructured Text
included via ..parsed-literal block with cross-references for the
documentation files that describe the API. It accepts an optional
``FILE_RULES`` file to describes what elements will be either ignored or
be pointed to a non-default reference type/name.

The output is written at ``FILE_OUT``.

It is capable of identifying defines, functions, structs, typedefs,
enums and enum symbols and create cross-references for all of them.
It is also capable of distinguish #define used for specifying a Linux
ioctl.

The optional ``FILE_RULES`` contains a set of rules like:

    ignore ioctl VIDIOC_ENUM_FMT
    replace ioctl VIDIOC_DQBUF vidioc_qbuf
    replace define V4L2_EVENT_MD_FL_HAVE_FRAME_SEQ :c:type:`v4l2_event_motion_det`
"""

import argparse

from lib.parse_data_structs import ParseDataStructs
from lib.enrich_formatter import EnrichFormatter

def main():
    """Main function"""
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=EnrichFormatter)

    parser.add_argument("-d", "--debug", action="count", default=0,
                        help="Increase debug level. Can be used multiple times")
    parser.add_argument("-t", "--toc", action="store_true",
                        help="instead of a literal block, outputs a TOC table at the RST file")

    parser.add_argument("file_in", help="Input C file")
    parser.add_argument("file_out", help="Output RST file")
    parser.add_argument("file_rules", nargs="?",
                        help="Exceptions file (optional)")

    args = parser.parse_args()

    parser = ParseDataStructs(debug=args.debug)
    parser.parse_file(args.file_in)

    if args.file_rules:
        parser.process_exceptions(args.file_rules)

    parser.debug_print()
    parser.write_output(args.file_in, args.file_out, args.toc)


if __name__ == "__main__":
    main()
