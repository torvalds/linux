# SPDX-License-Identifier: GPL-2.0-only
#
# gdb helper commands and functions for Linux kernel debugging
#
#  routines to introspect page table
#
# Authors:
#  Dmitrii Bundin <dmitrii.bundin.a@gmail.com>
#

import gdb

from linux import utils

PHYSICAL_ADDRESS_MASK = gdb.parse_and_eval('0xfffffffffffff')


def page_mask(level=1):
    # 4KB
    if level == 1:
        return gdb.parse_and_eval('(u64) ~0xfff')
    # 2MB
    elif level == 2:
        return gdb.parse_and_eval('(u64) ~0x1fffff')
    # 1GB
    elif level == 3:
        return gdb.parse_and_eval('(u64) ~0x3fffffff')
    else:
        raise Exception(f'Unknown page level: {level}')


#page_offset_base in case CONFIG_DYNAMIC_MEMORY_LAYOUT is disabled
POB_NO_DYNAMIC_MEM_LAYOUT = '0xffff888000000000'
def _page_offset_base():
    pob_symbol = gdb.lookup_global_symbol('page_offset_base')
    pob = pob_symbol.name if pob_symbol else POB_NO_DYNAMIC_MEM_LAYOUT
    return gdb.parse_and_eval(pob)


def is_bit_defined_tupled(data, offset):
    return offset, bool(data >> offset & 1)

def content_tupled(data, bit_start, bit_end):
    return (bit_start, bit_end), data >> bit_start & ((1 << (1 + bit_end - bit_start)) - 1)

def entry_va(level, phys_addr, translating_va):
        def start_bit(level):
            if level == 5:
                return 48
            elif level == 4:
                return 39
            elif level == 3:
                return 30
            elif level == 2:
                return 21
            elif level == 1:
                return 12
            else:
                raise Exception(f'Unknown level {level}')

        entry_offset =  ((translating_va >> start_bit(level)) & 511) * 8
        entry_va = _page_offset_base() + phys_addr + entry_offset
        return entry_va

class Cr3():
    def __init__(self, cr3, page_levels):
        self.cr3 = cr3
        self.page_levels = page_levels
        self.page_level_write_through = is_bit_defined_tupled(cr3, 3)
        self.page_level_cache_disabled = is_bit_defined_tupled(cr3, 4)
        self.next_entry_physical_address = cr3 & PHYSICAL_ADDRESS_MASK & page_mask()

    def next_entry(self, va):
        next_level = self.page_levels
        return PageHierarchyEntry(entry_va(next_level, self.next_entry_physical_address, va), next_level)

    def mk_string(self):
            return f"""\
cr3:
    {'cr3 binary data': <30} {hex(self.cr3)}
    {'next entry physical address': <30} {hex(self.next_entry_physical_address)}
    ---
    {'bit' : <4} {self.page_level_write_through[0]: <10} {'page level write through': <30} {self.page_level_write_through[1]}
    {'bit' : <4} {self.page_level_cache_disabled[0]: <10} {'page level cache disabled': <30} {self.page_level_cache_disabled[1]}
"""


