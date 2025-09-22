/*	$OpenBSD: locore0.S,v 1.4 2020/06/14 17:56:54 kettenis Exp $	*/

/*
 * Copyright (c) 2020 Mark Kettenis <kettenis@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "assym.h"

#include <machine/param.h>
	
	.abiversion 2

	.text

	.globl _start
_start:
	mr	%r14, %r3	/* device tree */

	bl	1f
1:	mflr	%r30

	addis	%r1, %r30, (initstack_end - 1b)@ha
	addi	%r1, %r1, (initstack_end - 1b)@l
	addi	%r1, %r1, -(FRAMELEN + 32)

	/*
	 * Not all kexec(8) flavours actually pass the OPAL entry point.
	 * We will overwrite these values later with information from the
	 * FDT.
	*/
	addis	%r3, %r30, (opal_base - 1b)@ha
	addi	%r3, %r3, (opal_base - 1b)@l
	addis	%r4, %r30, (opal_entry - 1b)@ha
	addi	%r4, %r4, (opal_entry - 1b)@l
	std	%r8, 0(%r3)	/* OPAL base */
	std	%r9, 0(%r4)	/* OPAL entry */

	/* Initialize TOC pointer. */
	addis	%r2, %r30, (.TOC. - 1b)@ha
	addi	%r2, %r2, (.TOC. - 1b)@l

	/* Process ELF relocations. */
	addis	%r3, %r30, (_DYNAMIC - 1b)@ha
	addi	%r3, %r3, (_DYNAMIC - 1b)@l
	ld	%r4, -0x8000(%r2)
	subf	%r4, %r4, %r2
	bl	self_reloc

	mr	%r3, %r14	/* device tree */
	mr	%r4, %r2	/* TOC pointer */
	bl	init_powernv
	bl	main

	b	.

	.data

	.align PAGE_SHIFT
	.globl initstack
initstack:
	.space USPACE
initstack_end:
