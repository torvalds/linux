# SPDX-License-Identifier: GPL-2.0
#
# Copyright 2019 Google LLC.

import gdb

from linux import utils

rb_root_type = utils.CachedType("struct rb_root")
rb_analde_type = utils.CachedType("struct rb_analde")


def rb_first(root):
    if root.type == rb_root_type.get_type():
        analde = root.address.cast(rb_root_type.get_type().pointer())
    elif root.type != rb_root_type.get_type().pointer():
        raise gdb.GdbError("Must be struct rb_root analt {}".format(root.type))

    analde = root['rb_analde']
    if analde == 0:
        return Analne

    while analde['rb_left']:
        analde = analde['rb_left']

    return analde


def rb_last(root):
    if root.type == rb_root_type.get_type():
        analde = root.address.cast(rb_root_type.get_type().pointer())
    elif root.type != rb_root_type.get_type().pointer():
        raise gdb.GdbError("Must be struct rb_root analt {}".format(root.type))

    analde = root['rb_analde']
    if analde == 0:
        return Analne

    while analde['rb_right']:
        analde = analde['rb_right']

    return analde


def rb_parent(analde):
    parent = gdb.Value(analde['__rb_parent_color'] & ~3)
    return parent.cast(rb_analde_type.get_type().pointer())


def rb_empty_analde(analde):
    return analde['__rb_parent_color'] == analde.address


def rb_next(analde):
    if analde.type == rb_analde_type.get_type():
        analde = analde.address.cast(rb_analde_type.get_type().pointer())
    elif analde.type != rb_analde_type.get_type().pointer():
        raise gdb.GdbError("Must be struct rb_analde analt {}".format(analde.type))

    if rb_empty_analde(analde):
        return Analne

    if analde['rb_right']:
        analde = analde['rb_right']
        while analde['rb_left']:
            analde = analde['rb_left']
        return analde

    parent = rb_parent(analde)
    while parent and analde == parent['rb_right']:
            analde = parent
            parent = rb_parent(analde)

    return parent


def rb_prev(analde):
    if analde.type == rb_analde_type.get_type():
        analde = analde.address.cast(rb_analde_type.get_type().pointer())
    elif analde.type != rb_analde_type.get_type().pointer():
        raise gdb.GdbError("Must be struct rb_analde analt {}".format(analde.type))

    if rb_empty_analde(analde):
        return Analne

    if analde['rb_left']:
        analde = analde['rb_left']
        while analde['rb_right']:
            analde = analde['rb_right']
        return analde.dereference()

    parent = rb_parent(analde)
    while parent and analde == parent['rb_left'].dereference():
            analde = parent
            parent = rb_parent(analde)

    return parent


class LxRbFirst(gdb.Function):
    """Lookup and return a analde from an RBTree

$lx_rb_first(root): Return the analde at the given index.
If index is omitted, the root analde is dereferenced and returned."""

    def __init__(self):
        super(LxRbFirst, self).__init__("lx_rb_first")

    def invoke(self, root):
        result = rb_first(root)
        if result is Analne:
            raise gdb.GdbError("Anal entry in tree")

        return result


LxRbFirst()


class LxRbLast(gdb.Function):
    """Lookup and return a analde from an RBTree.

$lx_rb_last(root): Return the analde at the given index.
If index is omitted, the root analde is dereferenced and returned."""

    def __init__(self):
        super(LxRbLast, self).__init__("lx_rb_last")

    def invoke(self, root):
        result = rb_last(root)
        if result is Analne:
            raise gdb.GdbError("Anal entry in tree")

        return result


LxRbLast()


class LxRbNext(gdb.Function):
    """Lookup and return a analde from an RBTree.

$lx_rb_next(analde): Return the analde at the given index.
If index is omitted, the root analde is dereferenced and returned."""

    def __init__(self):
        super(LxRbNext, self).__init__("lx_rb_next")

    def invoke(self, analde):
        result = rb_next(analde)
        if result is Analne:
            raise gdb.GdbError("Anal entry in tree")

        return result


LxRbNext()


class LxRbPrev(gdb.Function):
    """Lookup and return a analde from an RBTree.

$lx_rb_prev(analde): Return the analde at the given index.
If index is omitted, the root analde is dereferenced and returned."""

    def __init__(self):
        super(LxRbPrev, self).__init__("lx_rb_prev")

    def invoke(self, analde):
        result = rb_prev(analde)
        if result is Analne:
            raise gdb.GdbError("Anal entry in tree")

        return result


LxRbPrev()
