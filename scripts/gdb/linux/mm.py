# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2023 MediaTek Inc.
#
# Authors:
#  Kuan-Ying Lee <Kuan-Ying.Lee@mediatek.com>
#

import gdb
import math
from linux import utils, constants

def DIV_ROUND_UP(n,d):
    return ((n) + (d) - 1) // (d)

def test_bit(nr, addr):
    if addr.dereference() & (0x1 << nr):
        return True
    else:
        return False

class page_ops():
    ops = None
    def __init__(self):
        if not constants.LX_CONFIG_SPARSEMEM_VMEMMAP:
            raise gdb.GdbError('Only support CONFIG_SPARSEMEM_VMEMMAP now')
        if constants.LX_CONFIG_ARM64 and utils.is_target_arch('aarch64'):
            self.ops = aarch64_page_ops()
        else:
            raise gdb.GdbError('Only support aarch64 now')

class aarch64_page_ops():
    def __init__(self):
        self.SUBSECTION_SHIFT = 21
        self.SEBSECTION_SIZE = 1 << self.SUBSECTION_SHIFT
        self.MODULES_VSIZE = 2 * 1024 * 1024 * 1024

        if constants.LX_CONFIG_ARM64_64K_PAGES:
            self.SECTION_SIZE_BITS = 29
        else:
            self.SECTION_SIZE_BITS = 27
        self.MAX_PHYSMEM_BITS = constants.LX_CONFIG_ARM64_VA_BITS

        self.PAGE_SHIFT = constants.LX_CONFIG_PAGE_SHIFT
        self.PAGE_SIZE = 1 << self.PAGE_SHIFT
        self.PAGE_MASK = (~(self.PAGE_SIZE - 1)) & ((1 << 64) - 1)

        self.VA_BITS = constants.LX_CONFIG_ARM64_VA_BITS
        if self.VA_BITS > 48:
            if constants.LX_CONFIG_ARM64_16K_PAGES:
                self.VA_BITS_MIN = 47
            else:
                self.VA_BITS_MIN = 48
            tcr_el1 = gdb.execute("info registers $TCR_EL1", to_string=True)
            tcr_el1 = int(tcr_el1.split()[1], 16)
            self.vabits_actual = 64 - ((tcr_el1 >> 16) & 63)
        else:
            self.VA_BITS_MIN = self.VA_BITS
            self.vabits_actual = self.VA_BITS
        self.kimage_voffset = gdb.parse_and_eval('kimage_voffset') & ((1 << 64) - 1)

        self.SECTIONS_SHIFT = self.MAX_PHYSMEM_BITS - self.SECTION_SIZE_BITS

        if str(constants.LX_CONFIG_ARCH_FORCE_MAX_ORDER).isdigit():
            self.MAX_ORDER = constants.LX_CONFIG_ARCH_FORCE_MAX_ORDER
        else:
            self.MAX_ORDER = 10

        self.MAX_ORDER_NR_PAGES = 1 << (self.MAX_ORDER)
        self.PFN_SECTION_SHIFT = self.SECTION_SIZE_BITS - self.PAGE_SHIFT
        self.NR_MEM_SECTIONS = 1 << self.SECTIONS_SHIFT
        self.PAGES_PER_SECTION = 1 << self.PFN_SECTION_SHIFT
        self.PAGE_SECTION_MASK = (~(self.PAGES_PER_SECTION - 1)) & ((1 << 64) - 1)

        if constants.LX_CONFIG_SPARSEMEM_EXTREME:
            self.SECTIONS_PER_ROOT = self.PAGE_SIZE // gdb.lookup_type("struct mem_section").sizeof
        else:
            self.SECTIONS_PER_ROOT = 1

        self.NR_SECTION_ROOTS = DIV_ROUND_UP(self.NR_MEM_SECTIONS, self.SECTIONS_PER_ROOT)
        self.SECTION_ROOT_MASK = self.SECTIONS_PER_ROOT - 1
        self.SUBSECTION_SHIFT = 21
        self.SEBSECTION_SIZE = 1 << self.SUBSECTION_SHIFT
        self.PFN_SUBSECTION_SHIFT = self.SUBSECTION_SHIFT - self.PAGE_SHIFT
        self.PAGES_PER_SUBSECTION = 1 << self.PFN_SUBSECTION_SHIFT

        self.SECTION_HAS_MEM_MAP = 1 << int(gdb.parse_and_eval('SECTION_HAS_MEM_MAP_BIT'))
        self.SECTION_IS_EARLY = 1 << int(gdb.parse_and_eval('SECTION_IS_EARLY_BIT'))

        self.struct_page_size = utils.get_page_type().sizeof
        self.STRUCT_PAGE_MAX_SHIFT = (int)(math.log(self.struct_page_size, 2))

        self.PAGE_OFFSET = self._PAGE_OFFSET(self.VA_BITS)
        self.MODULES_VADDR = self._PAGE_END(self.VA_BITS_MIN)
        self.MODULES_END = self.MODULES_VADDR + self.MODULES_VSIZE

        self.VMEMMAP_RANGE = self._PAGE_END(self.VA_BITS_MIN) - self.PAGE_OFFSET
        self.VMEMMAP_SIZE = (self.VMEMMAP_RANGE >> self.PAGE_SHIFT) * self.struct_page_size
        self.VMEMMAP_END = (-(1 * 1024 * 1024 * 1024)) & 0xffffffffffffffff
        self.VMEMMAP_START = self.VMEMMAP_END - self.VMEMMAP_SIZE

        self.VMALLOC_START = self.MODULES_END
        self.VMALLOC_END = self.VMEMMAP_START - 256 * 1024 * 1024

        self.memstart_addr = gdb.parse_and_eval("memstart_addr")
        self.PHYS_OFFSET = self.memstart_addr
        self.vmemmap = gdb.Value(self.VMEMMAP_START).cast(utils.get_page_type().pointer()) - (self.memstart_addr >> self.PAGE_SHIFT)

        self.KERNEL_START = gdb.parse_and_eval("_text")
        self.KERNEL_END = gdb.parse_and_eval("_end")

        if constants.LX_CONFIG_KASAN_GENERIC or constants.LX_CONFIG_KASAN_SW_TAGS:
            if constants.LX_CONFIG_KASAN_GENERIC:
                self.KASAN_SHADOW_SCALE_SHIFT = 3
            else:
                self.KASAN_SHADOW_SCALE_SHIFT = 4
            self.KASAN_SHADOW_OFFSET = constants.LX_CONFIG_KASAN_SHADOW_OFFSET
            self.KASAN_SHADOW_END = (1 << (64 - self.KASAN_SHADOW_SCALE_SHIFT)) + self.KASAN_SHADOW_OFFSET
            self.PAGE_END = self.KASAN_SHADOW_END - (1 << (self.vabits_actual - self.KASAN_SHADOW_SCALE_SHIFT))
        else:
            self.PAGE_END = self._PAGE_END(self.VA_BITS_MIN)

        if constants.LX_CONFIG_NUMA and constants.LX_CONFIG_NODES_SHIFT:
            self.NODE_SHIFT = constants.LX_CONFIG_NODES_SHIFT
        else:
            self.NODE_SHIFT = 0

        self.MAX_NUMNODES = 1 << self.NODE_SHIFT

    def SECTION_NR_TO_ROOT(self, sec):
        return sec // self.SECTIONS_PER_ROOT

    def __nr_to_section(self, nr):
        root = self.SECTION_NR_TO_ROOT(nr)
        mem_section = gdb.parse_and_eval("mem_section")
        return mem_section[root][nr & self.SECTION_ROOT_MASK]

    def pfn_to_section_nr(self, pfn):
        return pfn >> self.PFN_SECTION_SHIFT

    def section_nr_to_pfn(self, sec):
        return sec << self.PFN_SECTION_SHIFT

    def __pfn_to_section(self, pfn):
        return self.__nr_to_section(self.pfn_to_section_nr(pfn))

    def pfn_to_section(self, pfn):
        return self.__pfn_to_section(pfn)

    def subsection_map_index(self, pfn):
        return (pfn & ~(self.PAGE_SECTION_MASK)) // self.PAGES_PER_SUBSECTION

    def pfn_section_valid(self, ms, pfn):
        if constants.LX_CONFIG_SPARSEMEM_VMEMMAP:
            idx = self.subsection_map_index(pfn)
            return test_bit(idx, ms['usage']['subsection_map'])
        else:
            return True

    def valid_section(self, mem_section):
        if mem_section != None and (mem_section['section_mem_map'] & self.SECTION_HAS_MEM_MAP):
            return True
        return False

    def early_section(self, mem_section):
        if mem_section != None and (mem_section['section_mem_map'] & self.SECTION_IS_EARLY):
            return True
        return False

    def pfn_valid(self, pfn):
        ms = None
        if self.PHYS_PFN(self.PFN_PHYS(pfn)) != pfn:
            return False
        if self.pfn_to_section_nr(pfn) >= self.NR_MEM_SECTIONS:
            return False
        ms = self.__pfn_to_section(pfn)

        if not self.valid_section(ms):
            return False
        return self.early_section(ms) or self.pfn_section_valid(ms, pfn)

    def _PAGE_OFFSET(self, va):
        return (-(1 << (va))) & 0xffffffffffffffff

    def _PAGE_END(self, va):
        return (-(1 << (va - 1))) & 0xffffffffffffffff

    def kasan_reset_tag(self, addr):
        if constants.LX_CONFIG_KASAN_SW_TAGS or constants.LX_CONFIG_KASAN_HW_TAGS:
            return int(addr) | (0xff << 56)
        else:
            return addr

    def __is_lm_address(self, addr):
        if (addr - self.PAGE_OFFSET) < (self.PAGE_END - self.PAGE_OFFSET):
            return True
        else:
            return False
    def __lm_to_phys(self, addr):
        return addr - self.PAGE_OFFSET + self.PHYS_OFFSET

    def __kimg_to_phys(self, addr):
        return addr - self.kimage_voffset

    def __virt_to_phys_nodebug(self, va):
        untagged_va = self.kasan_reset_tag(va)
        if self.__is_lm_address(untagged_va):
            return self.__lm_to_phys(untagged_va)
        else:
            return self.__kimg_to_phys(untagged_va)

    def __virt_to_phys(self, va):
        if constants.LX_CONFIG_DEBUG_VIRTUAL:
            if not self.__is_lm_address(self.kasan_reset_tag(va)):
                raise gdb.GdbError("Warning: virt_to_phys used for non-linear address: 0x%lx\n" % va)
        return self.__virt_to_phys_nodebug(va)

    def virt_to_phys(self, va):
        return self.__virt_to_phys(va)

    def PFN_PHYS(self, pfn):
        return pfn << self.PAGE_SHIFT

    def PHYS_PFN(self, phys):
        return phys >> self.PAGE_SHIFT

    def __phys_to_virt(self, pa):
        return (pa - self.PHYS_OFFSET) | self.PAGE_OFFSET

    def __phys_to_pfn(self, pa):
        return self.PHYS_PFN(pa)

    def __pfn_to_phys(self, pfn):
        return self.PFN_PHYS(pfn)

    def __pa_symbol_nodebug(self, x):
        return self.__kimg_to_phys(x)

    def __phys_addr_symbol(self, x):
        if constants.LX_CONFIG_DEBUG_VIRTUAL:
            if x < self.KERNEL_START or x > self.KERNEL_END:
                raise gdb.GdbError("0x%x exceed kernel range" % x)
        return self.__pa_symbol_nodebug(x)

    def __pa_symbol(self, x):
        return self.__phys_addr_symbol(x)

    def __va(self, pa):
        return self.__phys_to_virt(pa)

    def pfn_to_kaddr(self, pfn):
        return self.__va(pfn << self.PAGE_SHIFT)

    def virt_to_pfn(self, va):
        return self.__phys_to_pfn(self.__virt_to_phys(va))

    def sym_to_pfn(self, x):
        return self.__phys_to_pfn(self.__pa_symbol(x))

    def page_to_pfn(self, page):
        return int(page.cast(utils.get_page_type().pointer()) - self.vmemmap.cast(utils.get_page_type().pointer()))

    def page_to_phys(self, page):
        return self.__pfn_to_phys(self.page_to_pfn(page))

    def pfn_to_page(self, pfn):
        return (self.vmemmap + pfn).cast(utils.get_page_type().pointer())

    def page_to_virt(self, page):
        if constants.LX_CONFIG_DEBUG_VIRTUAL:
            return self.__va(self.page_to_phys(page))
        else:
            __idx = int((page.cast(gdb.lookup_type("unsigned long")) - self.VMEMMAP_START).cast(utils.get_ulong_type())) // self.struct_page_size
            return self.PAGE_OFFSET + (__idx * self.PAGE_SIZE)

    def virt_to_page(self, va):
        if constants.LX_CONFIG_DEBUG_VIRTUAL:
            return self.pfn_to_page(self.virt_to_pfn(va))
        else:
            __idx = int(self.kasan_reset_tag(va) - self.PAGE_OFFSET) // self.PAGE_SIZE
            addr = self.VMEMMAP_START + (__idx * self.struct_page_size)
            return gdb.Value(addr).cast(utils.get_page_type().pointer())

    def page_address(self, page):
        return self.page_to_virt(page)

    def folio_address(self, folio):
        return self.page_address(folio['page'].address)

