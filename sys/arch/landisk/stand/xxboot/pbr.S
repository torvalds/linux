/*	$OpenBSD: pbr.S,v 1.3 2022/12/08 02:11:27 guenther Exp $	*/
/*	$NetBSD: pbr.S,v 1.1 2006/09/01 21:26:19 uwe Exp $	*/

/*-
 * Copyright (c) 2005 NONAKA Kimihiro
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
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <machine/asm.h>

#ifdef BOOT_FROM_FAT
#define MBR_AFTERBPB	90		/* BPB size in FAT32 partition BR */
#else
#define MBR_AFTERBPB	62		/* BPB size in floppy master BR */
#endif

ENTRY(start)
	bra	start0
	 .byte	0x11			/* 0x4f11: cmp/pz r15... */
	.asciz	"OpenBSD"

	. = start + 0x0b		/* move to start of BPB */

	. = start + 0x1c		/* skip BPB */
start0:
	mova	pbr_end, r0
	mov.w	pbr_size, r2
	sub	r2, r0
	mov	r0, r11			/* r11: own loaded address */

	mov.w	stack_offset, r1
	add	r1, r0
	mov	r0, r15			/* r15: stack pointer */
	mov	r0, r10			/* r10: load address */

	/* enable/flush cache */
	mov	#0, r4
	mov	#6, r0
	trapa	#0x3f

	/* Read from start of disk */
	mov	#0x40, r4		/* LBA */
	mov	#0, r5			/* LBA #0 */
	mov	r10, r6			/* buffer address */
	bsr	read_sectors_lba
	 mov	#XXBOOT_SECTORS, r7	/* number of sectors */

	mov.l	@r11, r1
	mov.l	@r10, r2
	cmp/eq	r1, r2
	bt/s	pbr_read_ok
	 mov	#0, r9			/* r9: sector # */

	/* Search bootable partition */
	mov.w	part_offset, r12
	add	r10, r12		/* r12: pointer to partition entry */
	mov	#4, r8			/* r8: partition loop counter */
loop_part:
	mov.b	@(4, r12), r0
#ifdef BOOT_FROM_FAT
	cmp/eq	#MBR_PTYPE_FAT12, r0
	bt	found
	cmp/eq	#MBR_PTYPE_FAT16S, r0
	bt	found
	cmp/eq	#MBR_PTYPE_FAT16B, r0
	bt	found
	cmp/eq	#MBR_PTYPE_FAT32, r0
	bt	found
	cmp/eq	#MBR_PTYPE_FAT32L, r0
	bt	found
	cmp/eq	#MBR_PTYPE_FAT16L, r0
	bt	found
#else
	cmp/eq	#0xa6, r0
#endif
	bf	next_part

found:
	/* found boot partition */
	mov.w	@(8, r12), r0
	mov	r0, r1
	mov.w	@(10, r12), r0
	extu.w	r1, r1
	shll16	r0
	or	r1, r0
	tst	r0, r0
	bt	next_part		/* start LBA == 0 ? */

	bra	boot_lba
	 mov	r0, r9

next_part:
	dt	r8
	bf/s	loop_part
	 add	#16, r12

ptn_error:
	/* no OpenBSD partition found */
	mova	ERR_PTN, r0
error:
	bsr	message_crlf
	 mov	r0, r4
99:	bra	99b
	 nop

read_error:
	bra	error
	 mova	ERR_READ, r0

magic_error:
	bra	error
	 mova	ERR_NO_XXBOOT, r0

message_crlf:
	mov	#32, r0
	trapa	#0x3f
	mova	crlf, r0
	mov	r0, r4
	mov	#32, r0
	trapa	#0x3f
	rts
	 nop

read_sectors_lba:
	mov	#2, r0
	trapa	#0x3f
	tst	r0, r0
	bf	read_error
	rts
	 nop

boot_lba:
	mov	#0x40, r4		/* LBA */
	mov	r9, r5			/* LBA # */
	mov	r10, r6			/* buffer address */
	bsr	read_sectors_lba
	 mov	#XXBOOT_SECTORS, r7	/* number of sectors */

pbr_read_ok:
	mov.l	.L.xxboot_magic1, r1
	mov.l	.L.xxboot_magic, r2
	mov.l	@r2, r2
	cmp/eq	r1, r2
	bf	magic_error

	/* flush cache */
	mov	#0, r4
	mov	#6, r0
	trapa	#0x3f

	mov.l	.L.xxboot_start, r13
	jmp	@r13			/* jump to xxboot */
	 mov	r9, r4			/* pass sector address to xxboot */


	.align	1
pbr_size:	.word	pbr_end - start
	.align	1
stack_offset:	.word	0x1000
	.align	1
part_offset:	.word	0x1be
	.align	1
magic_offset:	.word	0x1fe

	.align	2
.L.xxboot_magic1:
	.long	0x20031125
.L.xxboot_magic:
	.long	xxboot_magic
.L.xxboot_start:
	.long	xxboot_start

	.align	2
crlf:		.asciz	"\r\n"

	.align	2
ERR_READ:	.asciz  "Disk read"
	.align	2
ERR_NO_XXBOOT:	.asciz  "Not a xxboot image"
	.align	2
ERR_PTN:	.asciz  "No OpenBSD partition"


	. = start + 0x1fe
magic:
	.word	0xaa55
pbr_end:

	.global	ptn_disklabel
ptn_disklabel:
	.fill	512
