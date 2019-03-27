/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2003
 *	Bill Paul <wpaul@windriver.com>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _PE_VAR_H_
#define	_PE_VAR_H_

/*
 *  Image Format
 */

#define	IMAGE_DOS_SIGNATURE			0x5A4D      /* MZ */
#define	IMAGE_OS2_SIGNATURE			0x454E      /* NE */
#define	IMAGE_OS2_SIGNATURE_LE			0x454C      /* LE */
#define	IMAGE_VXD_SIGNATURE			0x454C      /* LE */
#define	IMAGE_NT_SIGNATURE			0x00004550  /* PE00 */

/*
 * All PE files have one of these, just so if you attempt to
 * run them, they'll print out a message telling you they can
 * only be run in Windows.
 */

struct image_dos_header {
	uint16_t	idh_magic;	/* Magic number */
	uint16_t	idh_cblp;	/* Bytes on last page of file */
	uint16_t	idh_cp;		/* Pages in file */
	uint16_t	idh_crlc;	/* Relocations */
	uint16_t	idh_cparhdr;	/* Size of header in paragraphs */
	uint16_t	idh_minalloc;	/* Minimum extra paragraphs needed */
	uint16_t	idh_maxalloc;	/* Maximum extra paragraphs needed */
	uint16_t	idh_ss;		/* Initial (relative) SS value */
	uint16_t	idh_sp;		/* Initial SP value */
	uint16_t	idh_csum;	/* Checksum */
	uint16_t	idh_ip;		/* Initial IP value */
	uint16_t	idh_cs;		/* Initial (relative) CS value */
	uint16_t	idh_lfarlc;	/* File address of relocation table */
	uint16_t	idh_ovno;	/* Overlay number */
	uint16_t	idh_rsvd1[4];	/* Reserved words */
	uint16_t	idh_oemid;	/* OEM identifier (for idh_oeminfo) */
	uint16_t	idh_oeminfo;	/* OEM information; oemid specific */
	uint16_t	idh_rsvd2[10];	/* Reserved words */
	uint32_t	idh_lfanew;	/* File address of new exe header */
};

typedef struct image_dos_header image_dos_header;

/*
 * File header format.
 */

struct image_file_header {
	uint16_t	ifh_machine;		/* Machine type */
	uint16_t	ifh_numsections;	/* # of sections */
	uint32_t	ifh_timestamp;		/* Date/time stamp */
	uint32_t	ifh_symtblptr;		/* Offset to symbol table */
	uint32_t	ifh_numsyms;		/* # of symbols */
	uint16_t	ifh_optionalhdrlen;	/* Size of optional header */
	uint16_t	ifh_characteristics;	/* Characteristics */
};

typedef struct image_file_header image_file_header;

/* Machine types */

#define	IMAGE_FILE_MACHINE_UNKNOWN      0
#define	IMAGE_FILE_MACHINE_I860         0x014d
#define	IMAGE_FILE_MACHINE_I386         0x014c
#define	IMAGE_FILE_MACHINE_R3000        0x0162
#define	IMAGE_FILE_MACHINE_R4000        0x0166
#define	IMAGE_FILE_MACHINE_R10000       0x0168
#define	IMAGE_FILE_MACHINE_WCEMIPSV2    0x0169
#define	IMAGE_FILE_MACHINE_ALPHA        0x0184
#define	IMAGE_FILE_MACHINE_SH3          0x01a2
#define	IMAGE_FILE_MACHINE_SH3DSP       0x01a3
#define	IMAGE_FILE_MACHINE_SH3E         0x01a4
#define	IMAGE_FILE_MACHINE_SH4          0x01a6
#define	IMAGE_FILE_MACHINE_SH5          0x01a8
#define	IMAGE_FILE_MACHINE_ARM          0x01c0
#define	IMAGE_FILE_MACHINE_THUMB        0x01c2
#define	IMAGE_FILE_MACHINE_AM33         0x01d3
#define	IMAGE_FILE_MACHINE_POWERPC      0x01f0
#define	IMAGE_FILE_MACHINE_POWERPCFP    0x01f1
#define	IMAGE_FILE_MACHINE_MIPS16       0x0266
#define	IMAGE_FILE_MACHINE_ALPHA64      0x0284
#define	IMAGE_FILE_MACHINE_MIPSFPU      0x0366
#define	IMAGE_FILE_MACHINE_MIPSFPU16    0x0466
#define	IMAGE_FILE_MACHINE_AXP64        IMAGE_FILE_MACHINE_ALPHA64
#define	IMAGE_FILE_MACHINE_TRICORE      0x0520
#define	IMAGE_FILE_MACHINE_CEF          0x0cef
#define	IMAGE_FILE_MACHINE_EBC          0x0ebc
#define	IMAGE_FILE_MACHINE_AMD64        0x8664
#define	IMAGE_FILE_MACHINE_M32R         0x9041
#define	IMAGE_FILE_MACHINE_CEE          0xc0ee

