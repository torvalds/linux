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


DEBUG_HELP = """
Print debug information according with the level(s),
which is given by the following bitmask:

1  - enable debug parsing logic
2  - enable debug messages on file open
4  - enable debug for ABI parse data
"""
