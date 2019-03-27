/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 David E. O'Brien
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

/*
 * EABI ELF definitions for the StrongARM architecture.
 * See "ARM ELF", document no. `SWS ESPC 0003 A-08' for details.
 */

#include <sys/elf32.h>	/* Definitions common to all 32 bit architectures. */

#define	__ELF_WORD_SIZE	32	/* Used by <sys/elf_generic.h> */
#include <sys/elf_generic.h>

typedef struct {        /* Auxiliary vector entry on initial stack */
	int     a_type;                 /* Entry type. */
	union {
		long    a_val;          /* Integer value. */
		void    *a_ptr;         /* Address. */
		void    (*a_fcn)(void); /* Function pointer (not used). */
	} a_un;
} Elf32_Auxinfo;

__ElfType(Auxinfo);

#define	ELF_ARCH	EM_ARM

#define	ELF_MACHINE_OK(x) ((x) == EM_ARM)

/*
 * Relocation types.
 */

#define	R_ARM_COUNT	33	/* Count of defined relocation types. */

/* Define "machine" characteristics */
#define	ELF_TARG_CLASS	ELFCLASS32
#ifdef __ARMEB__
#define	ELF_TARG_DATA	ELFDATA2MSB
#else
#define	ELF_TARG_DATA	ELFDATA2LSB
#endif
#define	ELF_TARG_MACH	EM_ARM
#define	ELF_TARG_VER	1

/* Defines specific for arm headers */
#define	EF_ARM_EABI_VERSION(x) (((x) & EF_ARM_EABIMASK) >> 24)
#define	EF_ARM_EABI_VERSION_UNKNOWN 0
#define	EF_ARM_EABI_FREEBSD_MIN 4

/*
 * Magic number for the elf trampoline, chosen wisely to be an immediate
 * value.
 */
#define	MAGIC_TRAMP_NUMBER	0x5c000003

#define	ET_DYN_LOAD_ADDR	0x500000

/* Flags passed in AT_HWCAP. */
#define	HWCAP_SWP		0x00000001	/* Unsupported, never set.    */
#define	HWCAP_HALF		0x00000002	/* Always set.                */
#define	HWCAP_THUMB		0x00000004
#define	HWCAP_26BIT		0x00000008	/* Unsupported, never set.    */
#define	HWCAP_FAST_MULT		0x00000010	/* Always set.                */
#define	HWCAP_FPA		0x00000020	/* Unsupported, never set.    */
#define	HWCAP_VFP		0x00000040
#define	HWCAP_EDSP		0x00000080	/* Always set for ARMv6+.     */
#define	HWCAP_JAVA		0x00000100	/* Unsupported, never set.    */
#define	HWCAP_IWMMXT		0x00000200	/* Unsupported, never set.    */
#define	HWCAP_CRUNCH		0x00000400	/* Unsupported, never set.    */
#define	HWCAP_THUMBEE		0x00000800
#define	HWCAP_NEON		0x00001000
#define	HWCAP_VFPv3		0x00002000
#define	HWCAP_VFPv3D16		0x00004000
#define	HWCAP_TLS		0x00008000	/* Always set for ARMv6+.     */
#define	HWCAP_VFPv4		0x00010000
#define	HWCAP_IDIVA		0x00020000
#define	HWCAP_IDIVT		0x00040000
#define	HWCAP_VFPD32		0x00080000
#define	HWCAP_IDIV		(HWCAP_IDIVA | HWCAP_IDIVT)
#define	HWCAP_LPAE		0x00100000
#define	HWCAP_EVTSTRM		0x00200000	/* Not implemented yet.       */


/* Flags passed in AT_HWCAP2. */
#define	HWCAP2_AES		0x00000001
#define	HWCAP2_PMULL		0x00000002
#define	HWCAP2_SHA1		0x00000004
#define	HWCAP2_SHA2		0x00000008
#define	HWCAP2_CRC32		0x00000010

#endif /* !_MACHINE_ELF_H_ */