/* Characteristics */

#define	IMAGE_FILE_RELOCS_STRIPPED      0x0001 /* No relocation info */
#define	IMAGE_FILE_EXECUTABLE_IMAGE     0x0002
#define	IMAGE_FILE_LINE_NUMS_STRIPPED   0x0004
#define	IMAGE_FILE_LOCAL_SYMS_STRIPPED  0x0008
#define	IMAGE_FILE_AGGRESIVE_WS_TRIM    0x0010
#define	IMAGE_FILE_LARGE_ADDRESS_AWARE  0x0020
#define	IMAGE_FILE_16BIT_MACHINE        0x0040
#define	IMAGE_FILE_BYTES_REVERSED_LO    0x0080
#define	IMAGE_FILE_32BIT_MACHINE        0x0100
#define	IMAGE_FILE_DEBUG_STRIPPED       0x0200
#define	IMAGE_FILE_REMOVABLE_RUN_FROM_SWAP      0x0400
#define	IMAGE_FILE_NET_RUN_FROM_SWAP    0x0800
#define	IMAGE_FILE_SYSTEM               0x1000
#define	IMAGE_FILE_DLL                  0x2000
#define	IMAGE_FILE_UP_SYSTEM_ONLY       0x4000
#define	IMAGE_FILE_BYTES_REVERSED_HI    0x8000

#define	IMAGE_SIZEOF_FILE_HEADER             20

/*
 * Directory format.
 */

struct image_data_directory {
	uint32_t		idd_vaddr;	/* virtual address */
	uint32_t		idd_size;	/* size */
};

typedef struct image_data_directory image_data_directory;

#define	IMAGE_DIRECTORY_ENTRIES_MAX    16

/*
 * Optional header format.
 */

struct image_optional_header {

	/* Standard fields */

	uint16_t	ioh_magic;
	uint8_t		ioh_linkerver_major;
	uint8_t		ioh_linkerver_minor;
	uint32_t	ioh_codesize;
	uint32_t	ioh_datasize;
	uint32_t	ioh_bsssize;
	uint32_t	ioh_entryaddr;
	uint32_t	ioh_codebaseaddr;
#ifndef __amd64__
	uint32_t	ioh_databaseaddr;
#endif

	/* NT-specific fields */

	uintptr_t	ioh_imagebase;
	uint32_t	ioh_sectalign;
	uint32_t	ioh_filealign;
	uint16_t	ioh_osver_major;
	uint16_t	ioh_osver_minor;
	uint16_t	ioh_imagever_major;
	uint16_t	ioh_imagever_minor;
	uint16_t	ioh_subsys_major;
	uint16_t	ioh_subsys_minor;
	uint32_t	ioh_win32ver;
	uint32_t	ioh_imagesize;
	uint32_t	ioh_headersize;
	uint32_t	ioh_csum;
	uint16_t	ioh_subsys;
	uint16_t	ioh_dll_characteristics;
	uintptr_t	ioh_stackreservesize;
	uintptr_t	ioh_stackcommitsize;
	uintptr_t	ioh_heapreservesize;
	uintptr_t	ioh_heapcommitsize;
	uint16_t	ioh_loaderflags;
	uint32_t	ioh_rva_size_cnt;
	image_data_directory	ioh_datadir[IMAGE_DIRECTORY_ENTRIES_MAX];
};

