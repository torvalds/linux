#
# gdb helper commands and functions for Linux kernel debugging
#
#  load kernel and module symbols
#
# Copyright (c) Siemens AG, 2011-2013
#
# Authors:
#  Jan Kiszka <jan.kiszka@siemens.com>
#
# This work is licensed under the terms of the GNU GPL version 2.
#

import gdb
import os
import re
import string

from linux import modules, utils


class LxSymbols(gdb.Command):
    """(Re-)load symbols of Linux kernel and currently loaded modules.

The kernel (vmlinux) is taken from the current working directly. Modules (.ko)
are scanned recursively, starting in the same directory. Optionally, the module
search path can be extended by a space separated list of paths passed to the
lx-symbols command."""

    module_paths = []
    module_files = []
    module_files_updated = False

    def __init__(self):
        super(LxSymbols, self).__init__("lx-symbols", gdb.COMMAND_FILES,
                                        gdb.COMPLETE_FILENAME)

    def _update_module_files(self):
        self.module_files = []
        for path in self.module_paths:
            gdb.write("scanning for modules in {0}\n".format(path))
            for root, dirs, files in os.walk(path):
                for name in files:
                    if name.endswith(".ko"):
                        self.module_files.append(root + "/" + name)
        self.module_files_updated = True

    def _get_module_file(self, module_name):
        module_pattern = ".*/{0}\.ko$".format(
            string.replace(module_name, "_", r"[_\-]"))
        for name in self.module_files:
            if re.match(module_pattern, name) and os.path.exists(name):
                return name
        return None

    def _section_arguments(self, module):
        try:
            sect_attrs = module['sect_attrs'].dereference()
        except gdb.error:
            return ""
        attrs = sect_attrs['attrs']
        section_name_to_address = {
            attrs[n]['name'].string() : attrs[n]['address']
            for n in range(sect_attrs['nsections'])}
        args = []
        for section_name in [".data", ".data..read_mostly", ".rodata", ".bss"]:
            address = section_name_to_address.get(section_name)
            if address:
                args.append(" -s {name} {addr}".format(
                    name=section_name, addr=str(address)))
        return "".join(args)

    def load_module_symbols(self, module):
        module_name = module['name'].string()
        module_addr = str(module['module_core']).split()[0]

        module_file = self._get_module_file(module_name)
        if not module_file and not self.module_files_updated:
            self._update_module_files()
            module_file = self._get_module_file(module_name)

        if module_file:
            gdb.write("loading @{addr}: {filename}\n".format(
                addr=module_addr, filename=module_file))
            cmdline = "add-symbol-file {filename} {addr}{sections}".format(
                filename=module_file,
                addr=module_addr,
                sections=self._section_arguments(module))
            gdb.execute(cmdline, to_string=True)
        else:
            gdb.write("no module object found for '{0}'\n".format(module_name))

    def load_all_symbols(self):
        gdb.write("loading vmlinux\n")

        # Dropping symbols will disable all breakpoints. So save their states
        # and restore them afterward.
        saved_states = []
        if hasattr(gdb, 'breakpoints') and not gdb.breakpoints() is None:
            for bp in gdb.breakpoints():
                saved_states.append({'breakpoint': bp, 'enabled': bp.enabled})

        # drop all current symbols and reload vmlinux
        gdb.execute("symbol-file", to_string=True)
        gdb.execute("symbol-file vmlinux")

        module_list = modules.ModuleList()
        if not module_list:
            gdb.write("no modules found\n")
        else:
            [self.load_module_symbols(module) for module in module_list]

        for saved_state in saved_states:
            saved_state['breakpoint'].enabled = saved_state['enabled']

    def invoke(self, arg, from_tty):
        self.module_paths = arg.split()
        self.module_paths.append(os.getcwd())

        # enforce update
        self.module_files = []
        self.module_files_updated = False

        self.load_all_symbols()


LxSymbols()
