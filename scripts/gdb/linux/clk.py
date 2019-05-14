# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) NXP 2019

import gdb
import sys

from linux import utils, lists

clk_core_type = utils.CachedType("struct clk_core")


def clk_core_for_each_child(hlist_head):
    return lists.hlist_for_each_entry(hlist_head,
            clk_core_type.get_type().pointer(), "child_node")


class LxClkSummary(gdb.Command):
    """Print Linux kernel log buffer."""

    def __init__(self):
        super(LxClkSummary, self).__init__("lx-clk-summary", gdb.COMMAND_DATA)

    def show_subtree(self, clk, level):
        gdb.write("%*s%-*s %7d %8d %8d\n" % (
                level * 3 + 1, "",
                30 - level * 3,
                clk['name'].string(),
                clk['enable_count'],
                clk['prepare_count'],
                clk['protect_count']))

        for child in clk_core_for_each_child(clk['children']):
            self.show_subtree(child, level + 1)

    def invoke(self, arg, from_tty):
        gdb.write("                                 enable  prepare  protect\n")
        gdb.write("   clock                          count    count    count\n")
        gdb.write("---------------------------------------------------------\n")
        for clk in clk_core_for_each_child(gdb.parse_and_eval("clk_root_list")):
            self.show_subtree(clk, 0)
        for clk in clk_core_for_each_child(gdb.parse_and_eval("clk_orphan_list")):
            self.show_subtree(clk, 0)


LxClkSummary()


class LxClkCoreLookup(gdb.Function):
    """Find struct clk_core by name"""

    def __init__(self):
        super(LxClkCoreLookup, self).__init__("lx_clk_core_lookup")

    def lookup_hlist(self, hlist_head, name):
        for child in clk_core_for_each_child(hlist_head):
            if child['name'].string() == name:
                return child
            result = self.lookup_hlist(child['children'], name)
            if result:
                return result

    def invoke(self, name):
        name = name.string()
        return (self.lookup_hlist(gdb.parse_and_eval("clk_root_list"), name) or
                self.lookup_hlist(gdb.parse_and_eval("clk_orphan_list"), name))


LxClkCoreLookup()
