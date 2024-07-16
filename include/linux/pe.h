/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2011 Red Hat, Inc.
 * All rights reserved.
 *
 * Author(s): Peter Jones <pjones@redhat.com>
 */
#ifndef __LINUX_PE_H
#define __LINUX_PE_H

#include <linux/types.h>

/*
 * Linux EFI stub v1.0 adds the following functionality:
 * - Loading initrd from the LINUX_EFI_INITRD_MEDIA_GUID device path,
 * - Loading/starting the kernel from firmware that targets a different
 *   machine type, via the entrypoint exposed in the .compat PE/COFF section.
 *
 * The recommended way of loading and starting v1.0 or later kernels is to use
 * the LoadImage() and StartImage() EFI boot services, and expose the initrd
 * via the LINUX_EFI_INITRD_MEDIA_GUID device path.
 *
 * Versions older than v1.0 support initrd loading via the image load options
 * (using initrd=, limited to the volume from which the kernel itself was
 * loaded), or via arch specific means (bootparams, DT, etc).
 *
 * On x86, LoadImage() and StartImage() can be omitted if the EFI handover
 * protocol is implemented, which can be inferred from the version,
 * handover_offset and xloadflags fields in the bootparams structure.
 */
#define LINUX_EFISTUB_MAJOR_VERSION		0x1
#define LINUX_EFISTUB_MINOR_VERSION		0x0

#define MZ_MAGIC	0x5a4d	/* "MZ" */

#define PE_MAGIC		0x00004550	/* "PE\0\0" */
#define PE_OPT_MAGIC_PE32	0x010b
#define PE_OPT_MAGIC_PE32_ROM	0x0107
#define PE_OPT_MAGIC_PE32PLUS	0x020b

/* machine type */
#define	IMAGE_FILE_MACHINE_UNKNOWN	0x0000
#define	IMAGE_FILE_MACHINE_AM33		0x01d3
#define	IMAGE_FILE_MACHINE_AMD64	0x8664
#define	IMAGE_FILE_MACHINE_ARM		0x01c0
#define	IMAGE_FILE_MACHINE_ARMV7	0x01c4
#define	IMAGE_FILE_MACHINE_ARM64	0xaa64
#define	IMAGE_FILE_MACHINE_EBC		0x0ebc
#define	IMAGE_FILE_MACHINE_I386		0x014c
#define	IMAGE_FILE_MACHINE_IA64		0x0200
#define	IMAGE_FILE_MACHINE_M32R		0x9041
#define	IMAGE_FILE_MACHINE_MIPS16	0x0266
#define	IMAGE_FILE_MACHINE_MIPSFPU	0x0366
#define	IMAGE_FILE_MACHINE_MIPSFPU16	0x0466
#define	IMAGE_FILE_MACHINE_POWERPC	0x01f0
#define	IMAGE_FILE_MACHINE_POWERPCFP	0x01f1
#define	IMAGE_FILE_MACHINE_R4000	0x0166
#define	IMAGE_FILE_MACHINE_RISCV32	0x5032
#define	IMAGE_FILE_MACHINE_RISCV64	0x5064
#define	IMAGE_FILE_MACHINE_RISCV128	0x5128
#define	IMAGE_FILE_MACHINE_SH3		0x01a2
#define	IMAGE_FILE_MACHINE_SH3DSP	0x01a3
#define	IMAGE_FILE_MACHINE_SH3E		0x01a4
#define	IMAGE_FILE_MACHINE_SH4		0x01a6
#define	IMAGE_FILE_MACHINE_SH5		0x01a8
#define	IMAGE_FILE_MACHINE_THUMB	0x01c2
#define	IMAGE_FILE_MACHINE_WCEMIPSV2	0x0169
#define	IMAGE_FILE_MACHINE_LOONGARCH32	0x6232
#define	IMAGE_FILE_MACHINE_LOONGARCH64	0x6264

