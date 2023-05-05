#
# gdb helper commands and functions for Linux kernel debugging
#
#  VFS tools
#
# Copyright (c) 2023 Glenn Washburn
# Copyright (c) 2016 Linaro Ltd
#
# Authors:
#  Glenn Washburn <development@efficientek.com>
#  Kieran Bingham <kieran.bingham@linaro.org>
#
# This work is licensed under the terms of the GNU GPL version 2.
#

import gdb
from linux import utils


def dentry_name(d):
    parent = d['d_parent']
    if parent == d or parent == 0:
        return ""
    p = dentry_name(d['d_parent']) + "/"
    return p + d['d_iname'].string()

class DentryName(gdb.Function):
    """Return string of the full path of a dentry.

$lx_dentry_name(PTR): Given PTR to a dentry struct, return a string
of the full path of the dentry."""

    def __init__(self):
        super(DentryName, self).__init__("lx_dentry_name")

    def invoke(self, dentry_ptr):
        return dentry_name(dentry_ptr)

DentryName()


dentry_type = utils.CachedType("struct dentry")

class InodeDentry(gdb.Function):
    """Return dentry pointer for inode.

$lx_i_dentry(PTR): Given PTR to an inode struct, return a pointer to
the associated dentry struct, if there is one."""

    def __init__(self):
        super(InodeDentry, self).__init__("lx_i_dentry")

    def invoke(self, inode_ptr):
        d_u = inode_ptr["i_dentry"]["first"]
        if d_u == 0:
            return ""
        return utils.container_of(d_u, dentry_type.get_type().pointer(), "d_u")

InodeDentry()
