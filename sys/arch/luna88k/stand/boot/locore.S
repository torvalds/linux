/*	$OpenBSD: locore.S,v 1.4 2022/12/06 18:50:59 guenther Exp $	*/

/*
 * Copyright (c) 2013 Miodrag Vallat.
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
/*
 * Mach Operating System
 * Copyright (c) 1993-1991 Carnegie Mellon University
 * Copyright (c) 1991 OMRON Corporation
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON AND OMRON ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON AND OMRON DISCLAIM ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#define _KERNEL
#define _LOCORE

#include <machine/asm.h>
#include <machine/board.h>
#include <machine/psl.h>

	.text

ASGLOBAL(__start)
	NOP
	NOP

ASLOCAL(main_start)
	/*
	 * We want to only run on one processors, but still allow the
	 * other processors to run the kernel after it is loaded.
	 *
	 * To achieve this, we will `park' secondary processors into
	 * a spin loop, which they will exit once the kernel entry
	 * point and arguments are known.
	 *
	 * They will then proceed to run the kernel, as if the kernel
	 * had been directly booted from the PROM.
	 */
	or.u	%r3,  %r0,  %hi16(cpu_park_address)
	or	%r3,  %r3,  %lo16(cpu_park_address)

	or.u	%r2,  %r0,  %hi16(cpu_park)
	or	%r2,  %r2,  %lo16(cpu_park)
	FLUSH_PIPELINE
	xmem	%r2,  %r3,  %r0

	bcnd	eq0,  %r2, 1f	/* master! causing all the others to park */
	jmp	%r2		/* park ourselves */

1:
	/* clear BSS */
	or.u	%r2, %r0, %hi16(edata)
	or	%r2, %r2, %lo16(edata)
	or.u	%r4, %r0, %hi16(end)
	or	%r4, %r4, %lo16(end)
1:	st	%r0, %r2, %r0
	addu	%r2, %r2, 4
	cmp	%r3, %r2, %r4
	bb1	ne,  %r3, 1b

	/* setup stack, below our image */
	or.u	%r31, %r0,  %hi16(__start)
	or	%r31, %r31, %lo16(__start)

	/* read dip switch settings */
	or.u	%r11, %r0,  %hi16(OBIO_PIO0A)
	ld.bu	%r10, %r11, %lo16(OBIO_PIO0A)
	mak	%r10, %r10, 0<8>
	ld.bu	%r12, %r11, %lo16(OBIO_PIO0B)
	or	%r10, %r10, %r12

	or.u	%r11, %r0,  %hi16(dipswitch)
	st.h	%r10, %r11, %lo16(dipswitch)

	bsr	main
	bsr	_rtt
1:	br	1b

ASLOCAL(cpu_park)
	/* spin a while */
	or.u	%r2,  %r0,  1
9:
	subu	%r2,  %r2,  1
	bcnd	ne0,  %r2,  9b

	/* if kernel entry point is known, exit */
	or.u	%r1,  %r0,  %hi16(cpu_boot)
	ld	%r1,  %r1,  %lo16(cpu_boot)
	bcnd	eq0,  %r1,  cpu_park

	or.u	%r2,  %r0,  1
9:
	subu	%r2,  %r2,  1
	bcnd	ne0,  %r2,  9b

	or.u	%r2,  %r0, %hi16(cpu_bootarg1)
	ld	%r2,  %r2, %lo16(cpu_bootarg1)
	or.u	%r3,  %r0, %hi16(cpu_bootarg2)
	ld	%r3,  %r3, %lo16(cpu_bootarg2)
	or.u	%r4,  %r0, %hi16(cpu_bootarg3)
	ld	%r4,  %r4, %lo16(cpu_bootarg3)
	or.u	%r5,  %r0, %hi16(cpu_bootarg4)
	ld	%r5,  %r5, %lo16(cpu_bootarg4)

	jmp	%r1

GLOBAL(delay)
	bcnd	eq0, %r2, 2f
	or.u	%r3, %r0, %hi16(cpuspeed)
	ld	%r3, %r3, %lo16(cpuspeed)
	mul	%r4, %r2, %r3
	subu	%r4, %r4, 4
1:
	bcnd.n	gt0, %r4, 1b
	 subu	%r4, %r4, 2
2:
	jmp	%r1

	.data

ASLOCAL(cpu_park_address)
	.word	0
