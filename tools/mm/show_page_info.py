#!/usr/bin/env drgn
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2025 Ye Liu <liuye@kylinos.cn>

import argparse
import sys
from drgn import Object, FaultError, PlatformFlags, cast
from drgn.helpers.linux import find_task, follow_page, page_size
from drgn.helpers.linux.mm import (
    decode_page_flags, page_to_pfn, page_to_phys, page_to_virt, vma_find,
    PageSlab, PageCompound, PageHead, PageTail, compound_head, compound_order, compound_nr
)
from drgn.helpers.linux.cgroup import cgroup_name, cgroup_path

DESC = """
This is a drgn script to show the page state.
For more info on drgn, visit https://github.com/osandov/drgn.
"""

def format_page_data(page):
    """
    Format raw page data into a readable hex dump with "RAW:" prefix.

    :param page: drgn.Object instance representing the page.
    :return: Formatted string of memory contents.
    """
    try:
        address = page.value_()
        size = prog.type("struct page").size

        if prog.platform.flags & PlatformFlags.IS_64_BIT:
            word_size = 8
        else:
            word_size = 4
        num_words = size // word_size

        values = []
        for i in range(num_words):
            word_address = address + i * word_size
            word = prog.read_word(word_address)
            values.append(f"{word:0{word_size * 2}x}")

        lines = [f"RAW: {' '.join(values[i:i + 4])}" for i in range(0, len(values), 4)]

        return "\n".join(lines)

    except FaultError as e:
        return f"Error reading memory: {e}"
    except Exception as e:
        return f"Unexpected error: {e}"

def get_memcg_info(page):
    """Retrieve memory cgroup information for a page."""
    try:
        MEMCG_DATA_OBJEXTS = prog.constant("MEMCG_DATA_OBJEXTS").value_()
        MEMCG_DATA_KMEM = prog.constant("MEMCG_DATA_KMEM").value_()
        mask = prog.constant('__NR_MEMCG_DATA_FLAGS').value_() - 1
        memcg_data = page.memcg_data.read_()
        if memcg_data & MEMCG_DATA_OBJEXTS:
            slabobj_ext = cast("struct slabobj_ext *", memcg_data & ~mask)
            memcg = slabobj_ext.objcg.memcg.value_()
        elif memcg_data & MEMCG_DATA_KMEM:
            objcg = cast("struct obj_cgroup *", memcg_data & ~mask)
            memcg = objcg.memcg.value_()
        else:
            memcg = cast("struct mem_cgroup *", memcg_data & ~mask)

        if memcg.value_() == 0:
            return "none", "/sys/fs/cgroup/memory/"
        cgrp = memcg.css.cgroup
        return cgroup_name(cgrp).decode(), f"/sys/fs/cgroup/memory{cgroup_path(cgrp).decode()}"
    except FaultError as e:
        return "unknown", f"Error retrieving memcg info: {e}"
    except Exception as e:
        return "unknown", f"Unexpected error: {e}"

def show_page_state(page, addr, mm, pid, task):
    """Display detailed information about a page."""
    try:
        print(f'PID: {pid} Comm: {task.comm.string_().decode()} mm: {hex(mm)}')
        try:
            print(format_page_data(page))
        except FaultError as e:
            print(f"Error reading page data: {e}")
        fields = {
            "Page Address": hex(page.value_()),
            "Page Flags": decode_page_flags(page),
            "Page Size": prog["PAGE_SIZE"].value_(),
            "Page PFN": hex(page_to_pfn(page).value_()),
            "Page Physical": hex(page_to_phys(page).value_()),
            "Page Virtual": hex(page_to_virt(page).value_()),
            "Page Refcount": page._refcount.counter.value_(),
            "Page Mapcount": page._mapcount.counter.value_(),
            "Page Index": hex(page.__folio_index.value_()),
            "Page Memcg Data": hex(page.memcg_data.value_()),
        }

        memcg_name, memcg_path = get_memcg_info(page)
        fields["Memcg Name"] = memcg_name
        fields["Memcg Path"] = memcg_path
        fields["Page Mapping"] = hex(page.mapping.value_())
        fields["Page Anon/File"] = "Anon" if page.mapping.value_() & 0x1 else "File"

        try:
            vma = vma_find(mm, addr)
            fields["Page VMA"] = hex(vma.value_())
            fields["VMA Start"] = hex(vma.vm_start.value_())
            fields["VMA End"] = hex(vma.vm_end.value_())
        except FaultError as e:
            fields["Page VMA"] = "Unavailable"
            fields["VMA Start"] = "Unavailable"
            fields["VMA End"] = "Unavailable"
            print(f"Error retrieving VMA information: {e}")

        # Calculate the maximum field name length for alignment
        max_field_len = max(len(field) for field in fields)

        # Print aligned fields
        for field, value in fields.items():
            print(f"{field}:".ljust(max_field_len + 2) + f"{value}")

        # Additional information about the page
        if PageSlab(page):
            print("This page belongs to the slab allocator.")

        if PageCompound(page):
            print("This page is part of a compound page.")
            if PageHead(page):
                print("This page is the head page of a compound page.")
            if PageTail(page):
                print("This page is the tail page of a compound page.")
            print(f"{'Head Page:'.ljust(max_field_len + 2)}{hex(compound_head(page).value_())}")
            print(f"{'Compound Order:'.ljust(max_field_len + 2)}{compound_order(page).value_()}")
            print(f"{'Number of Pages:'.ljust(max_field_len + 2)}{compound_nr(page).value_()}")
        else:
            print("This page is not part of a compound page.")
    except FaultError as e:
        print(f"Error accessing page state: {e}")
    except Exception as e:
        print(f"Unexpected error: {e}")

def main():
    """Main function to parse arguments and display page state."""
    parser = argparse.ArgumentParser(description=DESC, formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument('pid', metavar='PID', type=int, help='Target process ID (PID)')
    parser.add_argument('vaddr', metavar='VADDR', type=str, help='Target virtual address in hexadecimal format (e.g., 0x7fff1234abcd)')
    args = parser.parse_args()

    try:
        vaddr = int(args.vaddr, 16)
    except ValueError:
        sys.exit(f"Error: Invalid virtual address format: {args.vaddr}")

    try:
        task = find_task(args.pid)
        mm = task.mm
        page = follow_page(mm, vaddr)

        if page:
            show_page_state(page, vaddr, mm, args.pid, task)
        else:
            sys.exit(f"Address {hex(vaddr)} is not mapped.")
    except FaultError as e:
        sys.exit(f"Error accessing task or memory: {e}")
    except Exception as e:
        sys.exit(f"Unexpected error: {e}")

if __name__ == "__main__":
    main()
