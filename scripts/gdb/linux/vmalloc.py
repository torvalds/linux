# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2023 MediaTek Inc.
#
# Authors:
#  Kuan-Ying Lee <Kuan-Ying.Lee@mediatek.com>
#

import gdb
import re
from linux import lists, utils, stackdepot, constants, mm

if constants.LX_CONFIG_MMU:
    vmap_area_type = utils.CachedType('struct vmap_area')
    vmap_area_ptr_type = vmap_area_type.get_type().pointer()

def is_vmalloc_addr(x):
    pg_ops = mm.page_ops().ops
    addr = pg_ops.kasan_reset_tag(x)
    return addr >= pg_ops.VMALLOC_START and addr < pg_ops.VMALLOC_END

class LxVmallocInfo(gdb.Command):
    """Show vmallocinfo"""

    def __init__(self):
        super(LxVmallocInfo, self).__init__("lx-vmallocinfo", gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        if not constants.LX_CONFIG_MMU:
            raise gdb.GdbError("Requires MMU support")

        nr_vmap_nodes = gdb.parse_and_eval('nr_vmap_nodes')
        for i in range(0, nr_vmap_nodes):
            vn = gdb.parse_and_eval('&vmap_nodes[%d]' % i)
            for vmap_area in lists.list_for_each_entry(vn['busy']['head'], vmap_area_ptr_type, "list"):
                if not vmap_area['vm']:
                    gdb.write("0x%x-0x%x %10d vm_map_ram\n" % (vmap_area['va_start'], vmap_area['va_end'],
                        vmap_area['va_end'] - vmap_area['va_start']))
                    continue
                v = vmap_area['vm']
                gdb.write("0x%x-0x%x %10d" % (v['addr'], v['addr'] + v['size'], v['size']))
                if v['caller']:
                    gdb.write(" %s" % str(v['caller']).split(' ')[-1])
                if v['nr_pages']:
                    gdb.write(" pages=%d" % v['nr_pages'])
                if v['phys_addr']:
                    gdb.write(" phys=0x%x" % v['phys_addr'])
                if v['flags'] & constants.LX_VM_IOREMAP:
                    gdb.write(" ioremap")
                if v['flags'] & constants.LX_VM_ALLOC:
                    gdb.write(" vmalloc")
                if v['flags'] & constants.LX_VM_MAP:
                    gdb.write(" vmap")
                if v['flags'] & constants.LX_VM_USERMAP:
                    gdb.write(" user")
                if v['flags'] & constants.LX_VM_DMA_COHERENT:
                    gdb.write(" dma-coherent")
                if is_vmalloc_addr(v['pages']):
                    gdb.write(" vpages")
                gdb.write("\n")

LxVmallocInfo()
