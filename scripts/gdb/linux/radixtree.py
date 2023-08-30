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
radix_tree_node_type = utils.CachedType("struct xa_node")

def is_internal_node(node):
    long_type = utils.get_long_type()
    return ((node.cast(long_type) & constants.LX_RADIX_TREE_ENTRY_MASK) == constants.LX_RADIX_TREE_INTERNAL_NODE)

def entry_to_node(node):
    long_type = utils.get_long_type()
    node_type = node.type
    indirect_ptr = node.cast(long_type) & ~constants.LX_RADIX_TREE_INTERNAL_NODE
    return indirect_ptr.cast(radix_tree_node_type.get_type().pointer())

def node_maxindex(node):
    return (constants.LX_RADIX_TREE_MAP_SIZE << node['shift']) - 1

def lookup(root, index):
    if root.type == radix_tree_root_type.get_type().pointer():
        node = root.dereference()
    elif root.type != radix_tree_root_type.get_type():
        raise gdb.GdbError("must be {} not {}"
                           .format(radix_tree_root_type.get_type(), root.type))

    node = root['xa_head']
    if node == 0:
        return None

    if not (is_internal_node(node)):
        if (index > 0):
            return None
        return node

    node = entry_to_node(node)
    maxindex = node_maxindex(node)

    if (index > maxindex):
        return None

    shift = node['shift'] + constants.LX_RADIX_TREE_MAP_SHIFT

    while True:
        offset = (index >> node['shift']) & constants.LX_RADIX_TREE_MAP_MASK
        slot = node['slots'][offset]

        if slot == 0:
            return None

        node = slot.cast(node.type.pointer()).dereference()
        if node == 0:
            return None

        shift -= constants.LX_RADIX_TREE_MAP_SHIFT
        if (shift <= 0):
            break

    return node

class LxRadixTree(gdb.Function):
    """ Lookup and return a node from a RadixTree.

$lx_radix_tree_lookup(root_node [, index]): Return the node at the given index.
If index is omitted, the root node is dereference and returned."""

    def __init__(self):
        super(LxRadixTree, self).__init__("lx_radix_tree_lookup")

    def invoke(self, root, index=0):
        result = lookup(root, index)
        if result is None:
            raise gdb.GdbError("No entry in tree at index {}".format(index))

        return result

LxRadixTree()
