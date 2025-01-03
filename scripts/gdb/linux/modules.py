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

from linux import cpus, utils, lists, constants


module_type = utils.CachedType("struct module")


def has_modules():
    return utils.gdb_eval_or_none("modules") is not None

def module_list():
    global module_type
    modules = utils.gdb_eval_or_none("modules")
    if modules is None:
        return

    module_ptr_type = module_type.get_type().pointer()

    for module in lists.list_for_each_entry(modules, module_ptr_type, "list"):
        yield module


def find_module_by_name(name):
    for module in module_list():
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

        for module in module_list():
            text = module['mem'][constants.LX_MOD_TEXT]
            text_addr = str(text['base']).split()[0]
            total_size = 0

            for i in range(constants.LX_MOD_TEXT, constants.LX_MOD_RO_AFTER_INIT + 1):
                total_size += module['mem'][i]['size']

            gdb.write("{address} {name:<19} {size:>8}  {ref}".format(
                address=text_addr,
                name=module['name'].string(),
                size=str(total_size),
                ref=str(module['refcnt']['counter'] - 1)))

            t = self._module_use_type.get_type().pointer()
            first = True
            sources = module['source_list']
            for use in lists.list_for_each_entry(sources, t, "source_list"):
                gdb.write("{separator}{name}".format(
                    separator=" " if first else ",",
                    name=use['source']['name'].string()))
                first = False

            gdb.write("\n")

LxLsmod()

def help():
    t = """Usage: lx-getmod-by-textaddr [Heximal Address]
    Example: lx-getmod-by-textaddr 0xffff800002d305ac\n"""
    gdb.write("Unrecognized command\n")
    raise gdb.GdbError(t)

class LxFindTextAddrinMod(gdb.Command):
    '''Look up loaded kernel module by text address.'''

    def __init__(self):
        super(LxFindTextAddrinMod, self).__init__('lx-getmod-by-textaddr', gdb.COMMAND_SUPPORT)

    def invoke(self, arg, from_tty):
        args = gdb.string_to_argv(arg)

        if len(args) != 1:
            help()

        addr = gdb.Value(int(args[0], 16)).cast(utils.get_ulong_type())
        for mod in module_list():
            mod_text_start = mod['mem'][constants.LX_MOD_TEXT]['base']
            mod_text_end = mod_text_start + mod['mem'][constants.LX_MOD_TEXT]['size'].cast(utils.get_ulong_type())

            if addr >= mod_text_start and addr < mod_text_end:
                s = "0x%x" % addr + " is in " + mod['name'].string() + ".ko\n"
                gdb.write(s)
                return
        gdb.write("0x%x is not in any module text section\n" % addr)

LxFindTextAddrinMod()
