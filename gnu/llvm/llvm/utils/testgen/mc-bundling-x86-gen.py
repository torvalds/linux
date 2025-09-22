#!/usr/bin/env python

# Auto-generates an exhaustive and repetitive test for correct bundle-locked
# alignment on x86.
# For every possible offset in an aligned bundle, a bundle-locked group of every
# size in the inclusive range [1, bundle_size] is inserted. An appropriate CHECK
# is added to verify that NOP padding occurred (or did not occur) as expected.
# Run with --align-to-end to generate a similar test with align_to_end for each
# .bundle_lock directive.

# This script runs with Python 2.7 and 3.2+

from __future__ import print_function
import argparse

BUNDLE_SIZE_POW2 = 4
BUNDLE_SIZE = 2**BUNDLE_SIZE_POW2

PREAMBLE = """
# RUN: llvm-mc -filetype=obj -triple i386-pc-linux-gnu %s -o - \\
# RUN:   | llvm-objdump -triple i386 -disassemble -no-show-raw-insn - | FileCheck %s

# !!! This test is auto-generated from utils/testgen/mc-bundling-x86-gen.py !!!
#     It tests that bundle-aligned grouping works correctly in MC. Read the
#     source of the script for more details.

  .text
  .bundle_align_mode {0}
""".format(
    BUNDLE_SIZE_POW2
).lstrip()

ALIGNTO = "  .align {0}, 0x90"
NOPFILL = "  .fill {0}, 1, 0x90"


def print_bundle_locked_sequence(len, align_to_end=False):
    print("  .bundle_lock{0}".format(" align_to_end" if align_to_end else ""))
    print("  .rept {0}".format(len))
    print("  inc %eax")
    print("  .endr")
    print("  .bundle_unlock")


def generate(align_to_end=False):
    print(PREAMBLE)

    ntest = 0
    for instlen in range(1, BUNDLE_SIZE + 1):
        for offset in range(0, BUNDLE_SIZE):
            # Spread out all the instructions to not worry about cross-bundle
            # interference.
            print(ALIGNTO.format(2 * BUNDLE_SIZE))
            print("INSTRLEN_{0}_OFFSET_{1}:".format(instlen, offset))
            if offset > 0:
                print(NOPFILL.format(offset))
            print_bundle_locked_sequence(instlen, align_to_end)

            # Now generate an appropriate CHECK line
            base_offset = ntest * 2 * BUNDLE_SIZE
            inst_orig_offset = base_offset + offset  # had it not been padded...

            def print_check(adjusted_offset=None, nop_split_offset=None):
                if adjusted_offset is not None:
                    print("# CHECK: {0:x}: nop".format(inst_orig_offset))
                    if nop_split_offset is not None:
                        print("# CHECK: {0:x}: nop".format(nop_split_offset))
                    print("# CHECK: {0:x}: incl".format(adjusted_offset))
                else:
                    print("# CHECK: {0:x}: incl".format(inst_orig_offset))

            if align_to_end:
                if offset + instlen == BUNDLE_SIZE:
                    # No padding needed
                    print_check()
                elif offset + instlen < BUNDLE_SIZE:
                    # Pad to end at nearest bundle boundary
                    offset_to_end = base_offset + (BUNDLE_SIZE - instlen)
                    print_check(offset_to_end)
                else:  # offset + instlen > BUNDLE_SIZE
                    # Pad to end at next bundle boundary, splitting the nop sequence
                    # at the nearest bundle boundary
                    offset_to_nearest_bundle = base_offset + BUNDLE_SIZE
                    offset_to_end = base_offset + (BUNDLE_SIZE * 2 - instlen)
                    if offset_to_nearest_bundle == offset_to_end:
                        offset_to_nearest_bundle = None
                    print_check(offset_to_end, offset_to_nearest_bundle)
            else:
                if offset + instlen > BUNDLE_SIZE:
                    # Padding needed
                    aligned_offset = (inst_orig_offset + instlen) & ~(BUNDLE_SIZE - 1)
                    print_check(aligned_offset)
                else:
                    # No padding needed
                    print_check()

            print()
            ntest += 1


if __name__ == "__main__":
    argparser = argparse.ArgumentParser()
    argparser.add_argument(
        "--align-to-end",
        action="store_true",
        help="generate .bundle_lock with align_to_end option",
    )
    args = argparser.parse_args()
    generate(align_to_end=args.align_to_end)
