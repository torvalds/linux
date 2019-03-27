/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1996-1997 John D. Polstra.
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

#ifndef _MACHINE_ELF_H_
#define	_MACHINE_ELF_H_ 1

#if defined(__i386__) || defined(_MACHINE_ELF_WANT_32BIT)

/*
 * ELF definitions for the i386 architecture.
 */

#include <sys/elf32.h>	/* Definitions common to all 32 bit architectures. */
#if defined(__ELF_WORD_SIZE) && __ELF_WORD_SIZE == 64
#include <sys/elf64.h>	/* Definitions common to all 64 bit architectures. */
#endif

#ifndef __ELF_WORD_SIZE
#define	__ELF_WORD_SIZE	32	/* Used by <sys/elf_generic.h> */
#endif

#include <sys/elf_generic.h>

#define	ELF_ARCH	EM_386

#define	ELF_MACHINE_OK(x) ((x) == EM_386 || (x) == EM_486)

/*
 * Auxiliary vector entries for passing information to the interpreter.
 *
 * The i386 supplement to the SVR4 ABI specification names this "auxv_t",
 * but POSIX lays claim to all symbols ending with "_t".
 */

typedef struct {	/* Auxiliary vector entry on initial stack */
	int	a_type;			/* Entry type. */
	union {
		long	a_val;		/* Integer value. */
		void	*a_ptr;		/* Address. */
		void	(*a_fcn)(void);	/* Function pointer (not used). */
	} a_un;
} Elf32_Auxinfo;

#if __ELF_WORD_SIZE == 64
/* Fake for amd64 loader support */
typedef struct {
	int fake;
} Elf64_Auxinfo;
#endif

__ElfType(Auxinfo);

/*
 * Relocation types.
 */

#define	R_386_COUNT	38	/* Count of defined relocation types. */

/* Define "machine" characteristics */
#define	ELF_TARG_CLASS	ELFCLASS32
#define	ELF_TARG_DATA	ELFDATA2LSB
#define	ELF_TARG_MACH	EM_386
#define	ELF_TARG_VER	1

#define	ET_DYN_LOAD_ADDR 0x01001000

#elif defined(__amd64__)

/*
 * ELF definitions for the AMD64 architecture.
 */

#ifndef __ELF_WORD_SIZE
#define	__ELF_WORD_SIZE	64	/* Used by <sys/elf_generic.h> */
#endif
#include <sys/elf32.h>	/* Definitions common to all 32 bit architectures. */
#include <sys/elf64.h>	/* Definitions common to all 64 bit architectures. */
#include <sys/elf_generic.h>

#define	ELF_ARCH	EM_X86_64
#define	ELF_ARCH32	EM_386

#define	ELF_MACHINE_OK(x) ((x) == EM_X86_64)

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
	} a_un;
} Elf32_Auxinfo;


typedef struct {	/* Auxiliary vector entry on initial stack */
	long	a_type;			/* Entry type. */
	union {
		long	a_val;		/* Integer value. */
		void	*a_ptr;		/* Address. */
		void	(*a_fcn)(void);	/* Function pointer (not used). */
	} a_un;
} Elf64_Auxinfo;

__ElfType(Auxinfo);

/*
 * Relocation types.
 */

#define	R_X86_64_COUNT	24	/* Count of defined relocation types. */

/* Define "machine" characteristics */
#if __ELF_WORD_SIZE == 32
#define ELF_TARG_CLASS  ELFCLASS32
#else
#define ELF_TARG_CLASS  ELFCLASS64
#endif
#define	ELF_TARG_DATA	ELFDATA2LSB
#define	ELF_TARG_MACH	EM_X86_64
#define	ELF_TARG_VER	1

#if __ELF_WORD_SIZE == 32
#define	ET_DYN_LOAD_ADDR 0x01001000
#else
#define	ET_DYN_LOAD_ADDR 0x01021000
#endif

#endif /* __i386__, __amd64__ */

#endif /* !_MACHINE_ELF_H_ */
