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