/* flags */
#define IMAGE_FILE_RELOCS_STRIPPED           0x0001
#define IMAGE_FILE_EXECUTABLE_IMAGE          0x0002
#define IMAGE_FILE_LINE_NUMS_STRIPPED        0x0004
#define IMAGE_FILE_LOCAL_SYMS_STRIPPED       0x0008
#define IMAGE_FILE_AGGRESSIVE_WS_TRIM        0x0010
#define IMAGE_FILE_LARGE_ADDRESS_AWARE       0x0020
#define IMAGE_FILE_16BIT_MACHINE             0x0040
#define IMAGE_FILE_BYTES_REVERSED_LO         0x0080
#define IMAGE_FILE_32BIT_MACHINE             0x0100
#define IMAGE_FILE_DEBUG_STRIPPED            0x0200
#define IMAGE_FILE_REMOVABLE_RUN_FROM_SWAP   0x0400
#define IMAGE_FILE_NET_RUN_FROM_SWAP         0x0800
#define IMAGE_FILE_SYSTEM                    0x1000
#define IMAGE_FILE_DLL                       0x2000
#define IMAGE_FILE_UP_SYSTEM_ONLY            0x4000
#define IMAGE_FILE_BYTES_REVERSED_HI         0x8000

#define IMAGE_FILE_OPT_ROM_MAGIC	0x107
#define IMAGE_FILE_OPT_PE32_MAGIC	0x10b
#define IMAGE_FILE_OPT_PE32_PLUS_MAGIC	0x20b

#define IMAGE_SUBSYSTEM_UNKNOWN			 0
#define IMAGE_SUBSYSTEM_NATIVE			 1
#define IMAGE_SUBSYSTEM_WINDOWS_GUI		 2
#define IMAGE_SUBSYSTEM_WINDOWS_CUI		 3
#define IMAGE_SUBSYSTEM_POSIX_CUI		 7
#define IMAGE_SUBSYSTEM_WINDOWS_CE_GUI		 9
#define IMAGE_SUBSYSTEM_EFI_APPLICATION		10
#define IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER	11
#define IMAGE_SUBSYSTEM_EFI_RUNTIME_DRIVER	12
#define IMAGE_SUBSYSTEM_EFI_ROM_IMAGE		13
#define IMAGE_SUBSYSTEM_XBOX			14

#define IMAGE_DLL_CHARACTERISTICS_DYNAMIC_BASE          0x0040
#define IMAGE_DLL_CHARACTERISTICS_FORCE_INTEGRITY       0x0080
#define IMAGE_DLL_CHARACTERISTICS_NX_COMPAT             0x0100
#define IMAGE_DLLCHARACTERISTICS_NO_ISOLATION           0x0200
#define IMAGE_DLLCHARACTERISTICS_NO_SEH                 0x0400
#define IMAGE_DLLCHARACTERISTICS_NO_BIND                0x0800
#define IMAGE_DLLCHARACTERISTICS_WDM_DRIVER             0x2000
#define IMAGE_DLLCHARACTERISTICS_TERMINAL_SERVER_AWARE  0x8000

