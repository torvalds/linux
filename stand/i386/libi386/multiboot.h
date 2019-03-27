/* multiboot.h - Multiboot header file. */
/* Copyright (C) 1999,2003,2007,2008,2009  Free Software Foundation, Inc.
*
*  Permission is hereby granted, free of charge, to any person obtaining a copy
*  of this software and associated documentation files (the "Software"), to
*  deal in the Software without restriction, including without limitation the
*  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
*  sell copies of the Software, and to permit persons to whom the Software is
*  furnished to do so, subject to the following conditions:
*
*  The above copyright notice and this permission notice shall be included in
*  all copies or substantial portions of the Software.
*
*  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL ANY
*  DEVELOPER OR DISTRIBUTOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
*  WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
*  IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*
* $FreeBSD$
*/

#ifndef MULTIBOOT_HEADER
#define MULTIBOOT_HEADER 1

/* How many bytes from the start of the file we search for the header. */
#define MULTIBOOT_SEARCH                        8192

/* The magic field should contain this. */
#define MULTIBOOT_HEADER_MAGIC                  0x1BADB002

/* This should be in %eax. */
#define MULTIBOOT_BOOTLOADER_MAGIC              0x2BADB002

/* The bits in the required part of flags field we don't support. */
#define MULTIBOOT_UNSUPPORTED                   0x0000fffc

/* Alignment of multiboot modules. */
#define MULTIBOOT_MOD_ALIGN                     0x00001000

/* Alignment of the multiboot info structure. */
#define MULTIBOOT_INFO_ALIGN                    0x00000004

/* Flags set in the 'flags' member of the multiboot header. */

/* Align all boot modules on i386 page (4KB) boundaries. */
#define MULTIBOOT_PAGE_ALIGN                    0x00000001

/* Must pass memory information to OS. */
#define MULTIBOOT_MEMORY_INFO                   0x00000002

/* Must pass video information to OS. */
#define MULTIBOOT_VIDEO_MODE                    0x00000004

/* This flag indicates the use of the address fields in the header. */
#define MULTIBOOT_AOUT_KLUDGE                   0x00010000

/* Flags to be set in the 'flags' member of the multiboot info structure. */

/* is there basic lower/upper memory information? */
#define MULTIBOOT_INFO_MEMORY                   0x00000001
/* is there a boot device set? */
#define MULTIBOOT_INFO_BOOTDEV                  0x00000002
/* is the command-line defined? */
#define MULTIBOOT_INFO_CMDLINE                  0x00000004
/* are there modules to do something with? */
#define MULTIBOOT_INFO_MODS                     0x00000008

/* These next two are mutually exclusive */

/* is there a symbol table loaded? */
#define MULTIBOOT_INFO_AOUT_SYMS                0x00000010
/* is there an ELF section header table? */
#define MULTIBOOT_INFO_ELF_SHDR                 0X00000020

/* is there a full memory map? */
#define MULTIBOOT_INFO_MEM_MAP                  0x00000040

/* Is there drive info? */
#define MULTIBOOT_INFO_DRIVE_INFO               0x00000080

/* Is there a config table? */
#define MULTIBOOT_INFO_CONFIG_TABLE             0x00000100

/* Is there a boot loader name? */
#define MULTIBOOT_INFO_BOOT_LOADER_NAME         0x00000200

/* Is there a APM table? */
#define MULTIBOOT_INFO_APM_TABLE                0x00000400

/* Is there video information? */
#define MULTIBOOT_INFO_VIDEO_INFO               0x00000800

#ifndef ASM_FILE

typedef unsigned short          multiboot_uint16_t;
typedef unsigned int            multiboot_uint32_t;
typedef unsigned long long      multiboot_uint64_t;

struct multiboot_header
{
/* Must be MULTIBOOT_MAGIC - see above. */
	multiboot_uint32_t magic;

/* Feature flags. */
	multiboot_uint32_t flags;

/* The above fields plus this one must equal 0 mod 2^32. */
	multiboot_uint32_t checksum;

/* These are only valid if MULTIBOOT_AOUT_KLUDGE is set. */
	multiboot_uint32_t header_addr;
	multiboot_uint32_t load_addr;
	multiboot_uint32_t load_end_addr;
	multiboot_uint32_t bss_end_addr;
	multiboot_uint32_t entry_addr;

/* These are only valid if MULTIBOOT_VIDEO_MODE is set. */
	multiboot_uint32_t mode_type;
	multiboot_uint32_t width;
	multiboot_uint32_t height;
	multiboot_uint32_t depth;
};

/* The symbol table for a.out. */
struct multiboot_aout_symbol_table
{
	multiboot_uint32_t tabsize;
	multiboot_uint32_t strsize;
	multiboot_uint32_t addr;
	multiboot_uint32_t reserved;
};
typedef struct multiboot_aout_symbol_table multiboot_aout_symbol_table_t;

/* The section header table for ELF. */
struct multiboot_elf_section_header_table
{
	multiboot_uint32_t num;
	multiboot_uint32_t size;
	multiboot_uint32_t addr;
	multiboot_uint32_t shndx;
};
typedef struct multiboot_elf_section_header_table multiboot_elf_section_header_table_t;

struct multiboot_info
{
/* Multiboot info version number */
	multiboot_uint32_t flags;

/* Available memory from BIOS */
	multiboot_uint32_t mem_lower;
	multiboot_uint32_t mem_upper;

/* "root" partition */
	multiboot_uint32_t boot_device;

/* Kernel command line */
	multiboot_uint32_t cmdline;

/* Boot-Module list */
	multiboot_uint32_t mods_count;
	multiboot_uint32_t mods_addr;

	union
	{
		multiboot_aout_symbol_table_t aout_sym;
		multiboot_elf_section_header_table_t elf_sec;
	} u;

/* Memory Mapping buffer */
	multiboot_uint32_t mmap_length;
	multiboot_uint32_t mmap_addr;

/* Drive Info buffer */
	multiboot_uint32_t drives_length;
	multiboot_uint32_t drives_addr;

/* ROM configuration table */
	multiboot_uint32_t config_table;

/* Boot Loader Name */
	multiboot_uint32_t boot_loader_name;

/* APM table */
	multiboot_uint32_t apm_table;

/* Video */
	multiboot_uint32_t vbe_control_info;
	multiboot_uint32_t vbe_mode_info;
	multiboot_uint16_t vbe_mode;
	multiboot_uint16_t vbe_interface_seg;
	multiboot_uint16_t vbe_interface_off;
	multiboot_uint16_t vbe_interface_len;
};
typedef struct multiboot_info multiboot_info_t;

struct multiboot_mmap_entry
{
	multiboot_uint32_t size;
	multiboot_uint64_t addr;
	multiboot_uint64_t len;
#define MULTIBOOT_MEMORY_AVAILABLE              1
#define MULTIBOOT_MEMORY_RESERVED               2
	multiboot_uint32_t type;
} __attribute__((packed));
typedef struct multiboot_mmap_entry multiboot_memory_map_t;

struct multiboot_mod_list
{
/* the memory used goes from bytes 'mod_start' to 'mod_end-1' inclusive */
	multiboot_uint32_t mod_start;
	multiboot_uint32_t mod_end;

/* Module command line */
	multiboot_uint32_t cmdline;

/* padding to take it to 16 bytes (must be zero) */
	multiboot_uint32_t pad;
};
typedef struct multiboot_mod_list multiboot_module_t;

#endif /* ! ASM_FILE */

#endif /* ! MULTIBOOT_HEADER */
