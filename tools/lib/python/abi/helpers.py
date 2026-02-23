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

    WHAT_PARSING = 1        #: Enable debug parsing logic.
    WHAT_OPEN = 2           #: Enable debug messages on file open.
    DUMP_ABI_STRUCTS = 4    #: Enable debug for ABI parse data.
    UNDEFINED = 8           #: Enable extra undefined symbol data.
    REGEX = 16              #: Enable debug for what to regex conversion.
    SUBGROUP_MAP = 32       #: Enable debug for symbol regex subgroups
    SUBGROUP_DICT = 64      #: Enable debug for sysfs graph tree variable.
    SUBGROUP_SIZE = 128     #: Enable debug of search groups.
    GRAPH = 256             #: Display ref tree graph for undefined symbols.

#: Helper messages for each debug variable
DEBUG_HELP = """
1   - enable debug parsing logic
2   - enable debug messages on file open
4   - enable debug for ABI parse data
8   - enable extra debug information to identify troubles
      with ABI symbols found at the local machine that
      weren't found on ABI documentation (used only for
      undefined subcommand)
16  - enable debug for what to regex conversion
32  - enable debug for symbol regex subgroups
64  - enable debug for sysfs graph tree variable
128 - enable debug of search groups
256 - enable displaying refrence tree graphs for undefined symbols.
"""
