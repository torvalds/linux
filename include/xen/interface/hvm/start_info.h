/*
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Copyright (c) 2016, Citrix Systems, Inc.
 */

#ifndef __XEN_PUBLIC_ARCH_X86_HVM_START_INFO_H__
#define __XEN_PUBLIC_ARCH_X86_HVM_START_INFO_H__

/*
 * Start of day structure passed to PVH guests and to HVM guests in %ebx.
 *
 * NOTE: nothing will be loaded at physical address 0, so a 0 value in any
 * of the address fields should be treated as not present.
 *
 *  0 +----------------+
 *    | magic          | Contains the magic value XEN_HVM_START_MAGIC_VALUE
 *    |                | ("xEn3" with the 0x80 bit of the "E" set).
 *  4 +----------------+
 *    | version        | Version of this structure. Current version is 0. New
 *    |                | versions are guaranteed to be backwards-compatible.
 *  8 +----------------+
 *    | flags          | SIF_xxx flags.
 * 12 +----------------+
 *    | nr_modules     | Number of modules passed to the kernel.
 * 16 +----------------+
 *    | modlist_paddr  | Physical address of an array of modules
 *    |                | (layout of the structure below).
 * 24 +----------------+
 *    | cmdline_paddr  | Physical address of the command line,
 *    |                | a zero-terminated ASCII string.
 * 32 +----------------+
 *    | rsdp_paddr     | Physical address of the RSDP ACPI data structure.
 * 40 +----------------+
 *
 * The layout of each entry in the module structure is the following:
 *
 *  0 +----------------+
 *    | paddr          | Physical address of the module.
 *  8 +----------------+
 *    | size           | Size of the module in bytes.
 * 16 +----------------+
 *    | cmdline_paddr  | Physical address of the command line,
 *    |                | a zero-terminated ASCII string.
 * 24 +----------------+
 *    | reserved       |
 * 32 +----------------+
 *
 * The address and sizes are always a 64bit little endian unsigned integer.
 *
 * NB: Xen on x86 will always try to place all the data below the 4GiB
 * boundary.
 */
#define XEN_HVM_START_MAGIC_VALUE 0x336ec578

/*
 * C representation of the x86/HVM start info layout.
 *
 * The canonical definition of this layout is above, this is just a way to
 * represent the layout described there using C types.
 */
struct hvm_start_info {
    uint32_t magic;             /* Contains the magic value 0x336ec578       */
                                /* ("xEn3" with the 0x80 bit of the "E" set).*/
    uint32_t version;           /* Version of this structure.                */
    uint32_t flags;             /* SIF_xxx flags.                            */
    uint32_t nr_modules;        /* Number of modules passed to the kernel.   */
    uint64_t modlist_paddr;     /* Physical address of an array of           */
                                /* hvm_modlist_entry.                        */
    uint64_t cmdline_paddr;     /* Physical address of the command line.     */
    uint64_t rsdp_paddr;        /* Physical address of the RSDP ACPI data    */
                                /* structure.                                */
};

struct hvm_modlist_entry {
    uint64_t paddr;             /* Physical address of the module.           */
    uint64_t size;              /* Size of the module in bytes.              */
    uint64_t cmdline_paddr;     /* Physical address of the command line.     */
    uint64_t reserved;
};

#endif /* __XEN_PUBLIC_ARCH_X86_HVM_START_INFO_H__ */
