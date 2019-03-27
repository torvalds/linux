/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1996-1998 John D. Polstra.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _SYS_ELF64_H_
#define _SYS_ELF64_H_ 1

#include <sys/elf_common.h>

/*
 * ELF definitions common to all 64-bit architectures.
 */

typedef uint64_t	Elf64_Addr;
typedef uint16_t	Elf64_Half;
typedef uint64_t	Elf64_Off;
typedef int32_t		Elf64_Sword;
typedef int64_t		Elf64_Sxword;
typedef uint32_t	Elf64_Word;
typedef uint64_t	Elf64_Lword;
typedef uint64_t	Elf64_Xword;

/*
 * Types of dynamic symbol hash table bucket and chain elements.
 *
 * This is inconsistent among 64 bit architectures, so a machine dependent
 * typedef is required.
 */

typedef Elf64_Word	Elf64_Hashelt;

/* Non-standard class-dependent datatype used for abstraction. */
typedef Elf64_Xword	Elf64_Size;
typedef Elf64_Sxword	Elf64_Ssize;

/*
 * ELF header.
 */

typedef struct {
	unsigned char	e_ident[EI_NIDENT];	/* File identification. */
	Elf64_Half	e_type;		/* File type. */
	Elf64_Half	e_machine;	/* Machine architecture. */
	Elf64_Word	e_version;	/* ELF format version. */
	Elf64_Addr	e_entry;	/* Entry point. */
	Elf64_Off	e_phoff;	/* Program header file offset. */
	Elf64_Off	e_shoff;	/* Section header file offset. */
	Elf64_Word	e_flags;	/* Architecture-specific flags. */
	Elf64_Half	e_ehsize;	/* Size of ELF header in bytes. */
	Elf64_Half	e_phentsize;	/* Size of program header entry. */
	Elf64_Half	e_phnum;	/* Number of program header entries. */
	Elf64_Half	e_shentsize;	/* Size of section header entry. */
	Elf64_Half	e_shnum;	/* Number of section header entries. */
	Elf64_Half	e_shstrndx;	/* Section name strings section. */
} Elf64_Ehdr;

/*
 * Shared object information, found in SHT_MIPS_LIBLIST.
 */

typedef struct {
	Elf64_Word l_name;		/* The name of a shared object. */
	Elf64_Word l_time_stamp;	/* 64-bit timestamp. */
	Elf64_Word l_checksum;		/* Checksum of visible symbols, sizes. */
	Elf64_Word l_version;		/* Interface version string index. */
	Elf64_Word l_flags;		/* Flags (LL_*). */
} Elf64_Lib;

/*
 * Section header.
 */

typedef struct {
	Elf64_Word	sh_name;	/* Section name (index into the
					   section header string table). */
	Elf64_Word	sh_type;	/* Section type. */
	Elf64_Xword	sh_flags;	/* Section flags. */
	Elf64_Addr	sh_addr;	/* Address in memory image. */
	Elf64_Off	sh_offset;	/* Offset in file. */
	Elf64_Xword	sh_size;	/* Size in bytes. */
	Elf64_Word	sh_link;	/* Index of a related section. */
	Elf64_Word	sh_info;	/* Depends on section type. */
	Elf64_Xword	sh_addralign;	/* Alignment in bytes. */
	Elf64_Xword	sh_entsize;	/* Size of each entry in section. */
} Elf64_Shdr;

/*
 * Program header.
 */

typedef struct {
	Elf64_Word	p_type;		/* Entry type. */
	Elf64_Word	p_flags;	/* Access permission flags. */
	Elf64_Off	p_offset;	/* File offset of contents. */
	Elf64_Addr	p_vaddr;	/* Virtual address in memory image. */
	Elf64_Addr	p_paddr;	/* Physical address (not used). */
	Elf64_Xword	p_filesz;	/* Size of contents in file. */
	Elf64_Xword	p_memsz;	/* Size of contents in memory. */
	Elf64_Xword	p_align;	/* Alignment in memory and file. */
} Elf64_Phdr;

/*
 * Dynamic structure.  The ".dynamic" section contains an array of them.
 */

typedef struct {
	Elf64_Sxword	d_tag;		/* Entry type. */
	union {
		Elf64_Xword	d_val;	/* Integer value. */
		Elf64_Addr	d_ptr;	/* Address value. */
	} d_un;
} Elf64_Dyn;

/*
 * Relocation entries.
 */

/* Relocations that don't need an addend field. */
typedef struct {
	Elf64_Addr	r_offset;	/* Location to be relocated. */
	Elf64_Xword	r_info;		/* Relocation type and symbol index. */
} Elf64_Rel;

