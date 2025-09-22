#!/usr/bin/env python3
# ===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===##
"""
Script to generate MSP430 definitions from TI's devices.csv

Download the devices.csv from [1] using the link "Header and Support Files".

[1]: https://www.ti.com/tool/MSP430-GCC-OPENSOURCE#downloads
"""
import csv
import sys

DEVICE_COLUMN = 0
MULTIPLIER_COLUMN = 3

MULTIPLIER_SW = "0"
MULTIPLIER_HW_16 = ("1", "2")
MULTIPLIER_HW_32 = ("4", "8")

PREFIX = """//===--- MSP430Target.def - MSP430 Feature/Processor Database----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the MSP430 devices and their features.
//
// Generated from TI's devices.csv in version {} using the script in
// Target/MSP430/gen-msp430-def.py - use this tool rather than adding
// new MCUs by hand.
//
//===----------------------------------------------------------------------===//

#ifndef MSP430_MCU_FEAT
#define MSP430_MCU_FEAT(NAME, HWMULT) MSP430_MCU(NAME)
#endif

#ifndef MSP430_MCU
#define MSP430_MCU(NAME)
#endif

"""

SUFFIX = """
// Generic MCUs
MSP430_MCU("msp430i2xxgeneric")

#undef MSP430_MCU
#undef MSP430_MCU_FEAT
"""


def csv2def(csv_path, def_path):
    """
    Parse the devices.csv file at the given path, generate the definitions and
    write them to the given path.

    :param csv_path: Path to the devices.csv to parse
    :type csv_path: str
    :param def_path: Path to the output file to write the definitions to
    "type def_path: str
    """

    mcus_multiplier_sw = []
    mcus_multiplier_hw_16 = []
    mcus_multiplier_hw_32 = []
    version = "unknown"

    with open(csv_path) as csv_file:
        csv_reader = csv.reader(csv_file)
        while True:
            row = next(csv_reader)
            if len(row) < MULTIPLIER_COLUMN:
                continue

            if row[DEVICE_COLUMN] == "# Device Name":
                assert row[MULTIPLIER_COLUMN] == "MPY_TYPE", "File format changed"
                break

            if row[0] == "Version:":
                version = row[1]

        for row in csv_reader:
            if row[DEVICE_COLUMN].endswith("generic"):
                continue
            if row[MULTIPLIER_COLUMN] == MULTIPLIER_SW:
                mcus_multiplier_sw.append(row[DEVICE_COLUMN])
            elif row[MULTIPLIER_COLUMN] in MULTIPLIER_HW_16:
                mcus_multiplier_hw_16.append(row[DEVICE_COLUMN])
            elif row[MULTIPLIER_COLUMN] in MULTIPLIER_HW_32:
                mcus_multiplier_hw_32.append(row[DEVICE_COLUMN])
            else:
                assert 0, "Unknown multiplier type"

    with open(def_path, "w") as def_file:
        def_file.write(PREFIX.format(version))

        for mcu in mcus_multiplier_sw:
            def_file.write(f'MSP430_MCU("{mcu}")\n')

        def_file.write("\n// With 16-bit hardware multiplier\n")

        for mcu in mcus_multiplier_hw_16:
            def_file.write(f'MSP430_MCU_FEAT("{mcu}", "16bit")\n')

        def_file.write("\n// With 32-bit hardware multiplier\n")

        for mcu in mcus_multiplier_hw_32:
            def_file.write(f'MSP430_MCU_FEAT("{mcu}", "32bit")\n')

        def_file.write(SUFFIX)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(f"Usage: {sys.argv[0]} <CSV_FILE> <DEF_FILE>")

    csv2def(sys.argv[1], sys.argv[2])
