#
# gdb helper commands and functions for Linux kernel debugging
#
#  Kernel proc information reader
#
# Copyright (c) 2016 Linaro Ltd
#
# Authors:
#  Kieran Bingham <kieran.bingham@linaro.org>
#
# This work is licensed under the terms of the GNU GPL version 2.
#

import gdb


class LxCmdLine(gdb.Command):
    """ Report the Linux Commandline used in the current kernel.
        Equivalent to cat /proc/cmdline on a running target"""

    def __init__(self):
        super(LxCmdLine, self).__init__("lx-cmdline", gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        gdb.write(gdb.parse_and_eval("saved_command_line").string() + "\n")

LxCmdLine()


class LxVersion(gdb.Command):
    """ Report the Linux Version of the current kernel.
        Equivalent to cat /proc/version on a running target"""

    def __init__(self):
        super(LxVersion, self).__init__("lx-version", gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        # linux_banner should contain a newline
        gdb.write(gdb.parse_and_eval("linux_banner").string())

LxVersion()


# Resource Structure Printers
#  /proc/iomem
#  /proc/ioports

def get_resources(resource, depth):
    while resource:
        yield resource, depth

        child = resource['child']
        if child:
            for res, deep in get_resources(child, depth + 1):
                yield res, deep

        resource = resource['sibling']


def show_lx_resources(resource_str):
        resource = gdb.parse_and_eval(resource_str)
        width = 4 if resource['end'] < 0x10000 else 8
        # Iterate straight to the first child
        for res, depth in get_resources(resource['child'], 0):
            start = int(res['start'])
            end = int(res['end'])
            gdb.write(" " * depth * 2 +
                      "{0:0{1}x}-".format(start, width) +
                      "{0:0{1}x} : ".format(end, width) +
                      res['name'].string() + "\n")


class LxIOMem(gdb.Command):
    """Identify the IO memory resource locations defined by the kernel

Equivalent to cat /proc/iomem on a running target"""

    def __init__(self):
        super(LxIOMem, self).__init__("lx-iomem", gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        return show_lx_resources("iomem_resource")

LxIOMem()


class LxIOPorts(gdb.Command):
    """Identify the IO port resource locations defined by the kernel

Equivalent to cat /proc/ioports on a running target"""

    def __init__(self):
        super(LxIOPorts, self).__init__("lx-ioports", gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        return show_lx_resources("ioport_resource")

LxIOPorts()