/* Relocations that need an addend field. */
typedef struct {
	Elf64_Addr	r_offset;	/* Location to be relocated. */
	Elf64_Xword	r_info;		/* Relocation type and symbol index. */
	Elf64_Sxword	r_addend;	/* Addend. */
} Elf64_Rela;

/* Macros for accessing the fields of r_info. */
#define	ELF64_R_SYM(info)	((info) >> 32)
#define	ELF64_R_TYPE(info)	((info) & 0xffffffffL)

/* Macro for constructing r_info from field values. */
#define	ELF64_R_INFO(sym, type)	(((sym) << 32) + ((type) & 0xffffffffL))

#define	ELF64_R_TYPE_DATA(info)	(((Elf64_Xword)(info)<<32)>>40)
#define	ELF64_R_TYPE_ID(info)	(((Elf64_Xword)(info)<<56)>>56)
#define	ELF64_R_TYPE_INFO(data, type)	\
				(((Elf64_Xword)(data)<<8)+(Elf64_Xword)(type))

/*
 *	Note entry header
 */
typedef Elf_Note Elf64_Nhdr;

/*
 *	Move entry
 */
typedef struct {
	Elf64_Lword	m_value;	/* symbol value */
	Elf64_Xword 	m_info;		/* size + index */
	Elf64_Xword	m_poffset;	/* symbol offset */
	Elf64_Half	m_repeat;	/* repeat count */
	Elf64_Half	m_stride;	/* stride info */
} Elf64_Move;

#define	ELF64_M_SYM(info)	((info)>>8)
#define	ELF64_M_SIZE(info)	((unsigned char)(info))
#define	ELF64_M_INFO(sym, size)	(((sym)<<8)+(unsigned char)(size))

/*
 *	Hardware/Software capabilities entry
 */
typedef struct {
	Elf64_Xword	c_tag;		/* how to interpret value */
	union {
		Elf64_Xword	c_val;
		Elf64_Addr	c_ptr;
	} c_un;
} Elf64_Cap;

/*
 * Symbol table entries.
 */

typedef struct {
	Elf64_Word	st_name;	/* String table index of name. */
	unsigned char	st_info;	/* Type and binding information. */
	unsigned char	st_other;	/* Reserved (not used). */
	Elf64_Half	st_shndx;	/* Section index of symbol. */
	Elf64_Addr	st_value;	/* Symbol value. */
	Elf64_Xword	st_size;	/* Size of associated object. */
} Elf64_Sym;

/* Macros for accessing the fields of st_info. */
#define	ELF64_ST_BIND(info)		((info) >> 4)
#define	ELF64_ST_TYPE(info)		((info) & 0xf)

/* Macro for constructing st_info from field values. */
#define	ELF64_ST_INFO(bind, type)	(((bind) << 4) + ((type) & 0xf))

/* Macro for accessing the fields of st_other. */
#define	ELF64_ST_VISIBILITY(oth)	((oth) & 0x3)

/* Structures used by Sun & GNU-style symbol versioning. */
typedef struct {
	Elf64_Half	vd_version;
	Elf64_Half	vd_flags;
	Elf64_Half	vd_ndx;
	Elf64_Half	vd_cnt;
	Elf64_Word	vd_hash;
	Elf64_Word	vd_aux;
	Elf64_Word	vd_next;
} Elf64_Verdef;

typedef struct {
	Elf64_Word	vda_name;
	Elf64_Word	vda_next;
} Elf64_Verdaux;

typedef struct {
	Elf64_Half	vn_version;
	Elf64_Half	vn_cnt;
	Elf64_Word	vn_file;
	Elf64_Word	vn_aux;
	Elf64_Word	vn_next;
} Elf64_Verneed;

typedef struct {
	Elf64_Word	vna_hash;
	Elf64_Half	vna_flags;
	Elf64_Half	vna_other;
	Elf64_Word	vna_name;
	Elf64_Word	vna_next;
} Elf64_Vernaux;

typedef Elf64_Half Elf64_Versym;

typedef struct {
	Elf64_Half	si_boundto;	/* direct bindings - symbol bound to */
	Elf64_Half	si_flags;	/* per symbol flags */
} Elf64_Syminfo;

typedef struct {
	Elf64_Word	ch_type;
	Elf64_Word	ch_reserved;
	Elf64_Xword	ch_size;
	Elf64_Xword	ch_addralign;
} Elf64_Chdr;

#endif /* !_SYS_ELF64_H_ */
