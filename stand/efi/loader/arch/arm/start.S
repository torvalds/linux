/*-
 * Copyright (c) 2014, 2015 Andrew Turner
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

#include <machine/asm.h>

/*
 * We need to be a PE32 file for EFI. On some architectures we can use
 * objcopy to create the correct file, however on arm we need to do
 * it ourselves.
 */

#define	IMAGE_FILE_MACHINE_ARM		0x01c2

#define	IMAGE_SCN_CNT_CODE		0x00000020
#define	IMAGE_SCN_CNT_INITIALIZED_DATA	0x00000040
#define	IMAGE_SCN_MEM_DISCARDABLE	0x02000000
#define	IMAGE_SCN_MEM_EXECUTE		0x20000000
#define	IMAGE_SCN_MEM_READ		0x40000000

	.section .peheader,"a"
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
	.short	IMAGE_FILE_MACHINE_ARM		/* ARM file */
	.short	2				/* 2 Sections */
	.long	0				/* Timestamp */
	.long	0				/* No symbol table */
	.long	0				/* No symbols */
	.short	section_table - optional_header	/* Optional header size */
	.short	0	/* Characteristics TODO: Fill in */

optional_header:
	.short	0x010b				/* PE32 (32-bit addressing) */
	.byte	0				/* Major linker version */
	.byte	0				/* Minor linker version */
	.long	_edata - _end_header		/* Code size */
	.long	0				/* No initialized data */
	.long	0				/* No uninitialized data */
	.long	_start - efi_start		/* Entry point */
	.long	_end_header - efi_start		/* Start of code */
	.long	0				/* Start of data */

optional_windows_header:
	.long	0				/* Image base */
	.long	32				/* Section Alignment */
	.long	8				/* File alignment */
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
	.long	0				/* Stack reserve */
	.long	0				/* Stack commit */
	.long	0				/* Heap reserve */
	.long	0				/* Heap commit */
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
	/* We need a .reloc section for EFI */
	.ascii	".reloc"
	.byte	0
	.byte	0				/* Pad to 8 bytes */
	.long	0				/* Virtual size */
	.long	0				/* Virtual address */
	.long	0				/* Size of raw data */
	.long	0				/* Pointer to raw data */
	.long	0				/* Pointer to relocations */
	.long	0				/* Pointer to line numbers */
	.short	0				/* Number of relocations */
	.short	0				/* Number of line numbers */
	.long	(IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | \
		 IMAGE_SCN_MEM_DISCARDABLE)	/* Characteristics */

	/* The contents of the loader */
	.ascii	".text"
	.byte	0
	.byte	0
	.byte	0				/* Pad to 8 bytes */
	.long	_edata - _end_header		/* Virtual size */
	.long	_end_header - efi_start		/* Virtual address */
	.long	_edata - _end_header		/* Size of raw data */
	.long	_end_header - efi_start		/* Pointer to raw data */
	.long	0				/* Pointer to relocations */
	.long	0				/* Pointer to line numbers */
	.short	0				/* Number of relocations */
	.short	0				/* Number of line numbers */
	.long	(IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | \
		 IMAGE_SCN_MEM_READ)		/* Characteristics */
_end_header:

	.text
_start:
	/* Save the boot params to the stack */
	push	{r0, r1}

	adr	r0, .Lbase
	ldr	r1, [r0]
	sub	r5, r0, r1

	ldr	r0, .Limagebase
	add	r0, r0, r5
	ldr	r1, .Ldynamic
	add	r1, r1, r5

	bl	_C_LABEL(self_reloc)

	/* Zero the BSS, _reloc fixed the values for us */
	ldr	r0, .Lbss
	ldr	r1, .Lbssend
	mov	r2, #0

1:	cmp	r0, r1
	bge	2f
	str	r2, [r0], #4
	b	1b
2:

	pop	{r0, r1}
	bl	_C_LABEL(efi_main)

1:	b	1b

.Lbase:
	.word	.
.Limagebase:
	.word	ImageBase
.Ldynamic:
	.word	_DYNAMIC
.Lbss:
	.word	__bss_start
.Lbssend:
	.word	__bss_end

.align	3
stack:
	.space 512
stack_end:

