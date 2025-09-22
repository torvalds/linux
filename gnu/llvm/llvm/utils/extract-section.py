#!/usr/bin/env python
from __future__ import print_function

"""
Helper script to print out the raw content of an ELF section.
Example usages:
```
# print out as bits by default
extract-section.py .text --input-file=foo.o
```
```
# read from stdin and print out in hex
cat foo.o | extract-section.py -h .text
```
This is merely a wrapper around `llvm-readobj` that focuses on the binary
content as well as providing more formatting options.
"""

# Unfortunately reading binary from stdin is not so trivial in Python...
def read_raw_stdin():
    import sys

    if sys.version_info >= (3, 0):
        reading_source = sys.stdin.buffer
    else:
        # Windows will always read as string so we need some
        # special handling
        if sys.platform == "win32":
            import os, msvcrt

            msvcrt.setformat(sys.stdin.fileno(), os.O_BINARY)
        reading_source = sys.stdin
    return reading_source.read()


def get_raw_section_dump(readobj_path, section_name, input_file):
    import subprocess

    cmd = [
        readobj_path,
        "--elf-output-style=GNU",
        "--hex-dump={}".format(section_name),
        input_file,
    ]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE)

    if input_file == "-":
        # From stdin
        out, _ = proc.communicate(input=read_raw_stdin())
    else:
        out, _ = proc.communicate()

    return out.decode("utf-8") if type(out) is not str else out


if __name__ == "__main__":
    import argparse

    # The default '-h' (--help) will conflict with our '-h' (hex) format
    arg_parser = argparse.ArgumentParser(add_help=False)
    arg_parser.add_argument(
        "--readobj-path",
        metavar="<executable path>",
        type=str,
        help="Path to llvm-readobj",
    )
    arg_parser.add_argument(
        "--input-file",
        metavar="<file>",
        type=str,
        help="Input object file, or '-' to read from stdin",
    )
    arg_parser.add_argument(
        "section", metavar="<name>", type=str, help="Name of the section to extract"
    )
    # Output format
    format_group = arg_parser.add_mutually_exclusive_group()
    format_group.add_argument(
        "-b",
        dest="format",
        action="store_const",
        const="bits",
        help="Print out in bits",
    )
    arg_parser.add_argument(
        "--byte-indicator",
        action="store_true",
        help="Whether to print a '.' every 8 bits in bits printing mode",
    )
    arg_parser.add_argument(
        "--bits-endian",
        metavar="<little/big>",
        type=str,
        choices=["little", "big"],
        help="Print out bits in specified endianness (little or big); defaults to big",
    )
    format_group.add_argument(
        "-h",
        dest="format",
        action="store_const",
        const="hex",
        help="Print out in hexadecimal",
    )
    arg_parser.add_argument(
        "--hex-width",
        metavar="<# of bytes>",
        type=int,
        help="The width (in byte) of every element in hex printing mode",
    )

    arg_parser.add_argument("--help", action="help")
    arg_parser.set_defaults(
        format="bits",
        tool_path="llvm-readobj",
        input_file="-",
        byte_indicator=False,
        hex_width=4,
        bits_endian="big",
    )
    args = arg_parser.parse_args()

    raw_section = get_raw_section_dump(args.tool_path, args.section, args.input_file)

    results = []
    for line in raw_section.splitlines(False):
        if line.startswith("Hex dump"):
            continue
        parts = line.strip().split(" ")[1:]
        for part in parts[:4]:
            # exclude any non-hex dump string
            try:
                val = int(part, 16)
                if args.format == "bits":
                    # divided into bytes first
                    offsets = (24, 16, 8, 0)
                    if args.bits_endian == "little":
                        offsets = (0, 8, 16, 24)
                    for byte in [(val >> off) & 0xFF for off in offsets]:
                        for bit in [(byte >> off) & 1 for off in range(7, -1, -1)]:
                            results.append(str(bit))
                        if args.byte_indicator:
                            results.append(".")
                elif args.format == "hex":
                    assert args.hex_width <= 4 and args.hex_width > 0
                    width_bits = args.hex_width * 8
                    offsets = [off for off in range(32 - width_bits, -1, -width_bits)]
                    mask = (1 << width_bits) - 1
                    format_str = "{:0" + str(args.hex_width * 2) + "x}"
                    for word in [(val >> i) & mask for i in offsets]:
                        results.append(format_str.format(word))
            except:
                break
    print(" ".join(results), end="")
