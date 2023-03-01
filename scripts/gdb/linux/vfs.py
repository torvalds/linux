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


def dentry_name(d):
    parent = d['d_parent']
    if parent == d or parent == 0:
        return ""
    p = dentry_name(d['d_parent']) + "/"
    return p + d['d_iname'].string()
