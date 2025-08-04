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
 * Starting from version v3.0, the major version field should be interpreted as
 * a bit mask of features supported by the kernel's EFI stub:
 * - 0x1: initrd loading from the LINUX_EFI_INITRD_MEDIA_GUID device path,
 * - 0x2: initrd loading using the initrd= command line option, where the file
 *        may be specified using device path notation, and is not required to
 *        reside on the same volume as the loaded kernel image.
 *
 * The recommended way of loading and starting v1.0 or later kernels is to use
 * the LoadImage() and StartImage() EFI boot services, and expose the initrd
 * via the LINUX_EFI_INITRD_MEDIA_GUID device path.
 *
 * Versions older than v1.0 may support initrd loading via the image load
 * options (using initrd=, limited to the volume from which the kernel itself
 * was loaded), or only via arch specific means (bootparams, DT, etc).
 *
 * The minor version field must remain 0x0.
 * (https://lore.kernel.org/all/efd6f2d4-547c-1378-1faa-53c044dbd297@gmail.com/)
 */
#define LINUX_EFISTUB_MAJOR_VERSION		0x3
#define LINUX_EFISTUB_MINOR_VERSION		0x0

/*
 * LINUX_PE_MAGIC appears at offset 0x38 into the MS-DOS header of EFI bootable
 * Linux kernel images that target the architecture as specified by the PE/COFF
 * header machine type field.
 */
#define LINUX_PE_MAGIC	0x818223cd

#define IMAGE_DOS_SIGNATURE	0x5a4d /* "MZ" */

#define IMAGE_NT_SIGNATURE	0x00004550 /* "PE\0\0" */

#define IMAGE_ROM_OPTIONAL_HDR_MAGIC	0x0107 /* ROM image (for R3000/R4000/R10000/ALPHA), without MZ and PE\0\0 sign */
#define IMAGE_NT_OPTIONAL_HDR32_MAGIC	0x010b /* PE32 executable image */
#define IMAGE_NT_OPTIONAL_HDR64_MAGIC	0x020b /* PE32+ executable image */

/* machine type */
#define	IMAGE_FILE_MACHINE_UNKNOWN	0x0000 /* Unknown architecture */
#define	IMAGE_FILE_MACHINE_TARGET_HOST	0x0001 /* Interacts with the host and not a WOW64 guest (not for file image) */
#define	IMAGE_FILE_MACHINE_ALPHA_OLD	0x0183 /* DEC Alpha AXP 32-bit (old images) */
#define	IMAGE_FILE_MACHINE_ALPHA	0x0184 /* DEC Alpha AXP 32-bit */
#define	IMAGE_FILE_MACHINE_ALPHA64	0x0284 /* DEC Alpha AXP 64-bit (with 8kB page size) */
#define	IMAGE_FILE_MACHINE_AXP64	IMAGE_FILE_MACHINE_ALPHA64
#define	IMAGE_FILE_MACHINE_AM33		0x01d3 /* Matsushita AM33, now Panasonic MN103 */
#define	IMAGE_FILE_MACHINE_AMD64	0x8664 /* AMD64 (x64) */
#define	IMAGE_FILE_MACHINE_ARM		0x01c0 /* ARM Little-Endian (ARMv4) */
#define	IMAGE_FILE_MACHINE_THUMB	0x01c2 /* ARM Thumb Little-Endian (ARMv4T) */
#define	IMAGE_FILE_MACHINE_ARMNT	0x01c4 /* ARM Thumb-2 Little-Endian (ARMv7) */
#define	IMAGE_FILE_MACHINE_ARMV7	IMAGE_FILE_MACHINE_ARMNT
#define	IMAGE_FILE_MACHINE_ARM64	0xaa64 /* ARM64 Little-Endian (Classic ABI) */
#define	IMAGE_FILE_MACHINE_ARM64EC	0xa641 /* ARM64 Little-Endian (Emulation Compatible ABI for AMD64) */
#define	IMAGE_FILE_MACHINE_ARM64X	0xa64e /* ARM64 Little-Endian (fat binary with both Classic ABI and EC ABI code) */
#define	IMAGE_FILE_MACHINE_CEE		0xc0ee /* COM+ Execution Engine (CLR pure MSIL object files) */
#define	IMAGE_FILE_MACHINE_CEF		0x0cef /* Windows CE 3.0 Common Executable Format (CEF bytecode) */
#define	IMAGE_FILE_MACHINE_CHPE_X86	0x3a64 /* ARM64 Little-Endian (Compiled Hybrid PE ABI for I386) */
#define	IMAGE_FILE_MACHINE_HYBRID_X86	IMAGE_FILE_MACHINE_CHPE_X86
#define	IMAGE_FILE_MACHINE_EBC		0x0ebc /* EFI/UEFI Byte Code */
#define	IMAGE_FILE_MACHINE_I386		0x014c /* Intel 386 (x86) */
#define	IMAGE_FILE_MACHINE_I860		0x014d /* Intel 860 (N10) */
#define	IMAGE_FILE_MACHINE_IA64		0x0200 /* Intel IA-64 (with 8kB page size) */
#define	IMAGE_FILE_MACHINE_LOONGARCH32	0x6232 /* LoongArch 32-bit processor family */
#define	IMAGE_FILE_MACHINE_LOONGARCH64	0x6264 /* LoongArch 64-bit processor family */
#define	IMAGE_FILE_MACHINE_M32R		0x9041 /* Mitsubishi M32R 32-bit Little-Endian */
#define	IMAGE_FILE_MACHINE_M68K		0x0268 /* Motorola 68000 series */
#define	IMAGE_FILE_MACHINE_MIPS16	0x0266 /* MIPS III with MIPS16 ASE Little-Endian */
#define	IMAGE_FILE_MACHINE_MIPSFPU	0x0366 /* MIPS III with FPU Little-Endian */
#define	IMAGE_FILE_MACHINE_MIPSFPU16	0x0466 /* MIPS III with MIPS16 ASE and FPU Little-Endian */
#define	IMAGE_FILE_MACHINE_MPPC_601	0x0601 /* PowerPC 32-bit Big-Endian */
#define	IMAGE_FILE_MACHINE_OMNI		0xace1 /* Microsoft OMNI VM (omniprox.dll) */
#define	IMAGE_FILE_MACHINE_PARISC	0x0290 /* HP PA-RISC */
#define	IMAGE_FILE_MACHINE_POWERPC	0x01f0 /* PowerPC 32-bit Little-Endian */
#define	IMAGE_FILE_MACHINE_POWERPCFP	0x01f1 /* PowerPC 32-bit with FPU Little-Endian */
#define	IMAGE_FILE_MACHINE_POWERPCBE	0x01f2 /* PowerPC 64-bit Big-Endian */
#define	IMAGE_FILE_MACHINE_R3000	0x0162 /* MIPS I Little-Endian */
#define	IMAGE_FILE_MACHINE_R3000_BE	0x0160 /* MIPS I Big-Endian */
#define	IMAGE_FILE_MACHINE_R4000	0x0166 /* MIPS III Little-Endian (with 1kB or 4kB page size) */
#define	IMAGE_FILE_MACHINE_R10000	0x0168 /* MIPS IV Little-Endian */
#define	IMAGE_FILE_MACHINE_RISCV32	0x5032 /* RISC-V 32-bit address space */
#define	IMAGE_FILE_MACHINE_RISCV64	0x5064 /* RISC-V 64-bit address space */
#define	IMAGE_FILE_MACHINE_RISCV128	0x5128 /* RISC-V 128-bit address space */
#define	IMAGE_FILE_MACHINE_SH3		0x01a2 /* Hitachi SH-3 32-bit Little-Endian (with 1kB page size) */
#define	IMAGE_FILE_MACHINE_SH3DSP	0x01a3 /* Hitachi SH-3 DSP 32-bit (with 1kB page size) */
#define	IMAGE_FILE_MACHINE_SH3E		0x01a4 /* Hitachi SH-3E Little-Endian (with 1kB page size) */
#define	IMAGE_FILE_MACHINE_SH4		0x01a6 /* Hitachi SH-4 32-bit Little-Endian (with 1kB page size) */
#define	IMAGE_FILE_MACHINE_SH5		0x01a8 /* Hitachi SH-5 64-bit */
#define	IMAGE_FILE_MACHINE_TAHOE	0x07cc /* Intel EM machine */
#define	IMAGE_FILE_MACHINE_TRICORE	0x0520 /* Infineon AUDO 32-bit */
#define	IMAGE_FILE_MACHINE_WCEMIPSV2	0x0169 /* MIPS Windows CE v2 Little-Endian */

/* flags */
#define IMAGE_FILE_RELOCS_STRIPPED		0x0001 /* Relocation info stripped from file */
#define IMAGE_FILE_EXECUTABLE_IMAGE		0x0002 /* File is executable (i.e. no unresolved external references) */
#define IMAGE_FILE_LINE_NUMS_STRIPPED		0x0004 /* Line nunbers stripped from file */
#define IMAGE_FILE_LOCAL_SYMS_STRIPPED		0x0008 /* Local symbols stripped from file */
#define IMAGE_FILE_AGGRESSIVE_WS_TRIM		0x0010 /* Aggressively trim working set */
#define IMAGE_FILE_LARGE_ADDRESS_AWARE		0x0020 /* App can handle >2gb addresses (image can be loaded at address above 2GB) */
#define IMAGE_FILE_16BIT_MACHINE		0x0040 /* 16 bit word machine */
#define IMAGE_FILE_BYTES_REVERSED_LO		0x0080 /* Bytes of machine word are reversed (should be set together with IMAGE_FILE_BYTES_REVERSED_HI) */
#define IMAGE_FILE_32BIT_MACHINE		0x0100 /* 32 bit word machine */
#define IMAGE_FILE_DEBUG_STRIPPED		0x0200 /* Debugging info stripped from file in .DBG file */
#define IMAGE_FILE_REMOVABLE_RUN_FROM_SWAP	0x0400 /* If Image is on removable media, copy and run from the swap file */
#define IMAGE_FILE_NET_RUN_FROM_SWAP		0x0800 /* If Image is on Net, copy and run from the swap file */
#define IMAGE_FILE_SYSTEM			0x1000 /* System kernel-mode file (can't be loaded in user-mode) */
#define IMAGE_FILE_DLL				0x2000 /* File is a DLL */
#define IMAGE_FILE_UP_SYSTEM_ONLY		0x4000 /* File should only be run on a UP (uniprocessor) machine */
#define IMAGE_FILE_BYTES_REVERSED_HI		0x8000 /* Bytes of machine word are reversed (should be set together with IMAGE_FILE_BYTES_REVERSED_LO) */

/* subsys */
#define IMAGE_SUBSYSTEM_UNKNOWN				 0 /* Unknown subsystem */
#define IMAGE_SUBSYSTEM_NATIVE				 1 /* No subsystem required (NT device drivers and NT native system processes) */
#define IMAGE_SUBSYSTEM_WINDOWS_GUI			 2 /* Windows graphical user interface (GUI) subsystem */
#define IMAGE_SUBSYSTEM_WINDOWS_CUI			 3 /* Windows character-mode user interface (CUI) subsystem */
#define IMAGE_SUBSYSTEM_WINDOWS_OLD_CE_GUI		 4 /* Old Windows CE subsystem */
#define IMAGE_SUBSYSTEM_OS2_CUI				 5 /* OS/2 CUI subsystem */
#define IMAGE_SUBSYSTEM_RESERVED_6			 6
#define IMAGE_SUBSYSTEM_POSIX_CUI			 7 /* POSIX CUI subsystem */
#define IMAGE_SUBSYSTEM_MMOSA				 8 /* MMOSA/Native Win32E */
#define IMAGE_SUBSYSTEM_WINDOWS_CE_GUI			 9 /* Windows CE subsystem */
#define IMAGE_SUBSYSTEM_EFI_APPLICATION			10 /* Extensible Firmware Interface (EFI) application */
#define IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER		11 /* EFI driver with boot services */
#define IMAGE_SUBSYSTEM_EFI_RUNTIME_DRIVER		12 /* EFI driver with run-time services */
#define IMAGE_SUBSYSTEM_EFI_ROM_IMAGE			13 /* EFI ROM image */
#define IMAGE_SUBSYSTEM_XBOX				14 /* Xbox system */
#define IMAGE_SUBSYSTEM_RESERVED_15			15
#define IMAGE_SUBSYSTEM_WINDOWS_BOOT_APPLICATION	16 /* Windows Boot application */
#define IMAGE_SUBSYSTEM_XBOX_CODE_CATALOG		17 /* Xbox Code Catalog */

/* dll_flags */
#define IMAGE_LIBRARY_PROCESS_INIT			0x0001 /* DLL initialization function called just after process initialization */
#define IMAGE_LIBRARY_PROCESS_TERM			0x0002 /* DLL initialization function called just before process termination */
#define IMAGE_LIBRARY_THREAD_INIT			0x0004 /* DLL initialization function called just after thread initialization */
#define IMAGE_LIBRARY_THREAD_TERM			0x0008 /* DLL initialization function called just before thread initialization */
#define IMAGE_DLLCHARACTERISTICS_RESERVED_4		0x0010
#define IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA	0x0020 /* ASLR with 64 bit address space (image can be loaded at address above 4GB) */
#define IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE		0x0040 /* The DLL can be relocated at load time */
#define IMAGE_DLLCHARACTERISTICS_FORCE_INTEGRITY	0x0080 /* Code integrity checks are forced */
#define IMAGE_DLLCHARACTERISTICS_NX_COMPAT		0x0100 /* Image is compatible with data execution prevention */
#define IMAGE_DLLCHARACTERISTICS_NO_ISOLATION		0x0200 /* Image is isolation aware, but should not be isolated (prevents loading of manifest file) */
#define IMAGE_DLLCHARACTERISTICS_NO_SEH			0x0400 /* Image does not use SEH, no SE handler may reside in this image */
#define IMAGE_DLLCHARACTERISTICS_NO_BIND		0x0800 /* Do not bind the image */
#define IMAGE_DLLCHARACTERISTICS_X86_THUNK		0x1000 /* Image is a Wx86 Thunk DLL (for non-x86/risc DLL files) */
#define IMAGE_DLLCHARACTERISTICS_APPCONTAINER		0x1000 /* Image should execute in an AppContainer (for EXE Metro Apps in Windows 8) */
#define IMAGE_DLLCHARACTERISTICS_WDM_DRIVER		0x2000 /* A WDM driver */
#define IMAGE_DLLCHARACTERISTICS_GUARD_CF		0x4000 /* Image supports Control Flow Guard */
#define IMAGE_DLLCHARACTERISTICS_TERMINAL_SERVER_AWARE	0x8000 /* The image is terminal server (Remote Desktop Services) aware */

/* IMAGE_DEBUG_TYPE_EX_DLLCHARACTERISTICS flags */
#define IMAGE_DLLCHARACTERISTICS_EX_CET_COMPAT					0x0001 /* Image is Control-flow Enforcement Technology Shadow Stack compatible */
#define IMAGE_DLLCHARACTERISTICS_EX_CET_COMPAT_STRICT_MODE			0x0002 /* CET is enforced in strict mode */
#define IMAGE_DLLCHARACTERISTICS_EX_CET_SET_CONTEXT_IP_VALIDATION_RELAXED_MODE	0x0004 /* Relaxed mode for Context IP Validation under CET is allowed */
#define IMAGE_DLLCHARACTERISTICS_EX_CET_DYNAMIC_APIS_ALLOW_IN_PROC		0x0008 /* Use of dynamic APIs is restricted to processes only */
#define IMAGE_DLLCHARACTERISTICS_EX_CET_RESERVED_1				0x0010
#define IMAGE_DLLCHARACTERISTICS_EX_CET_RESERVED_2				0x0020
#define IMAGE_DLLCHARACTERISTICS_EX_FORWARD_CFI_COMPAT				0x0040 /* All branch targets in all image code sections are annotated with forward-edge control flow integrity guard instructions */
#define IMAGE_DLLCHARACTERISTICS_EX_HOTPATCH_COMPATIBLE				0x0080 /* Image can be modified while in use, hotpatch-compatible */

/* section_header flags */
#define IMAGE_SCN_SCALE_INDEX	0x00000001 /* address of tls index is scaled = multiplied by 4 (for .tls section on MIPS only) */
#define IMAGE_SCN_TYPE_NO_LOAD	0x00000002 /* reserved */
#define IMAGE_SCN_TYPE_GROUPED	0x00000004 /* obsolete (used for 16-bit offset code) */
#define IMAGE_SCN_TYPE_NO_PAD	0x00000008 /* .o only - don't pad - obsolete (same as IMAGE_SCN_ALIGN_1BYTES) */
#define IMAGE_SCN_TYPE_COPY	0x00000010 /* reserved */
#define IMAGE_SCN_CNT_CODE	0x00000020 /* .text */
#define IMAGE_SCN_CNT_INITIALIZED_DATA 0x00000040 /* .data */
#define IMAGE_SCN_CNT_UNINITIALIZED_DATA 0x00000080 /* .bss */
#define IMAGE_SCN_LNK_OTHER	0x00000100 /* .o only - other type than code, data or info */
#define IMAGE_SCN_LNK_INFO	0x00000200 /* .o only - .drectve comments */
#define IMAGE_SCN_LNK_OVERLAY	0x00000400 /* section contains overlay */
#define IMAGE_SCN_LNK_REMOVE	0x00000800 /* .o only - scn to be rm'd*/
#define IMAGE_SCN_LNK_COMDAT	0x00001000 /* .o only - COMDAT data */
#define IMAGE_SCN_RESERVED_13	0x00002000 /* spec omits this */
#define IMAGE_SCN_MEM_PROTECTED	0x00004000 /* section is memory protected (for M68K) */
#define IMAGE_SCN_NO_DEFER_SPEC_EXC 0x00004000 /* reset speculative exceptions handling bits in the TLB entries (for non-M68K) */
#define IMAGE_SCN_MEM_FARDATA	0x00008000 /* section uses FAR_EXTERNAL relocations (for M68K) */
#define IMAGE_SCN_GPREL		0x00008000 /* global pointer referenced data (for non-M68K) */
#define IMAGE_SCN_MEM_SYSHEAP	0x00010000 /* use system heap (for M68K) */
#define IMAGE_SCN_MEM_PURGEABLE	0x00020000 /* section can be released from RAM (for M68K) */
#define IMAGE_SCN_MEM_16BIT	0x00020000 /* section is 16-bit (for non-M68K where it makes sense: I386, THUMB, MIPS16, MIPSFPU16, ...) */
#define IMAGE_SCN_MEM_LOCKED	0x00040000 /* prevent the section from being moved (for M68K and .o I386) */
#define IMAGE_SCN_MEM_PRELOAD	0x00080000 /* section is preload to RAM (for M68K and .o I386) */
/* and here they just stuck a 1-byte integer in the middle of a bitfield */
#define IMAGE_SCN_ALIGN_1BYTES	0x00100000 /* .o only - it does what it says on the box */
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
#define IMAGE_SCN_ALIGN_RESERVED 0x00f00000
#define IMAGE_SCN_ALIGN_MASK	0x00f00000
#define IMAGE_SCN_LNK_NRELOC_OVFL 0x01000000 /* .o only - extended relocations */
#define IMAGE_SCN_MEM_DISCARDABLE 0x02000000 /* scn can be discarded */
#define IMAGE_SCN_MEM_NOT_CACHED 0x04000000 /* cannot be cached */
#define IMAGE_SCN_MEM_NOT_PAGED	0x08000000 /* not pageable */
#define IMAGE_SCN_MEM_SHARED	0x10000000 /* can be shared */
#define IMAGE_SCN_MEM_EXECUTE	0x20000000 /* can be executed as code */
#define IMAGE_SCN_MEM_READ	0x40000000 /* readable */
#define IMAGE_SCN_MEM_WRITE	0x80000000 /* writeable */

#define IMAGE_DEBUG_TYPE_UNKNOWN		 0 /* Unknown value, ignored by all tools */
#define IMAGE_DEBUG_TYPE_COFF			 1 /* COFF debugging information */
#define IMAGE_DEBUG_TYPE_CODEVIEW		 2 /* CodeView debugging information or Visual C++ Program Database debugging information */
#define IMAGE_DEBUG_TYPE_FPO			 3 /* Frame pointer omission (FPO) information */
#define IMAGE_DEBUG_TYPE_MISC			 4 /* Location of DBG file with CodeView debugging information */
#define IMAGE_DEBUG_TYPE_EXCEPTION		 5 /* Exception information, copy of .pdata section */
#define IMAGE_DEBUG_TYPE_FIXUP			 6 /* Fixup information */
#define IMAGE_DEBUG_TYPE_OMAP_TO_SRC		 7 /* The mapping from an RVA in image to an RVA in source image */
#define IMAGE_DEBUG_TYPE_OMAP_FROM_SRC		 8 /* The mapping from an RVA in source image to an RVA in image */
#define IMAGE_DEBUG_TYPE_BORLAND		 9 /* Borland debugging information */
#define IMAGE_DEBUG_TYPE_RESERVED10		10 /* Coldpath / Hotpatch debug information */
#define IMAGE_DEBUG_TYPE_CLSID			11 /* CLSID */
#define IMAGE_DEBUG_TYPE_VC_FEATURE		12 /* Visual C++ counts / statistics */
#define IMAGE_DEBUG_TYPE_POGO			13 /* COFF group information, data for profile-guided optimization */
#define IMAGE_DEBUG_TYPE_ILTCG			14 /* Incremental link-time code generation */
#define IMAGE_DEBUG_TYPE_MPX			15 /* Intel Memory Protection Extensions */
#define IMAGE_DEBUG_TYPE_REPRO			16 /* PE determinism or reproducibility */
#define IMAGE_DEBUG_TYPE_EMBEDDED_PORTABLE_PDB	17 /* Embedded Portable PDB debugging information */
#define IMAGE_DEBUG_TYPE_SPGO			18 /* Sample profile-guided optimization */
#define IMAGE_DEBUG_TYPE_PDBCHECKSUM		19 /* PDB Checksum */
#define IMAGE_DEBUG_TYPE_EX_DLLCHARACTERISTICS	20 /* Extended DLL characteristics bits */
#define IMAGE_DEBUG_TYPE_PERFMAP		21 /* Location of associated Ready To Run PerfMap file */

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
	uint32_t win32_version;	/* win32 version reported at runtime */
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
	uint32_t loader_flags;	/* loader flags */
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
	uint32_t win32_version;	/* win32 version reported at runtime */
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
	uint32_t loader_flags;	/* loader flags */
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
	struct data_dirent bound_imports;	/* bound import table */
	struct data_dirent import_addrs;	/* import address table */
	struct data_dirent delay_imports;	/* delay-load import table */
	struct data_dirent clr_runtime_hdr;	/* .cor (clr/.net executables) */
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