/* they actually defined 0x00000000 as well, but I think we'll skip that one. */
#define IMAGE_SCN_RESERVED_0	0x00000001
#define IMAGE_SCN_RESERVED_1	0x00000002
#define IMAGE_SCN_RESERVED_2	0x00000004
#define IMAGE_SCN_TYPE_NO_PAD	0x00000008 /* don't pad - obsolete */
#define IMAGE_SCN_RESERVED_3	0x00000010
#define IMAGE_SCN_CNT_CODE	0x00000020 /* .text */
#define IMAGE_SCN_CNT_INITIALIZED_DATA 0x00000040 /* .data */
#define IMAGE_SCN_CNT_UNINITIALIZED_DATA 0x00000080 /* .bss */
#define IMAGE_SCN_LNK_OTHER	0x00000100 /* reserved */
#define IMAGE_SCN_LNK_INFO	0x00000200 /* .drectve comments */
#define IMAGE_SCN_RESERVED_4	0x00000400
#define IMAGE_SCN_LNK_REMOVE	0x00000800 /* .o only - scn to be rm'd*/
#define IMAGE_SCN_LNK_COMDAT	0x00001000 /* .o only - COMDAT data */
#define IMAGE_SCN_RESERVED_5	0x00002000 /* spec omits this */
#define IMAGE_SCN_RESERVED_6	0x00004000 /* spec omits this */
#define IMAGE_SCN_GPREL		0x00008000 /* global pointer referenced data */
/* spec lists 0x20000 twice, I suspect they meant 0x10000 for one of them */
#define IMAGE_SCN_MEM_PURGEABLE	0x00010000 /* reserved for "future" use */
#define IMAGE_SCN_16BIT		0x00020000 /* reserved for "future" use */
#define IMAGE_SCN_LOCKED	0x00040000 /* reserved for "future" use */
#define IMAGE_SCN_PRELOAD	0x00080000 /* reserved for "future" use */
/* and here they just stuck a 1-byte integer in the middle of a bitfield */
#define IMAGE_SCN_ALIGN_1BYTES	0x00100000 /* it does what it says on the box */
#define IMAGE_SCN_ALIGN_2BYTES	0x00200000
#define IMAGE_SCN_ALIGN_4BYTES	0x00300000
#define IMAGE_SCN_ALIGN_8BYTES	0x00400000
#define IMAGE_SCN_ALIGN_16BYTES	0x00500000
#define IMAGE_SCN_ALIGN_32BYTES	0x00600000
#define IMAGE_SCN_ALIGN_64BYTES	0x00700000
#define IMAGE_SCN_ALIGN_128BYTES 0x00800000
#define IMAGE_SCN_ALIGN_256BYTES 0x00900000
#define IMAGE_SCN_ALIGN_512BYTES 0x00a00000
#define IMAGE_SCN_ALIGN_1024BYTES 0x00b00000
#define IMAGE_SCN_ALIGN_2048BYTES 0x00c00000
#define IMAGE_SCN_ALIGN_4096BYTES 0x00d00000
#define IMAGE_SCN_ALIGN_8192BYTES 0x00e00000
#define IMAGE_SCN_LNK_NRELOC_OVFL 0x01000000 /* extended relocations */
#define IMAGE_SCN_MEM_DISCARDABLE 0x02000000 /* scn can be discarded */
#define IMAGE_SCN_MEM_NOT_CACHED 0x04000000 /* cannot be cached */
#define IMAGE_SCN_MEM_NOT_PAGED	0x08000000 /* not pageable */
#define IMAGE_SCN_MEM_SHARED	0x10000000 /* can be shared */
#define IMAGE_SCN_MEM_EXECUTE	0x20000000 /* can be executed as code */
#define IMAGE_SCN_MEM_READ	0x40000000 /* readable */
#define IMAGE_SCN_MEM_WRITE	0x80000000 /* writeable */

#define IMAGE_DEBUG_TYPE_CODEVIEW	2

#ifndef __ASSEMBLY__

struct mz_hdr {
	uint16_t magic;		/* MZ_MAGIC */
	uint16_t lbsize;	/* size of last used block */
	uint16_t blocks;	/* pages in file, 0x3 */
	uint16_t relocs;	/* relocations */
	uint16_t hdrsize;	/* header size in "paragraphs" */
	uint16_t min_extra_pps;	/* .bss */
	uint16_t max_extra_pps;	/* runtime limit for the arena size */
	uint16_t ss;		/* relative stack segment */
	uint16_t sp;		/* initial %sp register */
	uint16_t checksum;	/* word checksum */
	uint16_t ip;		/* initial %ip register */
	uint16_t cs;		/* initial %cs relative to load segment */
	uint16_t reloc_table_offset;	/* offset of the first relocation */
	uint16_t overlay_num;	/* overlay number.  set to 0. */
	uint16_t reserved0[4];	/* reserved */
	uint16_t oem_id;	/* oem identifier */
	uint16_t oem_info;	/* oem specific */
	uint16_t reserved1[10];	/* reserved */
	uint32_t peaddr;	/* address of pe header */
	char     message[];	/* message to print */
};

struct mz_reloc {
	uint16_t offset;
	uint16_t segment;
};

struct pe_hdr {
	uint32_t magic;		/* PE magic */
	uint16_t machine;	/* machine type */
	uint16_t sections;	/* number of sections */
	uint32_t timestamp;	/* time_t */
	uint32_t symbol_table;	/* symbol table offset */
	uint32_t symbols;	/* number of symbols */
	uint16_t opt_hdr_size;	/* size of optional header */
	uint16_t flags;		/* flags */
};

