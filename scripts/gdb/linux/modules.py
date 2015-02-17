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

from linux import cpus, utils


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

    def __next__(self):
        entry = self.curr_entry
        if entry != self.end_of_list:
            self.curr_entry = entry['next']
            return utils.container_of(entry, self.module_ptr_type, "list")
        else:
            raise StopIteration

    def next(self):
        return self.__next__()


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


class LxLsmod(gdb.Command):
    """List currently loaded modules."""

    _module_use_type = utils.CachedType("struct module_use")

    def __init__(self):
        super(LxLsmod, self).__init__("lx-lsmod", gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        gdb.write(
            "Address{0}    Module                  Size  Used by\n".format(
                "        " if utils.get_long_type().sizeof == 8 else ""))

        for module in ModuleList():
            ref = 0
            module_refptr = module['refptr']
            for cpu in cpus.CpuList("cpu_possible_mask"):
                refptr = cpus.per_cpu(module_refptr, cpu)
                ref += refptr['incs']
                ref -= refptr['decs']

            gdb.write("{address} {name:<19} {size:>8}  {ref}".format(
                address=str(module['module_core']).split()[0],
                name=module['name'].string(),
                size=str(module['core_size']),
                ref=str(ref)))

            source_list = module['source_list']
            t = self._module_use_type.get_type().pointer()
            entry = source_list['next']
            first = True
            while entry != source_list.address:
                use = utils.container_of(entry, t, "source_list")
                gdb.write("{separator}{name}".format(
                    separator=" " if first else ",",
                    name=use['source']['name'].string()))
                first = False
                entry = entry['next']
            gdb.write("\n")


LxLsmod()