typedef struct image_optional_header image_optional_header;

struct image_nt_header {
	uint32_t		inh_signature;
	image_file_header	inh_filehdr;
	image_optional_header	inh_optionalhdr;
};

typedef struct image_nt_header image_nt_header;

#define	IMAGE_SIZEOF_NT_HEADER(nthdr)					\
	(offsetof(image_nt_header, inh_optionalhdr) +			\
	  ((image_nt_header *)(nthdr))->inh_filehdr.ifh_optionalhdrlen)

/* Directory Entries */

#define	IMAGE_DIRECTORY_ENTRY_EXPORT         0   /* Export Directory */
#define	IMAGE_DIRECTORY_ENTRY_IMPORT         1   /* Import Directory */
#define	IMAGE_DIRECTORY_ENTRY_RESOURCE       2   /* Resource Directory */
#define	IMAGE_DIRECTORY_ENTRY_EXCEPTION      3   /* Exception Directory */
#define	IMAGE_DIRECTORY_ENTRY_SECURITY       4   /* Security Directory */
#define	IMAGE_DIRECTORY_ENTRY_BASERELOC      5   /* Base Relocation Table */
#define	IMAGE_DIRECTORY_ENTRY_DEBUG          6   /* Debug Directory */
#define	IMAGE_DIRECTORY_ENTRY_COPYRIGHT      7   /* Description String */
#define	IMAGE_DIRECTORY_ENTRY_GLOBALPTR      8   /* Machine Value (MIPS GP) */
#define	IMAGE_DIRECTORY_ENTRY_TLS            9   /* TLS Directory */
#define	IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG   10   /* Load Configuration Directory */
#define	IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT  11   /* Bound Import Directory in headers */
#define	IMAGE_DIRECTORY_ENTRY_IAT           12   /* Import Address Table */
#define	IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT      13
#define	IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR    14

/* Resource types */

#define	RT_CURSOR	1
#define	RT_BITMAP	2
#define	RT_ICON		3
#define	RT_MENU		4
#define	RT_DIALOG	5
#define	RT_STRING	6
#define	RT_FONTDIR	7
#define	RT_FONT		8
#define	RT_ACCELERATOR	9
#define	RT_RCDATA	10
#define	RT_MESSAGETABLE	11
#define	RT_GROUP_CURSOR	12
#define	RT_GROUP_ICON	14
#define	RT_VERSION	16
#define	RT_DLGINCLUDE	17
#define	RT_PLUGPLAY	19
#define	RT_VXD		20
#define	RT_ANICURSOR	21
#define	RT_ANIICON	22
#define	RT_HTML		23

/*
 * Section header format.
 */

#define	IMAGE_SHORT_NAME_LEN			8

struct image_section_header {
	uint8_t		ish_name[IMAGE_SHORT_NAME_LEN];
	union {
		uint32_t	ish_paddr;
		uint32_t	ish_vsize;
	} ish_misc;
	uint32_t	ish_vaddr;
	uint32_t	ish_rawdatasize;
	uint32_t	ish_rawdataaddr;
	uint32_t	ish_relocaddr;
	uint32_t	ish_linenumaddr;
	uint16_t	ish_numrelocs;
	uint16_t	ish_numlinenums;
	uint32_t	ish_characteristics;
};

typedef struct image_section_header image_section_header;

#define	IMAGE_SIZEOF_SECTION_HEADER          40

#define	IMAGE_FIRST_SECTION(nthdr)					\
	((image_section_header *)((vm_offset_t)(nthdr) +		\
	  offsetof(image_nt_header, inh_optionalhdr) +			\
	  ((image_nt_header *)(nthdr))->inh_filehdr.ifh_optionalhdrlen))

/*
 * Import format
 */