/* the fact that pe32 isn't padded where pe32+ is 64-bit means union won't
 * work right.  vomit. */
struct pe32_opt_hdr {
	/* "standard" header */
	uint16_t magic;		/* file type */
	uint8_t  ld_major;	/* linker major version */
	uint8_t  ld_minor;	/* linker minor version */
	uint32_t text_size;	/* size of text section(s) */
	uint32_t data_size;	/* size of data section(s) */
	uint32_t bss_size;	/* size of bss section(s) */
	uint32_t entry_point;	/* file offset of entry point */
	uint32_t code_base;	/* relative code addr in ram */
	uint32_t data_base;	/* relative data addr in ram */
	/* "windows" header */
	uint32_t image_base;	/* preferred load address */
	uint32_t section_align;	/* alignment in bytes */
	uint32_t file_align;	/* file alignment in bytes */
	uint16_t os_major;	/* major OS version */
	uint16_t os_minor;	/* minor OS version */
	uint16_t image_major;	/* major image version */
	uint16_t image_minor;	/* minor image version */
	uint16_t subsys_major;	/* major subsystem version */
	uint16_t subsys_minor;	/* minor subsystem version */
	uint32_t win32_version;	/* reserved, must be 0 */
	uint32_t image_size;	/* image size */
	uint32_t header_size;	/* header size rounded up to
				   file_align */
	uint32_t csum;		/* checksum */
	uint16_t subsys;	/* subsystem */
	uint16_t dll_flags;	/* more flags! */
	uint32_t stack_size_req;/* amt of stack requested */
	uint32_t stack_size;	/* amt of stack required */
	uint32_t heap_size_req;	/* amt of heap requested */
	uint32_t heap_size;	/* amt of heap required */
	uint32_t loader_flags;	/* reserved, must be 0 */
	uint32_t data_dirs;	/* number of data dir entries */
};

struct pe32plus_opt_hdr {
	uint16_t magic;		/* file type */
	uint8_t  ld_major;	/* linker major version */
	uint8_t  ld_minor;	/* linker minor version */
	uint32_t text_size;	/* size of text section(s) */
	uint32_t data_size;	/* size of data section(s) */
	uint32_t bss_size;	/* size of bss section(s) */
	uint32_t entry_point;	/* file offset of entry point */
	uint32_t code_base;	/* relative code addr in ram */
	/* "windows" header */
	uint64_t image_base;	/* preferred load address */
	uint32_t section_align;	/* alignment in bytes */
	uint32_t file_align;	/* file alignment in bytes */
	uint16_t os_major;	/* major OS version */
	uint16_t os_minor;	/* minor OS version */
	uint16_t image_major;	/* major image version */
	uint16_t image_minor;	/* minor image version */
	uint16_t subsys_major;	/* major subsystem version */
	uint16_t subsys_minor;	/* minor subsystem version */
	uint32_t win32_version;	/* reserved, must be 0 */
	uint32_t image_size;	/* image size */
	uint32_t header_size;	/* header size rounded up to
				   file_align */
	uint32_t csum;		/* checksum */
	uint16_t subsys;	/* subsystem */
	uint16_t dll_flags;	/* more flags! */
	uint64_t stack_size_req;/* amt of stack requested */
	uint64_t stack_size;	/* amt of stack required */
	uint64_t heap_size_req;	/* amt of heap requested */
	uint64_t heap_size;	/* amt of heap required */
	uint32_t loader_flags;	/* reserved, must be 0 */
	uint32_t data_dirs;	/* number of data dir entries */
};

struct data_dirent {
	uint32_t virtual_address;	/* relative to load address */
	uint32_t size;
};

