# SPDX-License-Identifier: GPL-2.0
#
# Copyright 2024 Canonical Ltd.
#
# Authors:
#  Kuan-Ying Lee <kuan-ying.lee@canonical.com>
#

import gdb
from linux import constants, mm

def help():
    t = """Usage: lx-kasan_mem_to_shadow [Hex memory addr]
    Example:
        lx-kasan_mem_to_shadow 0xffff000008eca008\n"""
    gdb.write("Unrecognized command\n")
    raise gdb.GdbError(t)

class KasanMemToShadow(gdb.Command):
    """Translate memory address to kasan shadow address"""

    p_ops = None

    def __init__(self):
        if constants.LX_CONFIG_KASAN_GENERIC or constants.LX_CONFIG_KASAN_SW_TAGS:
            super(KasanMemToShadow, self).__init__("lx-kasan_mem_to_shadow", gdb.COMMAND_SUPPORT)

    def invoke(self, args, from_tty):
        if not constants.LX_CONFIG_KASAN_GENERIC or constants.LX_CONFIG_KASAN_SW_TAGS:
            raise gdb.GdbError('CONFIG_KASAN_GENERIC or CONFIG_KASAN_SW_TAGS is not set')

        argv = gdb.string_to_argv(args)
        if len(argv) == 1:
            if self.p_ops is None:
                self.p_ops = mm.page_ops().ops
            addr = int(argv[0], 16)
            shadow_addr = self.kasan_mem_to_shadow(addr)
            gdb.write('shadow addr: 0x%x\n' % shadow_addr)
        else:
            help()
    def kasan_mem_to_shadow(self, addr):
        return (addr >> self.p_ops.KASAN_SHADOW_SCALE_SHIFT) + self.p_ops.KASAN_SHADOW_OFFSET

KasanMemToShadow()