struct image_import_by_name {
	uint16_t	iibn_hint;
	uint8_t		iibn_name[1];
};

#define	IMAGE_ORDINAL_FLAG 0x80000000
#define	IMAGE_ORDINAL(Ordinal) (Ordinal & 0xffff)

struct image_import_descriptor {
	uint32_t	iid_import_name_table_addr;
	uint32_t	iid_timestamp;
	uint32_t	iid_forwardchain;
	uint32_t	iid_nameaddr;
	uint32_t	iid_import_address_table_addr;
};

typedef struct image_import_descriptor image_import_descriptor;

struct image_base_reloc {
	uint32_t	ibr_vaddr;
	uint32_t	ibr_blocksize;
	uint16_t	ibr_rel[1];
};

typedef struct image_base_reloc image_base_reloc;

#define	IMR_RELTYPE(x)		((x >> 12) & 0xF)
#define	IMR_RELOFFSET(x)	(x & 0xFFF)

/* generic relocation types */
#define	IMAGE_REL_BASED_ABSOLUTE		0
#define	IMAGE_REL_BASED_HIGH			1
#define	IMAGE_REL_BASED_LOW			2
#define	IMAGE_REL_BASED_HIGHLOW			3
#define	IMAGE_REL_BASED_HIGHADJ			4
#define	IMAGE_REL_BASED_MIPS_JMPADDR		5
#define	IMAGE_REL_BASED_SECTION			6
#define	IMAGE_REL_BASED_REL			7
#define	IMAGE_REL_BASED_MIPS_JMPADDR16		9
#define	IMAGE_REL_BASED_DIR64			10
#define	IMAGE_REL_BASED_HIGH3ADJ		11

struct image_resource_directory_entry {
	uint32_t		irde_name;
	uint32_t		irde_dataoff;
};

typedef struct image_resource_directory_entry image_resource_directory_entry;

#define	RESOURCE_NAME_STR	0x80000000
#define	RESOURCE_DIR_FLAG	0x80000000

struct image_resource_directory {
	uint32_t		ird_characteristics;
	uint32_t		ird_timestamp;
	uint16_t		ird_majorver;
	uint16_t		ird_minorver;
	uint16_t		ird_named_entries;
	uint16_t		ird_id_entries;
#ifdef notdef
	image_resource_directory_entry	ird_entries[1];
#endif
};

typedef struct image_resource_directory image_resource_directory;

struct image_resource_directory_string {
	uint16_t		irds_len;
	char			irds_name[1];
};

typedef struct image_resource_directory_string image_resource_directory_string;

struct image_resource_directory_string_u {
	uint16_t		irds_len;
	char			irds_name[1];
};

typedef struct image_resource_directory_string_u
	image_resource_directory_string_u;

struct image_resource_data_entry {
	uint32_t		irde_offset;
	uint32_t		irde_size;
	uint32_t		irde_codepage;
	uint32_t		irde_rsvd;
};

typedef struct image_resource_data_entry image_resource_data_entry;

struct message_resource_data {
	uint32_t		mrd_numblocks;
#ifdef notdef
	message_resource_block	mrd_blocks[1];
#endif
};

typedef struct message_resource_data message_resource_data;

struct message_resource_block {
	uint32_t		mrb_lowid;
	uint32_t		mrb_highid;
	uint32_t		mrb_entryoff;
};

typedef struct message_resource_block message_resource_block;

struct message_resource_entry {
	uint16_t		mre_len;
	uint16_t		mre_flags;
	char			mre_text[];
};

typedef struct message_resource_entry message_resource_entry;

#define	MESSAGE_RESOURCE_UNICODE	0x0001

struct image_patch_table {
	char		*ipt_name;
	void		(*ipt_func)(void);
	void		(*ipt_wrap)(void);
	int		ipt_argcnt;
	int		ipt_ftype;
};

typedef struct image_patch_table image_patch_table;