class LxPFN2Page(gdb.Command):
    """PFN to struct page"""

    def __init__(self):
        super(LxPFN2Page, self).__init__("lx-pfn_to_page", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        argv = gdb.string_to_argv(arg)
        pfn = int(argv[0])
        page = page_ops().ops.pfn_to_page(pfn)
        gdb.write("pfn_to_page(0x%x) = 0x%x\n" % (pfn, page))

LxPFN2Page()

class LxPage2PFN(gdb.Command):
    """struct page to PFN"""

    def __init__(self):
        super(LxPage2PFN, self).__init__("lx-page_to_pfn", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        argv = gdb.string_to_argv(arg)
        struct_page_addr = int(argv[0], 16)
        page = gdb.Value(struct_page_addr).cast(utils.get_page_type().pointer())
        pfn = page_ops().ops.page_to_pfn(page)
        gdb.write("page_to_pfn(0x%x) = 0x%x\n" % (page, pfn))

LxPage2PFN()

class LxPageAddress(gdb.Command):
    """struct page to linear mapping address"""

    def __init__(self):
        super(LxPageAddress, self).__init__("lx-page_address", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        argv = gdb.string_to_argv(arg)
        struct_page_addr = int(argv[0], 16)
        page = gdb.Value(struct_page_addr).cast(utils.get_page_type().pointer())
        addr = page_ops().ops.page_address(page)
        gdb.write("page_address(0x%x) = 0x%x\n" % (page, addr))

LxPageAddress()

class LxPage2Phys(gdb.Command):
    """struct page to physical address"""

    def __init__(self):
        super(LxPage2Phys, self).__init__("lx-page_to_phys", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        argv = gdb.string_to_argv(arg)
        struct_page_addr = int(argv[0], 16)
        page = gdb.Value(struct_page_addr).cast(utils.get_page_type().pointer())
        phys_addr = page_ops().ops.page_to_phys(page)
        gdb.write("page_to_phys(0x%x) = 0x%x\n" % (page, phys_addr))

LxPage2Phys()

class LxVirt2Phys(gdb.Command):
    """virtual address to physical address"""

    def __init__(self):
        super(LxVirt2Phys, self).__init__("lx-virt_to_phys", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        argv = gdb.string_to_argv(arg)
        linear_addr = int(argv[0], 16)
        phys_addr = page_ops().ops.virt_to_phys(linear_addr)
        gdb.write("virt_to_phys(0x%x) = 0x%x\n" % (linear_addr, phys_addr))

LxVirt2Phys()

class LxVirt2Page(gdb.Command):
    """virtual address to struct page"""

    def __init__(self):
        super(LxVirt2Page, self).__init__("lx-virt_to_page", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        argv = gdb.string_to_argv(arg)
        linear_addr = int(argv[0], 16)
        page = page_ops().ops.virt_to_page(linear_addr)
        gdb.write("virt_to_page(0x%x) = 0x%x\n" % (linear_addr, page))

LxVirt2Page()

class LxSym2PFN(gdb.Command):
    """symbol address to PFN"""

    def __init__(self):
        super(LxSym2PFN, self).__init__("lx-sym_to_pfn", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        argv = gdb.string_to_argv(arg)
        sym_addr = int(argv[0], 16)
        pfn = page_ops().ops.sym_to_pfn(sym_addr)
        gdb.write("sym_to_pfn(0x%x) = %d\n" % (sym_addr, pfn))

LxSym2PFN()

class LxPFN2Kaddr(gdb.Command):
    """PFN to kernel address"""

    def __init__(self):
        super(LxPFN2Kaddr, self).__init__("lx-pfn_to_kaddr", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        argv = gdb.string_to_argv(arg)
        pfn = int(argv[0])
        kaddr = page_ops().ops.pfn_to_kaddr(pfn)
        gdb.write("pfn_to_kaddr(%d) = 0x%x\n" % (pfn, kaddr))

LxPFN2Kaddr()
