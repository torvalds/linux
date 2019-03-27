/*	$NetBSD: rtld_start.S,v 1.5 2001/08/14 22:17:48 eeh Exp $	*/

/*-
 * Copyright (c) 2001 Jake Burkholder.
 * Copyright (c) 2000 Eduardo Horvath.
 * Copyright (c) 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas and Paul Kranenburg.
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
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
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
 * $FreeBSD$
 */

#include <machine/asm.h>

/*
 * ELF:
 *	On startup the stack should contain 16 extended word register save
 *	area, followed by the arg count, etc.
 */

ENTRY(.rtld_start)
	clr	%fp
	mov	%o0, %l0
	mov	%o3, %l1

	sub	%sp, 16, %sp
	add	%sp, SPOFF + CCFSZ + 0x0, %o1
	call	_rtld
	 add	%sp, SPOFF + CCFSZ + 0x8, %o2

	ldx	[%sp + SPOFF + CCFSZ + 0x0], %o1
	ldx	[%sp + SPOFF + CCFSZ + 0x8], %o2
	add	%sp, 16, %sp

	mov	%l1, %o3
	jmp	%o0
	 mov	%l0, %o0
END(.rtld_start)

/*
 * Find the address of _DYNAMIC by disassembling a call instruction to it.
 * Binutils may not fill in the GOT as expected on other architectures.
 */
.weak	_DYNAMIC

ENTRY(rtld_dynamic_addr)
	save	%sp, -CCFSZ, %sp
	call	1f
	 nop
	call	_DYNAMIC + 8
1:	lduw	[%o7 + 8], %o0
	sll	%o0, 2, %o0
	sra	%o0, 0, %o0
	ret
	 restore %o0, %o7, %o0
END(rtld_dynamic_addr)

	/*
	 * We have two separate entry points to the runtime linker.
	 * I'm implementing this following the SPARC v9 ABI spec.
	 *
	 * _rtld_bind_start_0(x, y) is called from .PLT0, and is used for
	 * PLT entries above 32768.
	 *
	 * _rtld_bind_start_1(x, y) is called from .PLT1, and is used for
	 * PLT entries below 32768.
	 *
	 * The first two entries of PLT2 contain the xword object pointer.
	 *
	 * These routines are called with two longword arguments,
	 * x and y.  To calculate the address of the entry,
	 * _rtld_bind_start_1(x, y) does:
	 *
	 *	n = x >> 15;
	 *
	 * and _rtld_bind_start_0(x, y) should do, according to the SCD:
	 *
	 *	i = x - y - 1048596;
	 *	n = 32768 + (i/5120)*160 + (i%5120)/24;
	 *
	 * Note that the number of 1048596 from above is incorrect; rather,
	 * we need to use HIPLTOFFS as defined below.
	 *
	 * Neither routine needs to issue a save since it's already been
	 * done in the PLT entry.
	 */

#define	NPLTLOSLOTS	32768
#define	PLTSLOTSZ	32
/*
 * - 16 to compensate for the difference of the positions of the jumps that
 * generate the arguments in .PLT0 and the high plt entry.
 */
#define	HIPLTOFFS	(NPLTLOSLOTS * PLTSLOTSZ - 16)

ENTRY(_rtld_bind_start_0)
	sethi	%hi(HIPLTOFFS), %l1
	or	%l1, %lo(HIPLTOFFS), %l1
	sub	%o0, %o1, %l0		/* x - y */
	sub	%l0, %l1, %l0		/* i = x - y - HIPLTOFFS */
	sethi	%hi(5120), %l7
	sdivx	%l0, %l7, %l1		/* Calculate i / 5120 */
	mulx	%l1, %l7, %l3
	sub	%l0, %l3, %l2		/* And i % 5120 */
	mulx	%l1, 160, %l5		/* (i / 5120) * 160 */
	sdivx	%l2, 24, %l4		/* (i % 5120) / 24 */
	sethi	%hi(NPLTLOSLOTS), %l6
	add	%l4, %l5, %l4		/* (i / 5120) * 160 + (i % 5120) / 24 */
	add	%l4, %l6, %l4		/* + NPLTLOSLOTS */
	sub	%l4, 4, %l4		/* XXX:	4 entries are reserved */

	sllx	%l4, 1, %l5		/* Each element is an Elf_Rela which */
	add	%l5, %l4, %l4		/*  is 3 longwords or 24 bytes. */
	sllx	%l4, 3, %l4		/*  So multiply by 24. */

	ldx	[%o1 + (10*4)], %o0	/* Load object pointer from PLT2 */

	call	_rtld_bind		/* Call _rtld_bind(obj, offset) */
	 mov	%l4, %o1

	jmp	%o0			/* return value == function address */
	 restore			/* Dump our stack frame */
END(_rtld_bind_start_0)

ENTRY(_rtld_bind_start_1)
	srax	%o0, 15, %o2		/* %o0 is the index to our PLT slot */
	sub	%o2, 4, %o2		/* XXX:	4 entries are reserved */

	sllx	%o2, 1, %o3		/* Each element is an Elf_Rela which */
	add	%o3, %o2, %o2		/*  is 3 longwords or 24 bytes. */
	sllx	%o2, 3, %o2		/*  So multiply by 24. */

	ldx	[%o1 + 8], %o0		/* The object pointer is at [%o1 + 8] */

	call	_rtld_bind		/* Call _rtld_bind(obj, offset) */
	 mov	%o2, %o1

	jmp	%o0			/* return value == function address */
	 restore			/* Dump our stack frame */
END(_rtld_bind_start_1)
