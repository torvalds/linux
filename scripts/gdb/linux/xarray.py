# SPDX-License-Identifier: GPL-2.0
#
#  Xarray helpers
#
# Copyright (c) 2025 Broadcom
#
# Authors:
#  Florian Fainelli <florian.fainelli@broadcom.com>

import gdb

from linux import utils
from linux import constants

def xa_is_internal(entry):
    ulong_type = utils.get_ulong_type()
    return ((entry.cast(ulong_type) & 3) == 2)

def xa_mk_internal(v):
    return ((v << 2) | 2)

def xa_is_zero(entry):
    ulong_type = utils.get_ulong_type()
    return entry.cast(ulong_type) == xa_mk_internal(257)

def xa_is_node(entry):
    ulong_type = utils.get_ulong_type()
    return xa_is_internal(entry) and (entry.cast(ulong_type) > 4096)