struct data_directory {
	struct data_dirent exports;		/* .edata */
	struct data_dirent imports;		/* .idata */
	struct data_dirent resources;		/* .rsrc */
	struct data_dirent exceptions;		/* .pdata */
	struct data_dirent certs;		/* certs */
	struct data_dirent base_relocations;	/* .reloc */
	struct data_dirent debug;		/* .debug */
	struct data_dirent arch;		/* reservered */
	struct data_dirent global_ptr;		/* global pointer reg. Size=0 */
	struct data_dirent tls;			/* .tls */
	struct data_dirent load_config;		/* load configuration structure */
	struct data_dirent bound_imports;	/* no idea */
	struct data_dirent import_addrs;	/* import address table */
	struct data_dirent delay_imports;	/* delay-load import table */
	struct data_dirent clr_runtime_hdr;	/* .cor (object only) */
	struct data_dirent reserved;
};

struct section_header {
	char name[8];			/* name or "/12\0" string tbl offset */
	uint32_t virtual_size;		/* size of loaded section in ram */
	uint32_t virtual_address;	/* relative virtual address */
	uint32_t raw_data_size;		/* size of the section */
	uint32_t data_addr;		/* file pointer to first page of sec */
	uint32_t relocs;		/* file pointer to relocation entries */
	uint32_t line_numbers;		/* line numbers! */
	uint16_t num_relocs;		/* number of relocations */
	uint16_t num_lin_numbers;	/* srsly. */
	uint32_t flags;
};

enum x64_coff_reloc_type {
	IMAGE_REL_AMD64_ABSOLUTE = 0,
	IMAGE_REL_AMD64_ADDR64,
	IMAGE_REL_AMD64_ADDR32,
	IMAGE_REL_AMD64_ADDR32N,
	IMAGE_REL_AMD64_REL32,
	IMAGE_REL_AMD64_REL32_1,
	IMAGE_REL_AMD64_REL32_2,
	IMAGE_REL_AMD64_REL32_3,
	IMAGE_REL_AMD64_REL32_4,
	IMAGE_REL_AMD64_REL32_5,
	IMAGE_REL_AMD64_SECTION,
	IMAGE_REL_AMD64_SECREL,
	IMAGE_REL_AMD64_SECREL7,
	IMAGE_REL_AMD64_TOKEN,
	IMAGE_REL_AMD64_SREL32,
	IMAGE_REL_AMD64_PAIR,
	IMAGE_REL_AMD64_SSPAN32,
};

enum arm_coff_reloc_type {
	IMAGE_REL_ARM_ABSOLUTE,
	IMAGE_REL_ARM_ADDR32,
	IMAGE_REL_ARM_ADDR32N,
	IMAGE_REL_ARM_BRANCH2,
	IMAGE_REL_ARM_BRANCH1,
	IMAGE_REL_ARM_SECTION,
	IMAGE_REL_ARM_SECREL,
};

enum sh_coff_reloc_type {
	IMAGE_REL_SH3_ABSOLUTE,
	IMAGE_REL_SH3_DIRECT16,
	IMAGE_REL_SH3_DIRECT32,
	IMAGE_REL_SH3_DIRECT8,
	IMAGE_REL_SH3_DIRECT8_WORD,
	IMAGE_REL_SH3_DIRECT8_LONG,
	IMAGE_REL_SH3_DIRECT4,
	IMAGE_REL_SH3_DIRECT4_WORD,
	IMAGE_REL_SH3_DIRECT4_LONG,
	IMAGE_REL_SH3_PCREL8_WORD,
	IMAGE_REL_SH3_PCREL8_LONG,
	IMAGE_REL_SH3_PCREL12_WORD,
	IMAGE_REL_SH3_STARTOF_SECTION,
	IMAGE_REL_SH3_SIZEOF_SECTION,
	IMAGE_REL_SH3_SECTION,
	IMAGE_REL_SH3_SECREL,
	IMAGE_REL_SH3_DIRECT32_NB,
	IMAGE_REL_SH3_GPREL4_LONG,
	IMAGE_REL_SH3_TOKEN,
	IMAGE_REL_SHM_PCRELPT,
	IMAGE_REL_SHM_REFLO,
	IMAGE_REL_SHM_REFHALF,
	IMAGE_REL_SHM_RELLO,
	IMAGE_REL_SHM_RELHALF,
	IMAGE_REL_SHM_PAIR,
	IMAGE_REL_SHM_NOMODE,
};

