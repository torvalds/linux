#
# gdb helper commands and functions for Linux kernel debugging
#
#  per-cpu tools
#
# Copyright (c) Siemens AG, 2011-2013
#
# Authors:
#  Jan Kiszka <jan.kiszka@siemens.com>
#
# This work is licensed under the terms of the GNU GPL version 2.
#

import gdb

from linux import tasks, utils


MAX_CPUS = 4096


def get_current_cpu():
    if utils.get_gdbserver_type() == utils.GDBSERVER_QEMU:
        return gdb.selected_thread().num - 1
    elif utils.get_gdbserver_type() == utils.GDBSERVER_KGDB:
        tid = gdb.selected_thread().ptid[2]
        if tid > (0x100000000 - MAX_CPUS - 2):
            return 0x100000000 - tid - 2
        else:
            return tasks.get_thread_info(tasks.get_task_by_pid(tid))['cpu']
    else:
        raise gdb.GdbError("Sorry, obtaining the current CPU is not yet "
                           "supported with this gdb server.")


def per_cpu(var_ptr, cpu):
    if cpu == -1:
        cpu = get_current_cpu()
    if utils.is_target_arch("sparc:v9"):
        offset = gdb.parse_and_eval(
            "trap_block[{0}].__per_cpu_base".format(str(cpu)))
    else:
        try:
            offset = gdb.parse_and_eval(
                "__per_cpu_offset[{0}]".format(str(cpu)))
        except gdb.error:
            # !CONFIG_SMP case
            offset = 0
    pointer = var_ptr.cast(utils.get_long_type()) + offset
    return pointer.cast(var_ptr.type).dereference()


class PerCpu(gdb.Function):
    """Return per-cpu variable.

$lx_per_cpu("VAR"[, CPU]): Return the per-cpu variable called VAR for the
given CPU number. If CPU is omitted, the CPU of the current context is used.
Note that VAR has to be quoted as string."""

    def __init__(self):
        super(PerCpu, self).__init__("lx_per_cpu")

    def invoke(self, var_name, cpu=-1):
        var_ptr = gdb.parse_and_eval("&" + var_name.string())
        return per_cpu(var_ptr, cpu)


PerCpu()
