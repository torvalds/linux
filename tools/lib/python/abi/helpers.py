#!/usr/bin/env python3
# Copyright(c) 2025: Mauro Carvalho Chehab <mchehab@kernel.org>.
# pylint: disable=R0903
# SPDX-License-Identifier: GPL-2.0

"""
Helper classes for ABI parser
"""

ABI_DIR = "Documentation/ABI/"


class AbiDebug:
    """Debug levels"""

    WHAT_PARSING = 1
    WHAT_OPEN = 2
    DUMP_ABI_STRUCTS = 4
    UNDEFINED = 8
    REGEX = 16
    SUBGROUP_MAP = 32
    SUBGROUP_DICT = 64
    SUBGROUP_SIZE = 128
    GRAPH = 256


DEBUG_HELP = """
1  - enable debug parsing logic
2  - enable debug messages on file open
4  - enable debug for ABI parse data
8  - enable extra debug information to identify troubles
     with ABI symbols found at the local machine that
     weren't found on ABI documentation (used only for
     undefined subcommand)
16 - enable debug for what to regex conversion
32 - enable debug for symbol regex subgroups
64 - enable debug for sysfs graph tree variable
"""
