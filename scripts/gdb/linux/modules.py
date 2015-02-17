#
# gdb helper commands and functions for Linux kernel debugging
#
#  module tools
#
# Copyright (c) Siemens AG, 2013
#
# Authors:
#  Jan Kiszka <jan.kiszka@siemens.com>
#
# This work is licensed under the terms of the GNU GPL version 2.
#

import gdb

from linux import utils


module_type = utils.CachedType("struct module")


class ModuleList:
    def __init__(self):
        global module_type
        self.module_ptr_type = module_type.get_type().pointer()
        modules = gdb.parse_and_eval("modules")
        self.curr_entry = modules['next']
        self.end_of_list = modules.address

    def __iter__(self):
        return self

    def next(self):
        entry = self.curr_entry
        if entry != self.end_of_list:
            self.curr_entry = entry['next']
            return utils.container_of(entry, self.module_ptr_type, "list")
        else:
            raise StopIteration


def find_module_by_name(name):
    for module in ModuleList():
        if module['name'].string() == name:
            return module
    return None


class LxModule(gdb.Function):
    """Find module by name and return the module variable.

$lx_module("MODULE"): Given the name MODULE, iterate over all loaded modules
of the target and return that module variable which MODULE matches."""

    def __init__(self):
        super(LxModule, self).__init__("lx_module")

    def invoke(self, mod_name):
        mod_name = mod_name.string()
        module = find_module_by_name(mod_name)
        if module:
            return module.dereference()
        else:
            raise gdb.GdbError("Unable to find MODULE " + mod_name)


LxModule()
