/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD AND BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2013 M. Warner Losh.
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

/*-
 * Copyright (c) 2013 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * See below starting with the line with $NetBSD...$ for code this applies to.
 */

#ifndef	__MIPS_ELF_H
#define	__MIPS_ELF_H

/* FreeBSD specific bits - derived from FreeBSD specific files and changes to old elf.h */

/*
 * Define __ELF_WORD_SIZE based on the ABI, if not defined yet. This sets
 * the proper defaults when we're not trying to do 32-bit on 64-bit systems.
 * We include both 32 and 64 bit versions so we can support multiple ABIs.
 */
#ifndef __ELF_WORD_SIZE
#if defined(__mips_n64)
#define __ELF_WORD_SIZE 64
#else
#define __ELF_WORD_SIZE 32
#endif
#endif
#include <sys/elf32.h>
#include <sys/elf64.h>
#include <sys/elf_generic.h>

#define ELF_ARCH	EM_MIPS
#define ELF_ARCH32	EM_MIPS

#define	ELF_MACHINE_OK(x)	((x) == ELF_ARCH)

/* Define "machine" characteristics */
#if __ELF_WORD_SIZE == 32
#define	ELF_TARG_CLASS	ELFCLASS32
#else
#define	ELF_TARG_CLASS	ELFCLASS64
#endif
#ifdef __MIPSEB__
#define	ELF_TARG_DATA	ELFDATA2MSB
#else
#define ELF_TARG_DATA	ELFDATA2LSB
#endif
#define	ELF_TARG_MACH	EM_MIPS
#define	ELF_TARG_VER	1

/*
 * Auxiliary vector entries for passing information to the interpreter.
 *
 * The i386 supplement to the SVR4 ABI specification names this "auxv_t",
 * but POSIX lays claim to all symbols ending with "_t".
 */
typedef struct {	/* Auxiliary vector entry on initial stack */
	int	a_type;			/* Entry type. */
	union {
		int	a_val;		/* Integer value. */
		void	*a_ptr;		/* Address. */
		void	(*a_fcn)(void); /* Function pointer (not used). */
	} a_un;
} Elf32_Auxinfo;

typedef struct {	/* Auxiliary vector entry on initial stack */
	long	a_type;			/* Entry type. */
	union {
		long	a_val;		/* Integer value. */
		void	*a_ptr;		/* Address. */
		void	(*a_fcn)(void); /* Function pointer (not used). */
	} a_un;
} Elf64_Auxinfo;

__ElfType(Auxinfo);

#define	ET_DYN_LOAD_ADDR 0x0120000

/*
 * Constant to mark start of symtab/strtab saved by trampoline
 */
#define	SYMTAB_MAGIC	0x64656267

/* from NetBSD's sys/mips/include/elf_machdep.h $NetBSD: elf_machdep.h,v 1.18 2013/05/23 21:39:49 christos Exp $ */

/* mips relocs. */

#define	R_MIPS_NONE		0
#define	R_MIPS_16		1
#define	R_MIPS_32		2
#define	R_MIPS_REL32		3
#define	R_MIPS_REL		R_MIPS_REL32
#define	R_MIPS_26		4
#define	R_MIPS_HI16		5	/* high 16 bits of symbol value */
#define	R_MIPS_LO16		6	/* low 16 bits of symbol value */
#define	R_MIPS_GPREL16		7	/* GP-relative reference  */
#define	R_MIPS_LITERAL		8	/* Reference to literal section  */
#define	R_MIPS_GOT16		9	/* Reference to global offset table */
#define	R_MIPS_GOT		R_MIPS_GOT16
#define	R_MIPS_PC16		10	/* 16 bit PC relative reference */
#define	R_MIPS_CALL16 		11	/* 16 bit call thru glbl offset tbl */
#define	R_MIPS_CALL		R_MIPS_CALL16
#define	R_MIPS_GPREL32		12

/* 13, 14, 15 are not defined at this point. */
#define	R_MIPS_UNUSED1		13
#define	R_MIPS_UNUSED2		14
#define	R_MIPS_UNUSED3		15

/*
 * The remaining relocs are apparently part of the 64-bit Irix ELF ABI.
 */
#define	R_MIPS_SHIFT5		16
#define	R_MIPS_SHIFT6		17

#define	R_MIPS_64		18
#define	R_MIPS_GOT_DISP		19
#define	R_MIPS_GOT_PAGE		20
#define	R_MIPS_GOT_OFST		21
#define	R_MIPS_GOT_HI16		22
#define	R_MIPS_GOT_LO16		23
#define	R_MIPS_SUB		24
#define	R_MIPS_INSERT_A		25
#define	R_MIPS_INSERT_B		26
#define	R_MIPS_DELETE		27
#define	R_MIPS_HIGHER		28
#define	R_MIPS_HIGHEST		29
#define	R_MIPS_CALL_HI16	30
#define	R_MIPS_CALL_LO16	31
#define	R_MIPS_SCN_DISP		32
#define	R_MIPS_REL16		33
#define	R_MIPS_ADD_IMMEDIATE	34
#define	R_MIPS_PJUMP		35
#define	R_MIPS_RELGOT		36
#define	R_MIPS_JALR		37
/* TLS relocations */

#define	R_MIPS_TLS_DTPMOD32	38	/* Module number 32 bit */
#define	R_MIPS_TLS_DTPREL32	39	/* Module-relative offset 32 bit */
#define	R_MIPS_TLS_DTPMOD64	40	/* Module number 64 bit */
#define	R_MIPS_TLS_DTPREL64	41	/* Module-relative offset 64 bit */
#define	R_MIPS_TLS_GD		42	/* 16 bit GOT offset for GD */
#define	R_MIPS_TLS_LDM		43	/* 16 bit GOT offset for LDM */
#define	R_MIPS_TLS_DTPREL_HI16	44	/* Module-relative offset, high 16 bits */
#define	R_MIPS_TLS_DTPREL_LO16	45	/* Module-relative offset, low 16 bits */
#define	R_MIPS_TLS_GOTTPREL	46	/* 16 bit GOT offset for IE */
#define	R_MIPS_TLS_TPREL32	47	/* TP-relative offset, 32 bit */
#define	R_MIPS_TLS_TPREL64	48	/* TP-relative offset, 64 bit */
#define	R_MIPS_TLS_TPREL_HI16	49	/* TP-relative offset, high 16 bits */
#define	R_MIPS_TLS_TPREL_LO16	50	/* TP-relative offset, low 16 bits */

#define	R_MIPS_max		51

#define	R_TYPE(name)		__CONCAT(R_MIPS_,name)

#define	R_MIPS16_min		100
#define	R_MIPS16_26		100
#define	R_MIPS16_GPREL		101
#define	R_MIPS16_GOT16		102
#define	R_MIPS16_CALL16		103
#define	R_MIPS16_HI16		104
#define	R_MIPS16_LO16		105
#define	R_MIPS16_max		106

#define	R_MIPS_COPY		126
#define	R_MIPS_JUMP_SLOT	127

#endif /* __MIPS_ELF_H */
