/* $OpenBSD: start.S,v 1.5 2022/07/30 21:06:54 patrick Exp $ */
/*-
 * Copyright (c) 2014 Andrew Turner
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
 * $FreeBSD: head/sys/boot/efi/loader/arch/arm64/start.S 282727 2015-05-10 13:24:26Z ian $
 */

/*
 * We need to be a PE32+ file for EFI. On some architectures we can use
 * objcopy to create the correct file, however on arm64 we need to do
 * it ourselves.
 */

#define	IMAGE_FILE_MACHINE_ARM64	0xaa64

#define	IMAGE_SCN_CNT_CODE		0x00000020
#define	IMAGE_SCN_CNT_INITIALIZED_DATA	0x00000040
#define	IMAGE_SCN_MEM_DISCARDABLE	0x02000000
#define	IMAGE_SCN_MEM_EXECUTE		0x20000000
#define	IMAGE_SCN_MEM_READ		0x40000000
#define	IMAGE_SCN_MEM_WRITE		0x80000000

	.globl __data_start

	.section .peheader, "a"
efi_start:
	/* The MS-DOS Stub, only used to get the offset of the COFF header */
	.ascii	"MZ"
	.short	0
	.space	0x38
	.long	pe_sig - efi_start

	/* The PE32 Signature. Needs to be 8-byte aligned */
	.align	3
pe_sig:
	.ascii	"PE"
	.short	0
coff_head:
	.short	IMAGE_FILE_MACHINE_ARM64	/* AArch64 file */
	.short	2				/* 2 Sections */
	.long	0				/* Timestamp */
	.long	0				/* No symbol table */
	.long	0				/* No symbols */
	.short	section_table - optional_header	/* Optional header size */
	.short	0x0206				/* Characteristics */

optional_header:
	.short	0x020b				/* PE32+ (64-bit addressing) */
	.byte	0				/* Major linker version */
	.byte	0				/* Minor linker version */
	.long	_etext - _end_header		/* Code size */
	.long	__data_size			/* Initialized data size */
	.long	0				/* No uninitialized data */
	.long	_start - efi_start		/* Entry point */
	.long	_end_header - efi_start		/* Start of code */

optional_windows_header:
	.quad	0				/* Image base */
	.long	4096				/* Section Alignment */
	.long	512				/* File alignment */
	.short	0				/* Major OS version */
	.short	0				/* Minor OS version */
	.short	0				/* Major image version */
	.short	0				/* Minor image version */
	.short	0				/* Major subsystem version */
	.short	0				/* Minor subsystem version */
	.long	0				/* Win32 version */
	.long	_edata - efi_start		/* Image size */
	.long	_end_header - efi_start		/* Header size */
	.long	0				/* Checksum */
	.short	0xa				/* Subsystem (EFI app) */
	.short	0				/* DLL Characteristics */
	.quad	0				/* Stack reserve */
	.quad	0				/* Stack commit */
	.quad	0				/* Heap reserve */
	.quad	0				/* Heap commit */
	.long	0				/* Loader flags */
	.long	6				/* Number of RVAs */

	/* RVAs: */
	.quad	0
	.quad	0
	.quad	0
	.quad	0
	.quad	0
	.quad	0

section_table:
	/* The contents of the loader */
	.ascii	".text"
	.byte	0
	.byte	0
	.byte	0				/* Pad to 8 bytes */
	.long	_etext - _end_header		/* Virtual size */
	.long	_end_header - efi_start		/* Virtual address */
	.long	_etext - _end_header		/* Size of raw data */
	.long	_end_header - efi_start		/* Pointer to raw data */
	.long	0				/* Pointer to relocations */
	.long	0				/* Pointer to line numbers */
	.short	0				/* Number of relocations */
	.short	0				/* Number of line numbers */
	.long	(IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | \
		 IMAGE_SCN_MEM_READ)		/* Characteristics */

	/* The contents of the loader */
	.ascii	".data"
	.byte	0
	.byte	0
	.byte	0				/* Pad to 8 bytes */
	.long	__data_size			/* Virtual size */
	.long	__data_start - efi_start	/* Virtual address */
	.long	__data_size			/* Size of raw data */
	.long	__data_start - efi_start	/* Pointer to raw data */
	.long	0				/* Pointer to relocations */
	.long	0				/* Pointer to line numbers */
	.short	0				/* Number of relocations */
	.short	0				/* Number of line numbers */
	.long	(IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | \
		 IMAGE_SCN_MEM_WRITE)		/* Characteristics */

	.align	12
_end_header:

	.text
	.globl	_start
_start:
	/* Save the boot params to the stack */
	stp	x0, x1, [sp, #-16]!

	adr	x0, __bss_start
	adr	x1, __bss_end

	b 2f

1:
	stp	xzr, xzr, [x0], #16
2:
	cmp	x0, x1
	b.lo	1b

	adr	x0, ImageBase
	adr	x1, _DYNAMIC

	bl	self_reloc

	ldp	x0, x1, [sp], #16

	bl	efi_main

1:	b	1b

	.data
	.align 4
#include "dt_blob.S"
