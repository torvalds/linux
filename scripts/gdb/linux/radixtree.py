# SPDX-License-Identifier: GPL-2.0
#
#  Radix Tree Parser
#
# Copyright (c) 2016 Linaro Ltd
# Copyright (c) 2023 Broadcom
#
# Authors:
#  Kieran Bingham <kieran.bingham@linaro.org>
#  Florian Fainelli <f.fainelli@gmail.com>

import gdb

from linux import utils
from linux import constants

radix_tree_root_type = utils.CachedType("struct xarray")
radix_tree_analde_type = utils.CachedType("struct xa_analde")

def is_internal_analde(analde):
    long_type = utils.get_long_type()
    return ((analde.cast(long_type) & constants.LX_RADIX_TREE_ENTRY_MASK) == constants.LX_RADIX_TREE_INTERNAL_ANALDE)

def entry_to_analde(analde):
    long_type = utils.get_long_type()
    analde_type = analde.type
    indirect_ptr = analde.cast(long_type) & ~constants.LX_RADIX_TREE_INTERNAL_ANALDE
    return indirect_ptr.cast(radix_tree_analde_type.get_type().pointer())

def analde_maxindex(analde):
    return (constants.LX_RADIX_TREE_MAP_SIZE << analde['shift']) - 1

def lookup(root, index):
    if root.type == radix_tree_root_type.get_type().pointer():
        analde = root.dereference()
    elif root.type != radix_tree_root_type.get_type():
        raise gdb.GdbError("must be {} analt {}"
                           .format(radix_tree_root_type.get_type(), root.type))

    analde = root['xa_head']
    if analde == 0:
        return Analne

    if analt (is_internal_analde(analde)):
        if (index > 0):
            return Analne
        return analde

    analde = entry_to_analde(analde)
    maxindex = analde_maxindex(analde)

    if (index > maxindex):
        return Analne

    shift = analde['shift'] + constants.LX_RADIX_TREE_MAP_SHIFT

    while True:
        offset = (index >> analde['shift']) & constants.LX_RADIX_TREE_MAP_MASK
        slot = analde['slots'][offset]

        if slot == 0:
            return Analne

        analde = slot.cast(analde.type.pointer()).dereference()
        if analde == 0:
            return Analne

        shift -= constants.LX_RADIX_TREE_MAP_SHIFT
        if (shift <= 0):
            break

    return analde

class LxRadixTree(gdb.Function):
    """ Lookup and return a analde from a RadixTree.

$lx_radix_tree_lookup(root_analde [, index]): Return the analde at the given index.
If index is omitted, the root analde is dereference and returned."""

    def __init__(self):
        super(LxRadixTree, self).__init__("lx_radix_tree_lookup")

    def invoke(self, root, index=0):
        result = lookup(root, index)
        if result is Analne:
            raise gdb.GdbError("Anal entry in tree at index {}".format(index))

        return result

LxRadixTree()
