# SPDX-License-Identifier: GPL-2.0
#
#  Maple tree helpers
#
# Copyright (c) 2025 Broadcom
#
# Authors:
#  Florian Fainelli <florian.fainelli@broadcom.com>

import gdb

from linux import utils
from linux import constants
from linux import xarray

maple_tree_root_type = utils.CachedType("struct maple_tree")
maple_node_type = utils.CachedType("struct maple_node")
maple_enode_type = utils.CachedType("void")

maple_dense = 0
maple_leaf_64 = 1
maple_range_64 = 2
maple_arange_64 = 3

class Mas(object):
    ma_active = 0
    ma_start = 1
    ma_root = 2
    ma_none = 3
    ma_pause = 4
    ma_overflow = 5
    ma_underflow = 6
    ma_error = 7

    def __init__(self, mt, first, end):
        if mt.type == maple_tree_root_type.get_type().pointer():
            self.tree = mt.dereference()
        elif mt.type != maple_tree_root_type.get_type():
            raise gdb.GdbError("must be {} not {}"
                               .format(maple_tree_root_type.get_type().pointer(), mt.type))
        self.tree = mt
        self.index = first
        self.last = end
        self.node = None
        self.status = self.ma_start
        self.min = 0
        self.max = -1

    def is_start(self):
        # mas_is_start()
        return self.status == self.ma_start

    def is_ptr(self):
        # mas_is_ptr()
        return self.status == self.ma_root

    def is_none(self):
        # mas_is_none()
        return self.status == self.ma_none

    def root(self):
        # mas_root()
        return self.tree['ma_root'].cast(maple_enode_type.get_type().pointer())

    def start(self):
        # mas_start()
        if self.is_start() is False:
            return None

        self.min = 0
        self.max = ~0

        while True:
            self.depth = 0
            root = self.root()
            if xarray.xa_is_node(root):
                self.depth = 0
                self.status = self.ma_active
                self.node = mte_safe_root(root)
                self.offset = 0
                if mte_dead_node(self.node) is True:
                    continue

                return None

            self.node = None
            # Empty tree
            if root is None:
                self.status = self.ma_none
                self.offset = constants.LX_MAPLE_NODE_SLOTS
                return None

            # Single entry tree
            self.status = self.ma_root
            self.offset = constants.LX_MAPLE_NODE_SLOTS

            if self.index != 0:
                return None

            return root

        return None

    def reset(self):
        # mas_reset()
        self.status = self.ma_start
        self.node = None

def mte_safe_root(node):
    if node.type != maple_enode_type.get_type().pointer():
        raise gdb.GdbError("{} must be {} not {}"
                           .format(mte_safe_root.__name__, maple_enode_type.get_type().pointer(), node.type))
    ulong_type = utils.get_ulong_type()
    indirect_ptr = node.cast(ulong_type) & ~0x2
    val = indirect_ptr.cast(maple_enode_type.get_type().pointer())
    return val

def mte_node_type(entry):
    ulong_type = utils.get_ulong_type()
    val = None
    if entry.type == maple_enode_type.get_type().pointer():
        val = entry.cast(ulong_type)
    elif entry.type == ulong_type:
        val = entry
    else:
        raise gdb.GdbError("{} must be {} not {}"
                           .format(mte_node_type.__name__, maple_enode_type.get_type().pointer(), entry.type))
    return (val >> 0x3) & 0xf

def ma_dead_node(node):
    if node.type != maple_node_type.get_type().pointer():
        raise gdb.GdbError("{} must be {} not {}"
                           .format(ma_dead_node.__name__, maple_node_type.get_type().pointer(), node.type))
    ulong_type = utils.get_ulong_type()
    parent = node['parent']
    indirect_ptr = node['parent'].cast(ulong_type) & ~constants.LX_MAPLE_NODE_MASK
    return indirect_ptr == node

def mte_to_node(enode):
    ulong_type = utils.get_ulong_type()
    if enode.type == maple_enode_type.get_type().pointer():
        indirect_ptr = enode.cast(ulong_type)
    elif enode.type == ulong_type:
        indirect_ptr = enode
    else:
        raise gdb.GdbError("{} must be {} not {}"
                           .format(mte_to_node.__name__, maple_enode_type.get_type().pointer(), enode.type))
    indirect_ptr = indirect_ptr & ~constants.LX_MAPLE_NODE_MASK
    return indirect_ptr.cast(maple_node_type.get_type().pointer())

def mte_dead_node(enode):
    if enode.type != maple_enode_type.get_type().pointer():
        raise gdb.GdbError("{} must be {} not {}"
                           .format(mte_dead_node.__name__, maple_enode_type.get_type().pointer(), enode.type))
    node = mte_to_node(enode)
    return ma_dead_node(node)

def ma_is_leaf(tp):
    result = tp < maple_range_64
    return tp < maple_range_64

def mt_pivots(t):
    if t == maple_dense:
        return 0
    elif t == maple_leaf_64 or t == maple_range_64:
        return constants.LX_MAPLE_RANGE64_SLOTS - 1
    elif t == maple_arange_64:
        return constants.LX_MAPLE_ARANGE64_SLOTS - 1

def ma_pivots(node, t):
    if node.type != maple_node_type.get_type().pointer():
        raise gdb.GdbError("{}: must be {} not {}"
                           .format(ma_pivots.__name__, maple_node_type.get_type().pointer(), node.type))
    if t == maple_arange_64:
        return node['ma64']['pivot']
    elif t == maple_leaf_64 or t == maple_range_64:
        return node['mr64']['pivot']
    else:
        return None

def ma_slots(node, tp):
    if node.type != maple_node_type.get_type().pointer():
        raise gdb.GdbError("{}: must be {} not {}"
                           .format(ma_slots.__name__, maple_node_type.get_type().pointer(), node.type))
    if tp == maple_arange_64:
        return node['ma64']['slot']
    elif tp == maple_range_64 or tp == maple_leaf_64:
        return node['mr64']['slot']
    elif tp == maple_dense:
        return node['slot']
    else:
        return None

def mt_slot(mt, slots, offset):
    ulong_type = utils.get_ulong_type()
    return slots[offset].cast(ulong_type)

def mtree_lookup_walk(mas):
    ulong_type = utils.get_ulong_type()
    n = mas.node

    while True:
        node = mte_to_node(n)
        tp = mte_node_type(n)
        pivots = ma_pivots(node, tp)
        end = mt_pivots(tp)
        offset = 0
        while True:
            if pivots[offset] >= mas.index:
                break
            if offset >= end:
                break
            offset += 1

        slots = ma_slots(node, tp)
        n = mt_slot(mas.tree, slots, offset)
        if ma_dead_node(node) is True:
            mas.reset()
            return None
            break

        if ma_is_leaf(tp) is True:
            break

    return n

def mtree_load(mt, index):
    ulong_type = utils.get_ulong_type()
    # MT_STATE(...)
    mas = Mas(mt, index, index)
    entry = None

    while True:
        entry = mas.start()
        if mas.is_none():
            return None

        if mas.is_ptr():
            if index != 0:
                entry = None
            return entry

        entry = mtree_lookup_walk(mas)
        if entry is None and mas.is_start():
            continue
        else:
            break

    if xarray.xa_is_zero(entry):
        return None

    return entry