class PageHierarchyEntry():
    def __init__(self, address, level):
        data = int.from_bytes(
            memoryview(gdb.selected_inferior().read_memory(address, 8)),
            "little"
        )
        if level == 1:
            self.is_page = True
            self.entry_present = is_bit_defined_tupled(data, 0)
            self.read_write = is_bit_defined_tupled(data, 1)
            self.user_access_allowed = is_bit_defined_tupled(data, 2)
            self.page_level_write_through = is_bit_defined_tupled(data, 3)
            self.page_level_cache_disabled = is_bit_defined_tupled(data, 4)
            self.entry_was_accessed = is_bit_defined_tupled(data, 5)
            self.dirty = is_bit_defined_tupled(data, 6)
            self.pat = is_bit_defined_tupled(data, 7)
            self.global_translation = is_bit_defined_tupled(data, 8)
            self.page_physical_address = data & PHYSICAL_ADDRESS_MASK & page_mask(level)
            self.next_entry_physical_address = None
            self.hlat_restart_with_ordinary = is_bit_defined_tupled(data, 11)
            self.protection_key = content_tupled(data, 59, 62)
            self.executed_disable = is_bit_defined_tupled(data, 63)
        else:
            page_size = is_bit_defined_tupled(data, 7)
            page_size_bit = page_size[1]
            self.is_page = page_size_bit
            self.entry_present = is_bit_defined_tupled(data, 0)
            self.read_write = is_bit_defined_tupled(data, 1)
            self.user_access_allowed = is_bit_defined_tupled(data, 2)
            self.page_level_write_through = is_bit_defined_tupled(data, 3)
            self.page_level_cache_disabled = is_bit_defined_tupled(data, 4)
            self.entry_was_accessed = is_bit_defined_tupled(data, 5)
            self.page_size = page_size
            self.dirty = is_bit_defined_tupled(
                data, 6) if page_size_bit else None
            self.global_translation = is_bit_defined_tupled(
                data, 8) if page_size_bit else None
            self.pat = is_bit_defined_tupled(
                data, 12) if page_size_bit else None
            self.page_physical_address = data & PHYSICAL_ADDRESS_MASK & page_mask(level) if page_size_bit else None
            self.next_entry_physical_address = None if page_size_bit else data & PHYSICAL_ADDRESS_MASK & page_mask()
            self.hlat_restart_with_ordinary = is_bit_defined_tupled(data, 11)
            self.protection_key = content_tupled(data, 59, 62) if page_size_bit else None
            self.executed_disable = is_bit_defined_tupled(data, 63)
        self.address = address
        self.page_entry_binary_data = data
        self.page_hierarchy_level = level

    def next_entry(self, va):
        if self.is_page or not self.entry_present[1]:
            return None

        next_level = self.page_hierarchy_level - 1
        return PageHierarchyEntry(entry_va(next_level, self.next_entry_physical_address, va), next_level)


    def mk_string(self):
        if not self.entry_present[1]:
            return f"""\
level {self.page_hierarchy_level}:
    {'entry address': <30} {hex(self.address)}
    {'page entry binary data': <30} {hex(self.page_entry_binary_data)}
    ---
    PAGE ENTRY IS NOT PRESENT!
"""
        elif self.is_page:
            def page_size_line(ps_bit, ps, level):
                return "" if level == 1 else f"{'bit': <3} {ps_bit: <5} {'page size': <30} {ps}"

            return f"""\
level {self.page_hierarchy_level}:
    {'entry address': <30} {hex(self.address)}
    {'page entry binary data': <30} {hex(self.page_entry_binary_data)}
    {'page size': <30} {'1GB' if self.page_hierarchy_level == 3 else '2MB' if self.page_hierarchy_level == 2 else '4KB' if self.page_hierarchy_level == 1 else 'Unknown page size for level:' + self.page_hierarchy_level}
    {'page physical address': <30} {hex(self.page_physical_address)}
    ---
    {'bit': <4} {self.entry_present[0]: <10} {'entry present': <30} {self.entry_present[1]}
    {'bit': <4} {self.read_write[0]: <10} {'read/write access allowed': <30} {self.read_write[1]}
    {'bit': <4} {self.user_access_allowed[0]: <10} {'user access allowed': <30} {self.user_access_allowed[1]}
    {'bit': <4} {self.page_level_write_through[0]: <10} {'page level write through': <30} {self.page_level_write_through[1]}
    {'bit': <4} {self.page_level_cache_disabled[0]: <10} {'page level cache disabled': <30} {self.page_level_cache_disabled[1]}
    {'bit': <4} {self.entry_was_accessed[0]: <10} {'entry has been accessed': <30} {self.entry_was_accessed[1]}
    {"" if self.page_hierarchy_level == 1 else f"{'bit': <4} {self.page_size[0]: <10} {'page size': <30} {self.page_size[1]}"}
    {'bit': <4} {self.dirty[0]: <10} {'page dirty': <30} {self.dirty[1]}
    {'bit': <4} {self.global_translation[0]: <10} {'global translation': <30} {self.global_translation[1]}
    {'bit': <4} {self.hlat_restart_with_ordinary[0]: <10} {'restart to ordinary': <30} {self.hlat_restart_with_ordinary[1]}
    {'bit': <4} {self.pat[0]: <10} {'pat': <30} {self.pat[1]}
    {'bits': <4} {str(self.protection_key[0]): <10} {'protection key': <30} {self.protection_key[1]}
    {'bit': <4} {self.executed_disable[0]: <10} {'execute disable': <30} {self.executed_disable[1]}
"""
        else:
            return f"""\
level {self.page_hierarchy_level}:
    {'entry address': <30} {hex(self.address)}
    {'page entry binary data': <30} {hex(self.page_entry_binary_data)}
    {'next entry physical address': <30} {hex(self.next_entry_physical_address)}
    ---
    {'bit': <4} {self.entry_present[0]: <10} {'entry present': <30} {self.entry_present[1]}
    {'bit': <4} {self.read_write[0]: <10} {'read/write access allowed': <30} {self.read_write[1]}
    {'bit': <4} {self.user_access_allowed[0]: <10} {'user access allowed': <30} {self.user_access_allowed[1]}
    {'bit': <4} {self.page_level_write_through[0]: <10} {'page level write through': <30} {self.page_level_write_through[1]}
    {'bit': <4} {self.page_level_cache_disabled[0]: <10} {'page level cache disabled': <30} {self.page_level_cache_disabled[1]}
    {'bit': <4} {self.entry_was_accessed[0]: <10} {'entry has been accessed': <30} {self.entry_was_accessed[1]}
    {'bit': <4} {self.page_size[0]: <10} {'page size': <30} {self.page_size[1]}
    {'bit': <4} {self.hlat_restart_with_ordinary[0]: <10} {'restart to ordinary': <30} {self.hlat_restart_with_ordinary[1]}
    {'bit': <4} {self.executed_disable[0]: <10} {'execute disable': <30} {self.executed_disable[1]}
"""


class TranslateVM(gdb.Command):
    """Prints the entire paging structure used to translate a given virtual address.

Having an address space of the currently executed process translates the virtual address
and prints detailed information of all paging structure levels used for the transaltion.
Currently supported arch: x86"""

    def __init__(self):
        super(TranslateVM, self).__init__('translate-vm', gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        if utils.is_target_arch("x86"):
            vm_address = gdb.parse_and_eval(f'{arg}')
            cr3_data = gdb.parse_and_eval('$cr3')
            cr4 = gdb.parse_and_eval('$cr4')
            page_levels = 5 if cr4 & (1 << 12) else 4
            page_entry = Cr3(cr3_data, page_levels)
            while page_entry:
                gdb.write(page_entry.mk_string())
                page_entry = page_entry.next_entry(vm_address)
        else:
            gdb.GdbError("Virtual address translation is not"
                         "supported for this arch")


TranslateVM()
