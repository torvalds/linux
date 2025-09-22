#!/usr/bin/env python

import argparse
import libcxx.header_information
import os
import pathlib
import re
import sys
import typing

def IWYU_mapping(header: str) -> typing.Optional[typing.List[str]]:
    ignore = [
        "__debug_utils/.+",
        "__fwd/get[.]h",
        "__pstl/.+",
        "__support/.+",
        "__utility/private_constructor_tag.h",
    ]
    if any(re.match(pattern, header) for pattern in ignore):
        return None
    elif header == "__bits":
        return ["bits"]
    elif header in ("__bit_reference", "__fwd/bit_reference.h"):
        return ["bitset", "vector"]
    elif re.match("__configuration/.+", header) or header == "__config":
        return ["version"]
    elif header == "__hash_table":
        return ["unordered_map", "unordered_set"]
    elif header == "__locale":
        return ["locale"]
    elif re.match("__locale_dir/.+", header):
        return ["locale"]
    elif re.match("__math/.+", header):
        return ["cmath"]
    elif header == "__node_handle":
        return ["map", "set", "unordered_map", "unordered_set"]
    elif header == "__split_buffer":
        return ["deque", "vector"]
    elif re.match("(__thread/support[.]h)|(__thread/support/.+)", header):
        return ["atomic", "mutex", "semaphore", "thread"]
    elif header == "__tree":
        return ["map", "set"]
    elif header == "__fwd/pair.h":
        return ["utility"]
    elif header == "__fwd/subrange.h":
        return ["ranges"]
    elif re.match("__fwd/(fstream|ios|istream|ostream|sstream|streambuf)[.]h", header):
        return ["iosfwd"]
    # Handle remaining forward declaration headers
    elif re.match("__fwd/(.+)[.]h", header):
        return [re.match("__fwd/(.+)[.]h", header).group(1)]
    # Handle detail headers for things like <__algorithm/foo.h>
    elif re.match("__(.+?)/.+", header):
        return [re.match("__(.+?)/.+", header).group(1)]
    else:
        return None


def main(argv: typing.List[str]):
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "-o",
        help="File to output the IWYU mappings into",
        type=argparse.FileType("w"),
        required=True,
        dest="output",
    )
    args = parser.parse_args(argv)

    mappings = []  # Pairs of (header, public_header)
    for header in libcxx.header_information.all_headers:
        public_headers = IWYU_mapping(header)
        if public_headers is not None:
            mappings.extend((header, public) for public in public_headers)

    # Validate that we only have valid public header names -- otherwise the mapping above
    # needs to be updated.
    for header, public in mappings:
        if public not in libcxx.header_information.public_headers:
            raise RuntimeError(f"{header}: Header {public} is not a valid header")

    args.output.write("[\n")
    for header, public in sorted(mappings):
        args.output.write(
            f'  {{ include: [ "<{header}>", "private", "<{public}>", "public" ] }},\n'
        )
    args.output.write("]\n")

if __name__ == "__main__":
    main(sys.argv[1:])
