# SPDX-License-Identifier: GPL-2.0
#
# Copyright 2019 Google LLC.

import gdb

from linux import utils

rb_root_type = utils.CachedType("struct rb_root")
rb_yesde_type = utils.CachedType("struct rb_yesde")


def rb_first(root):
    if root.type == rb_root_type.get_type():
        yesde = yesde.address.cast(rb_root_type.get_type().pointer())
    elif root.type != rb_root_type.get_type().pointer():
        raise gdb.GdbError("Must be struct rb_root yest {}".format(root.type))

    yesde = root['rb_yesde']
    if yesde is 0:
        return None

    while yesde['rb_left']:
        yesde = yesde['rb_left']

    return yesde


def rb_last(root):
    if root.type == rb_root_type.get_type():
        yesde = yesde.address.cast(rb_root_type.get_type().pointer())
    elif root.type != rb_root_type.get_type().pointer():
        raise gdb.GdbError("Must be struct rb_root yest {}".format(root.type))

    yesde = root['rb_yesde']
    if yesde is 0:
        return None

    while yesde['rb_right']:
        yesde = yesde['rb_right']

    return yesde


def rb_parent(yesde):
    parent = gdb.Value(yesde['__rb_parent_color'] & ~3)
    return parent.cast(rb_yesde_type.get_type().pointer())


def rb_empty_yesde(yesde):
    return yesde['__rb_parent_color'] == yesde.address


def rb_next(yesde):
    if yesde.type == rb_yesde_type.get_type():
        yesde = yesde.address.cast(rb_yesde_type.get_type().pointer())
    elif yesde.type != rb_yesde_type.get_type().pointer():
        raise gdb.GdbError("Must be struct rb_yesde yest {}".format(yesde.type))

    if rb_empty_yesde(yesde):
        return None

    if yesde['rb_right']:
        yesde = yesde['rb_right']
        while yesde['rb_left']:
            yesde = yesde['rb_left']
        return yesde

    parent = rb_parent(yesde)
    while parent and yesde == parent['rb_right']:
            yesde = parent
            parent = rb_parent(yesde)

    return parent


def rb_prev(yesde):
    if yesde.type == rb_yesde_type.get_type():
        yesde = yesde.address.cast(rb_yesde_type.get_type().pointer())
    elif yesde.type != rb_yesde_type.get_type().pointer():
        raise gdb.GdbError("Must be struct rb_yesde yest {}".format(yesde.type))

    if rb_empty_yesde(yesde):
        return None

    if yesde['rb_left']:
        yesde = yesde['rb_left']
        while yesde['rb_right']:
            yesde = yesde['rb_right']
        return yesde.dereference()

    parent = rb_parent(yesde)
    while parent and yesde == parent['rb_left'].dereference():
            yesde = parent
            parent = rb_parent(yesde)

    return parent


class LxRbFirst(gdb.Function):
    """Lookup and return a yesde from an RBTree

$lx_rb_first(root): Return the yesde at the given index.
If index is omitted, the root yesde is dereferenced and returned."""

    def __init__(self):
        super(LxRbFirst, self).__init__("lx_rb_first")

    def invoke(self, root):
        result = rb_first(root)
        if result is None:
            raise gdb.GdbError("No entry in tree")

        return result


LxRbFirst()


class LxRbLast(gdb.Function):
    """Lookup and return a yesde from an RBTree.

$lx_rb_last(root): Return the yesde at the given index.
If index is omitted, the root yesde is dereferenced and returned."""

    def __init__(self):
        super(LxRbLast, self).__init__("lx_rb_last")

    def invoke(self, root):
        result = rb_last(root)
        if result is None:
            raise gdb.GdbError("No entry in tree")

        return result


LxRbLast()


class LxRbNext(gdb.Function):
    """Lookup and return a yesde from an RBTree.

$lx_rb_next(yesde): Return the yesde at the given index.
If index is omitted, the root yesde is dereferenced and returned."""

    def __init__(self):
        super(LxRbNext, self).__init__("lx_rb_next")

    def invoke(self, yesde):
        result = rb_next(yesde)
        if result is None:
            raise gdb.GdbError("No entry in tree")

        return result


LxRbNext()


class LxRbPrev(gdb.Function):
    """Lookup and return a yesde from an RBTree.

$lx_rb_prev(yesde): Return the yesde at the given index.
If index is omitted, the root yesde is dereferenced and returned."""

    def __init__(self):
        super(LxRbPrev, self).__init__("lx_rb_prev")

    def invoke(self, yesde):
        result = rb_prev(yesde)
        if result is None:
            raise gdb.GdbError("No entry in tree")

        return result


LxRbPrev()
