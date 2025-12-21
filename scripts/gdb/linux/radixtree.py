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

def resolve_root(root):
    if root.type == radix_tree_root_type.get_type():
        return root
    if root.type == radix_tree_root_type.get_type().pointer():
        return root.dereference()
    raise gdb.GdbError("must be {} not {}"
                       .format(radix_tree_root_type.get_type(), root.type))

def lookup(root, index):
    root = resolve_root(root)
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

def descend(parent, index):
    offset = (index >> int(parent["shift"])) & constants.LX_RADIX_TREE_MAP_MASK
    return offset, parent["slots"][offset]

def load_root(root):
    node = root["xa_head"]
    nodep = node

    if is_internal_node(node):
        node = entry_to_node(node)
        maxindex = node_maxindex(node)
        return int(node["shift"]) + constants.LX_RADIX_TREE_MAP_SHIFT, \
               nodep, maxindex

    return 0, nodep, 0

class RadixTreeIter:
    def __init__(self, start):
        self.index = 0
        self.next_index = start
        self.node = None

def xa_mk_internal(v):
    return (v << 2) | 2

LX_XA_RETRY_ENTRY = xa_mk_internal(256)
LX_RADIX_TREE_RETRY = LX_XA_RETRY_ENTRY

def next_chunk(root, iter):
    mask = (1 << (utils.get_ulong_type().sizeof * 8)) - 1

    index = iter.next_index
    if index == 0 and iter.index != 0:
        return None

    restart = True
    while restart:
        restart = False

        _, child, maxindex = load_root(root)
        if index > maxindex:
            return None
        if not child:
            return None

        if not is_internal_node(child):
            iter.index = index
            iter.next_index = (maxindex + 1) & mask
            iter.node = None
            return root["xa_head"].address

        while True:
            node = entry_to_node(child)
            offset, child = descend(node, index)

            if not child:
                while True:
                    offset += 1
                    if offset >= constants.LX_RADIX_TREE_MAP_SIZE:
                        break
                    slot = node["slots"][offset]
                    if slot:
                        break
                index &= ~node_maxindex(node)
                index = (index + (offset << int(node["shift"]))) & mask
                if index == 0:
                    return None
                if offset == constants.LX_RADIX_TREE_MAP_SIZE:
                    restart = True
                    break
                child = node["slots"][offset]

            if not child:
                restart = True
                break
            if child == LX_XA_RETRY_ENTRY:
                break
            if not node["shift"] or not is_internal_node(child):
                break

    iter.index = (index & ~node_maxindex(node)) | offset
    iter.next_index = ((index | node_maxindex(node)) + 1) & mask
    iter.node = node

    return node["slots"][offset].address

def next_slot(slot, iter):
    mask = (1 << (utils.get_ulong_type().sizeof * 8)) - 1
    for _ in range(iter.next_index - iter.index - 1):
        slot += 1
        iter.index = (iter.index + 1) & mask
        if slot.dereference():
            return slot
    return None

def for_each_slot(root, start=0):
    iter = RadixTreeIter(start)
    slot = None
    while True:
        if not slot:
            slot = next_chunk(root, iter)
            if not slot:
                break
        yield iter.index, slot
        slot = next_slot(slot, iter)

class LxRadixTreeLookup(gdb.Function):
    """ Lookup and return a node from a RadixTree.

$lx_radix_tree_lookup(root_node [, index]): Return the node at the given index.
If index is omitted, the root node is dereference and returned."""

    def __init__(self):
        super(LxRadixTreeLookup, self).__init__("lx_radix_tree_lookup")

    def invoke(self, root, index=0):
        result = lookup(root, index)
        if result is None:
            raise gdb.GdbError("No entry in tree at index {}".format(index))

        return result

class LxRadixTree(gdb.Command):
    """Show all values stored in a RadixTree."""

    def __init__(self):
        super(LxRadixTree, self).__init__("lx-radix-tree", gdb.COMMAND_DATA,
                                          gdb.COMPLETE_NONE)

    def invoke(self, argument, from_tty):
        args = gdb.string_to_argv(argument)
        if len(args) != 1:
            raise gdb.GdbError("Usage: lx-radix-tree ROOT")
        root = gdb.parse_and_eval(args[0])
        for index, slot in for_each_slot(root):
            gdb.write("[{}] = {}\n".format(index, slot.dereference()))

LxRadixTree()
LxRadixTreeLookup()