enum ppc_coff_reloc_type {
	IMAGE_REL_PPC_ABSOLUTE,
	IMAGE_REL_PPC_ADDR64,
	IMAGE_REL_PPC_ADDR32,
	IMAGE_REL_PPC_ADDR24,
	IMAGE_REL_PPC_ADDR16,
	IMAGE_REL_PPC_ADDR14,
	IMAGE_REL_PPC_REL24,
	IMAGE_REL_PPC_REL14,
	IMAGE_REL_PPC_ADDR32N,
	IMAGE_REL_PPC_SECREL,
	IMAGE_REL_PPC_SECTION,
	IMAGE_REL_PPC_SECREL16,
	IMAGE_REL_PPC_REFHI,
	IMAGE_REL_PPC_REFLO,
	IMAGE_REL_PPC_PAIR,
	IMAGE_REL_PPC_SECRELLO,
	IMAGE_REL_PPC_GPREL,
	IMAGE_REL_PPC_TOKEN,
};

enum x86_coff_reloc_type {
	IMAGE_REL_I386_ABSOLUTE,
	IMAGE_REL_I386_DIR16,
	IMAGE_REL_I386_REL16,
	IMAGE_REL_I386_DIR32,
	IMAGE_REL_I386_DIR32NB,
	IMAGE_REL_I386_SEG12,
	IMAGE_REL_I386_SECTION,
	IMAGE_REL_I386_SECREL,
	IMAGE_REL_I386_TOKEN,
	IMAGE_REL_I386_SECREL7,
	IMAGE_REL_I386_REL32,
};

enum ia64_coff_reloc_type {
	IMAGE_REL_IA64_ABSOLUTE,
	IMAGE_REL_IA64_IMM14,
	IMAGE_REL_IA64_IMM22,
	IMAGE_REL_IA64_IMM64,
	IMAGE_REL_IA64_DIR32,
	IMAGE_REL_IA64_DIR64,
	IMAGE_REL_IA64_PCREL21B,
	IMAGE_REL_IA64_PCREL21M,
	IMAGE_REL_IA64_PCREL21F,
	IMAGE_REL_IA64_GPREL22,
	IMAGE_REL_IA64_LTOFF22,
	IMAGE_REL_IA64_SECTION,
	IMAGE_REL_IA64_SECREL22,
	IMAGE_REL_IA64_SECREL64I,
	IMAGE_REL_IA64_SECREL32,
	IMAGE_REL_IA64_DIR32NB,
	IMAGE_REL_IA64_SREL14,
	IMAGE_REL_IA64_SREL22,
	IMAGE_REL_IA64_SREL32,
	IMAGE_REL_IA64_UREL32,
	IMAGE_REL_IA64_PCREL60X,
	IMAGE_REL_IA64_PCREL60B,
	IMAGE_REL_IA64_PCREL60F,
	IMAGE_REL_IA64_PCREL60I,
	IMAGE_REL_IA64_PCREL60M,
	IMAGE_REL_IA64_IMMGPREL6,
	IMAGE_REL_IA64_TOKEN,
	IMAGE_REL_IA64_GPREL32,
	IMAGE_REL_IA64_ADDEND,
};

struct coff_reloc {
	uint32_t virtual_address;
	uint32_t symbol_table_index;
	union {
		enum x64_coff_reloc_type  x64_type;
		enum arm_coff_reloc_type  arm_type;
		enum sh_coff_reloc_type   sh_type;
		enum ppc_coff_reloc_type  ppc_type;
		enum x86_coff_reloc_type  x86_type;
		enum ia64_coff_reloc_type ia64_type;
		uint16_t data;
	};
};

/*
 * Definitions for the contents of the certs data block
 */
#define WIN_CERT_TYPE_PKCS_SIGNED_DATA	0x0002
#define WIN_CERT_TYPE_EFI_OKCS115	0x0EF0
#define WIN_CERT_TYPE_EFI_GUID		0x0EF1

#define WIN_CERT_REVISION_1_0	0x0100
#define WIN_CERT_REVISION_2_0	0x0200

struct win_certificate {
	uint32_t length;
	uint16_t revision;
	uint16_t cert_type;
};

#endif /* !__ASSEMBLY__ */

#endif /* __LINUX_PE_H */
