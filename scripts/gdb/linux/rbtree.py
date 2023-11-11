# SPDX-License-Identifier: GPL-2.0
#
# Copyright 2019 Google LLC.

import gdb

from linux import utils

rb_root_type = utils.CachedType("struct rb_root")
rb_node_type = utils.CachedType("struct rb_node")


def rb_first(root):
    if root.type == rb_root_type.get_type():
        node = root.address.cast(rb_root_type.get_type().pointer())
    elif root.type != rb_root_type.get_type().pointer():
        raise gdb.GdbError("Must be struct rb_root not {}".format(root.type))

    node = root['rb_node']
    if node == 0:
        return None

    while node['rb_left']:
        node = node['rb_left']

    return node


def rb_last(root):
    if root.type == rb_root_type.get_type():
        node = root.address.cast(rb_root_type.get_type().pointer())
    elif root.type != rb_root_type.get_type().pointer():
        raise gdb.GdbError("Must be struct rb_root not {}".format(root.type))

    node = root['rb_node']
    if node == 0:
        return None

    while node['rb_right']:
        node = node['rb_right']

    return node


def rb_parent(node):
    parent = gdb.Value(node['__rb_parent_color'] & ~3)
    return parent.cast(rb_node_type.get_type().pointer())


def rb_empty_node(node):
    return node['__rb_parent_color'] == node.address


def rb_next(node):
    if node.type == rb_node_type.get_type():
        node = node.address.cast(rb_node_type.get_type().pointer())
    elif node.type != rb_node_type.get_type().pointer():
        raise gdb.GdbError("Must be struct rb_node not {}".format(node.type))

    if rb_empty_node(node):
        return None

    if node['rb_right']:
        node = node['rb_right']
        while node['rb_left']:
            node = node['rb_left']
        return node

    parent = rb_parent(node)
    while parent and node == parent['rb_right']:
            node = parent
            parent = rb_parent(node)

    return parent


def rb_prev(node):
    if node.type == rb_node_type.get_type():
        node = node.address.cast(rb_node_type.get_type().pointer())
    elif node.type != rb_node_type.get_type().pointer():
        raise gdb.GdbError("Must be struct rb_node not {}".format(node.type))

    if rb_empty_node(node):
        return None

    if node['rb_left']:
        node = node['rb_left']
        while node['rb_right']:
            node = node['rb_right']
        return node.dereference()

    parent = rb_parent(node)
    while parent and node == parent['rb_left'].dereference():
            node = parent
            parent = rb_parent(node)

    return parent


class LxRbFirst(gdb.Function):
    """Lookup and return a node from an RBTree

$lx_rb_first(root): Return the node at the given index.
If index is omitted, the root node is dereferenced and returned."""

    def __init__(self):
        super(LxRbFirst, self).__init__("lx_rb_first")

    def invoke(self, root):
        result = rb_first(root)
        if result is None:
            raise gdb.GdbError("No entry in tree")

        return result


LxRbFirst()


class LxRbLast(gdb.Function):
    """Lookup and return a node from an RBTree.

$lx_rb_last(root): Return the node at the given index.
If index is omitted, the root node is dereferenced and returned."""

    def __init__(self):
        super(LxRbLast, self).__init__("lx_rb_last")

    def invoke(self, root):
        result = rb_last(root)
        if result is None:
            raise gdb.GdbError("No entry in tree")

        return result


LxRbLast()


class LxRbNext(gdb.Function):
    """Lookup and return a node from an RBTree.

$lx_rb_next(node): Return the node at the given index.
If index is omitted, the root node is dereferenced and returned."""

    def __init__(self):
        super(LxRbNext, self).__init__("lx_rb_next")

    def invoke(self, node):
        result = rb_next(node)
        if result is None:
            raise gdb.GdbError("No entry in tree")

        return result


LxRbNext()


class LxRbPrev(gdb.Function):
    """Lookup and return a node from an RBTree.

$lx_rb_prev(node): Return the node at the given index.
If index is omitted, the root node is dereferenced and returned."""

    def __init__(self):
        super(LxRbPrev, self).__init__("lx_rb_prev")

    def invoke(self, node):
        result = rb_prev(node)
        if result is None:
            raise gdb.GdbError("No entry in tree")

        return result


LxRbPrev()