/*
 * AMD64 support. Microsoft uses a different calling convention
 * than everyone else on the amd64 platform. Sadly, gcc has no
 * built-in support for it (yet).
 *
 * The three major differences we're concerned with are:
 *
 * - The first 4 register-sized arguments are passed in the
 *   %rcx, %rdx, %r8 and %r9 registers, and the rest are pushed
 *   onto the stack. (The ELF ABI uses 6 registers, not 4).
 *
 * - The caller must reserve space on the stack for the 4
 *   register arguments in case the callee has to spill them.
 *
 * - The stack myst be 16-byte aligned by the time the callee
 *   executes. A call instruction implicitly pushes an 8 byte
 *   return address onto the stack. We have to make sure that
 *   the amount of space we consume, plus the return address,
 *   is a multiple of 16 bytes in size. This means that in
 *   some cases, we may need to chew up an extra 8 bytes on
 *   the stack that will be unused.
 *
 * On the bright side, Microsoft seems to be using just the one
 * calling convention for all functions on amd64, unlike x86 where
 * they use a mix of _stdcall, _fastcall and _cdecl.
 */

#ifdef __amd64__

extern uint64_t x86_64_call1(void *, uint64_t);
extern uint64_t x86_64_call2(void *, uint64_t, uint64_t);
extern uint64_t x86_64_call3(void *, uint64_t, uint64_t, uint64_t);
extern uint64_t x86_64_call4(void *, uint64_t, uint64_t, uint64_t, uint64_t);
extern uint64_t x86_64_call5(void *, uint64_t, uint64_t, uint64_t, uint64_t,
	uint64_t);
extern uint64_t x86_64_call6(void *, uint64_t, uint64_t, uint64_t, uint64_t,
	uint64_t, uint64_t);

