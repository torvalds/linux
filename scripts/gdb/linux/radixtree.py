#
# gdb helper commands and functions for Linux kernel debugging
#
#  Radix Tree Parser
#
# Copyright (c) 2016 Linaro Ltd
#
# Authors:
#  Kieran Bingham <kieran.bingham@linaro.org>
#
# This work is licensed under the terms of the GNU GPL version 2.
#

import gdb

from linux import utils
from linux import constants

radix_tree_root_type = utils.CachedType("struct radix_tree_root")
radix_tree_node_type = utils.CachedType("struct radix_tree_node")


def is_indirect_ptr(node):
    long_type = utils.get_long_type()
    return (node.cast(long_type) & constants.LX_RADIX_TREE_INDIRECT_PTR)


def indirect_to_ptr(node):
    long_type = utils.get_long_type()
    node_type = node.type
    indirect_ptr = node.cast(long_type) & ~constants.LX_RADIX_TREE_INDIRECT_PTR
    return indirect_ptr.cast(node_type)


def maxindex(height):
    height = height & constants.LX_RADIX_TREE_HEIGHT_MASK
    return gdb.parse_and_eval("height_to_maxindex["+str(height)+"]")


def lookup(root, index):
    if root.type == radix_tree_root_type.get_type().pointer():
        root = root.dereference()
    elif root.type != radix_tree_root_type.get_type():
        raise gdb.GdbError("Must be struct radix_tree_root not {}"
                           .format(root.type))

    node = root['rnode']
    if node is 0:
        return None

    if not (is_indirect_ptr(node)):
        if (index > 0):
            return None
        return node

    node = indirect_to_ptr(node)

    height = node['path'] & constants.LX_RADIX_TREE_HEIGHT_MASK
    if (index > maxindex(height)):
        return None

    shift = (height-1) * constants.LX_RADIX_TREE_MAP_SHIFT

    while True:
        new_index = (index >> shift) & constants.LX_RADIX_TREE_MAP_MASK
        slot = node['slots'][new_index]

        node = slot.cast(node.type.pointer()).dereference()
        if node is 0:
            return None

        shift -= constants.LX_RADIX_TREE_MAP_SHIFT
        height -= 1

        if (height <= 0):
            break

    return node


class LxRadixTree(gdb.Function):
    """ Lookup and return a node from a RadixTree.

$lx_radix_tree_lookup(root_node [, index]): Return the node at the given index.
If index is omitted, the root node is dereferenced and returned."""

    def __init__(self):
        super(LxRadixTree, self).__init__("lx_radix_tree_lookup")

    def invoke(self, root, index=0):
        result = lookup(root, index)
        if result is None:
            raise gdb.GdbError("No entry in tree at index {}".format(index))

        return result

LxRadixTree()
