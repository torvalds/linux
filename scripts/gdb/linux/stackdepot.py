# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2023 MediaTek Inc.
#
# Authors:
#  Kuan-Ying Lee <Kuan-Ying.Lee@mediatek.com>
#

import gdb
from linux import utils, constants

if constants.LX_CONFIG_STACKDEPOT:
    stack_record_type = utils.CachedType('struct stack_record')
    DEPOT_STACK_ALIGN = 4

def help():
    t = """Usage: lx-stack_depot_lookup [Hex handle value]
    Example:
        lx-stack_depot_lookup 0x00c80300\n"""
    gdb.write("Unrecognized command\n")
    raise gdb.GdbError(t)

def stack_depot_fetch(handle):
    global DEPOT_STACK_ALIGN
    global stack_record_type

    stack_depot_disabled = gdb.parse_and_eval('stack_depot_disabled')

    if stack_depot_disabled:
        raise gdb.GdbError("stack_depot_disabled\n")

    handle_parts_t = gdb.lookup_type("union handle_parts")
    parts = handle.cast(handle_parts_t)
    offset = parts['offset'] << DEPOT_STACK_ALIGN
    pools_num = gdb.parse_and_eval('pools_num')

    if handle == 0:
        raise gdb.GdbError("handle is 0\n")

    pool_index = parts['pool_index_plus_1'] - 1
    if pool_index >= pools_num:
        gdb.write("pool index %d out of bounds (%d) for stack id 0x%08x\n" % (parts['pool_index'], pools_num, handle))
        return gdb.Value(0), 0

    stack_pools = gdb.parse_and_eval('stack_pools')

    try:
        pool = stack_pools[pool_index]
        stack = (pool + gdb.Value(offset).cast(utils.get_size_t_type())).cast(stack_record_type.get_type().pointer())
        size = int(stack['size'].cast(utils.get_ulong_type()))
        return stack['entries'], size
    except Exception as e:
        gdb.write("%s\n" % e)
        return gdb.Value(0), 0

def stack_depot_print(handle):
    if not constants.LX_CONFIG_STACKDEPOT:
        raise gdb.GdbError("CONFIG_STACKDEPOT is not enabled")

    entries, nr_entries = stack_depot_fetch(handle)
    if nr_entries > 0:
        for i in range(0, nr_entries):
            try:
                gdb.execute("x /i 0x%x" % (int(entries[i])))
            except Exception as e:
                gdb.write("%s\n" % e)

class StackDepotLookup(gdb.Command):
    """Search backtrace by handle"""

    def __init__(self):
        if constants.LX_CONFIG_STACKDEPOT:
            super(StackDepotLookup, self).__init__("lx-stack_depot_lookup", gdb.COMMAND_SUPPORT)

    def invoke(self, args, from_tty):
        if not constants.LX_CONFIG_STACKDEPOT:
            raise gdb.GdbError('CONFIG_STACKDEPOT is not set')

        argv = gdb.string_to_argv(args)
        if len(argv) == 1:
            handle = int(argv[0], 16)
            stack_depot_print(gdb.Value(handle).cast(utils.get_uint_type()))
        else:
            help()

StackDepotLookup()