uint64_t _x86_64_call1(void *, uint64_t);
uint64_t _x86_64_call2(void *, uint64_t, uint64_t);
uint64_t _x86_64_call3(void *, uint64_t, uint64_t, uint64_t);
uint64_t _x86_64_call4(void *, uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t _x86_64_call5(void *, uint64_t, uint64_t, uint64_t, uint64_t,
    uint64_t);
uint64_t _x86_64_call6(void *, uint64_t, uint64_t, uint64_t, uint64_t,
    uint64_t, uint64_t);

#define	MSCALL1(fn, a)						\
	_x86_64_call1((fn), (uint64_t)(a))
#define	MSCALL2(fn, a, b)					\
	_x86_64_call2((fn), (uint64_t)(a), (uint64_t)(b))
#define	MSCALL3(fn, a, b, c)					\
	_x86_64_call3((fn), (uint64_t)(a), (uint64_t)(b),		\
	(uint64_t)(c))
#define	MSCALL4(fn, a, b, c, d)					\
	_x86_64_call4((fn), (uint64_t)(a), (uint64_t)(b),		\
	(uint64_t)(c), (uint64_t)(d))
#define	MSCALL5(fn, a, b, c, d, e)				\
	_x86_64_call5((fn), (uint64_t)(a), (uint64_t)(b),		\
	(uint64_t)(c), (uint64_t)(d), (uint64_t)(e))
#define	MSCALL6(fn, a, b, c, d, e, f)				\
	_x86_64_call6((fn), (uint64_t)(a), (uint64_t)(b),		\
	(uint64_t)(c), (uint64_t)(d), (uint64_t)(e), (uint64_t)(f))

#endif /* __amd64__ */

#ifdef __i386__

extern uint32_t x86_stdcall_call(void *, int, ...);

#define	MSCALL1(fn, a)		x86_stdcall_call(fn, 1, (a))
#define	MSCALL2(fn, a, b)	x86_stdcall_call(fn, 2, (a), (b))
#define	MSCALL3(fn, a, b, c)	x86_stdcall_call(fn, 3, (a), (b), (c))
#define	MSCALL4(fn, a, b, c, d)	x86_stdcall_call(fn, 4, (a), (b), (c), (d))
#define	MSCALL5(fn, a, b, c, d, e)	\
		x86_stdcall_call(fn, 5, (a), (b), (c), (d), (e))
#define	MSCALL6(fn, a, b, c, d, e, f)	\
		x86_stdcall_call(fn, 6, (a), (b), (c), (d), (e), (f))

#endif /* __i386__ */


#define	FUNC void(*)(void)

#ifdef __i386__
#define	IMPORT_SFUNC(x, y)	{ #x, (FUNC)x, NULL, y, WINDRV_WRAP_STDCALL }
#define	IMPORT_SFUNC_MAP(x, y, z)	\
				{ #x, (FUNC)y, NULL, z, WINDRV_WRAP_STDCALL }
#define	IMPORT_FFUNC(x, y)	{ #x, (FUNC)x, NULL, y, WINDRV_WRAP_FASTCALL }
#define	IMPORT_FFUNC_MAP(x, y, z)	\
				{ #x, (FUNC)y, NULL, z, WINDRV_WRAP_FASTCALL }
#define	IMPORT_RFUNC(x, y)	{ #x, (FUNC)x, NULL, y, WINDRV_WRAP_REGPARM }
#define	IMPORT_RFUNC_MAP(x, y, z)	\
				{ #x, (FUNC)y, NULL, z, WINDRV_WRAP_REGPARM }
#define	IMPORT_CFUNC(x, y)	{ #x, (FUNC)x, NULL, y, WINDRV_WRAP_CDECL }
#define	IMPORT_CFUNC_MAP(x, y, z)	\
				{ #x, (FUNC)y, NULL, z, WINDRV_WRAP_CDECL }
#endif /* __i386__ */

#ifdef __amd64__
#define	IMPORT_SFUNC(x, y)	{ #x, (FUNC)x, NULL, y, WINDRV_WRAP_AMD64 }
#define	IMPORT_SFUNC_MAP(x, y, z)	\
				{ #x, (FUNC)y, NULL, z, WINDRV_WRAP_AMD64 }
#define	IMPORT_FFUNC(x, y)	{ #x, (FUNC)x, NULL, y, WINDRV_WRAP_AMD64 }
#define	IMPORT_FFUNC_MAP(x, y, z)	\
				{ #x, (FUNC)y, NULL, z, WINDRV_WRAP_AMD64 }
#define	IMPORT_RFUNC(x, y)	{ #x, (FUNC)x, NULL, y, WINDRV_WRAP_AMD64 }
#define	IMPORT_RFUNC_MAP(x, y, z)	\
				{ #x, (FUNC)y, NULL, z, WINDRV_WRAP_AMD64 }
#define	IMPORT_CFUNC(x, y)	{ #x, (FUNC)x, NULL, y, WINDRV_WRAP_AMD64 }
#define	IMPORT_CFUNC_MAP(x, y, z)	\
				{ #x, (FUNC)y, NULL, z, WINDRV_WRAP_AMD64 }
#endif /* __amd64__ */

__BEGIN_DECLS
extern int pe_get_dos_header(vm_offset_t, image_dos_header *);
extern int pe_is_nt_image(vm_offset_t);
extern int pe_get_optional_header(vm_offset_t, image_optional_header *);
extern int pe_get_file_header(vm_offset_t, image_file_header *);
extern int pe_get_section_header(vm_offset_t, image_section_header *);
extern int pe_numsections(vm_offset_t);
extern vm_offset_t pe_imagebase(vm_offset_t);
extern vm_offset_t pe_directory_offset(vm_offset_t, uint32_t);
extern vm_offset_t pe_translate_addr (vm_offset_t, vm_offset_t);
extern int pe_get_section(vm_offset_t, image_section_header *, const char *);
extern int pe_relocate(vm_offset_t);
extern int pe_get_import_descriptor(vm_offset_t, image_import_descriptor *, char *);
extern int pe_patch_imports(vm_offset_t, char *, image_patch_table *);
extern int pe_get_messagetable(vm_offset_t, message_resource_data **);
extern int pe_get_message(vm_offset_t, uint32_t, char **, int *, uint16_t *);
__END_DECLS

#endif /* _PE_VAR_H_ */
