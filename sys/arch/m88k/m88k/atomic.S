/*	$OpenBSD: atomic.S,v 1.7 2022/12/06 18:50:59 guenther Exp $	*/

/*
 * Copyright (c) 2009 Miodrag Vallat.
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

#include <machine/asm.h>

#ifdef M88110
#define	CACHE_LINE	32
#else
#define	CACHE_LINE	16
#endif
	.data

/*
 * A __cpu_simple_lock_t used to provide the inter-processor interlock,
 * alone on its cache line.
 */
	.balign	CACHE_LINE
ASLOCAL(__atomic_interlock)
	.word	0
	.balign	CACHE_LINE

	.text

/*
 * Register usage in this file:
 *
 * r2 data address
 * r3 bits to set or clear
 * r4 argument / scratch
 * r5 return address
 * r6 interlock address
 * r7 psr upon entry
 * r8 active psr
 * r9 scratch
 */

ENTRY(atomic_setbits_int)
	or	%r5, %r1, %r0		/* save return address */
	bsr	__atomic_lock

	ld	%r4, %r2, %r0
	or	%r4, %r4, %r3
	st	%r4, %r2, %r0

	br	__atomic_unlock

ENTRY(atomic_clearbits_int)
	or	%r5, %r1, %r0		/* save return address */
	bsr	__atomic_lock

	ld	%r4, %r2, %r0
	or	%r4, %r4, %r3
	xor	%r4, %r4, %r3		/* r4 &= ~r3 */
	st	%r4, %r2, %r0

	br	__atomic_unlock

ENTRY(atomic_add_int_nv_mp)
	or	%r5, %r1, %r0		/* save return address */
	bsr	__atomic_lock

	or	%r9, %r2, %r0
	ld	%r2, %r9, %r0
	addu	%r2, %r2, %r3
	st	%r2, %r9, %r0

	br	__atomic_unlock

ENTRY(atomic_sub_int_nv_mp)
	or	%r5, %r1, %r0		/* save return address */
	bsr	__atomic_lock

	or	%r9, %r2, %r0
	ld	%r2, %r9, %r0
	subu	%r2, %r2, %r3
	st	%r2, %r9, %r0

	br	__atomic_unlock

ENTRY(atomic_cas_uint_mp)
	or	%r5, %r1, %r0		/* save return address */
	bsr	__atomic_lock

	ld	%r9, %r2, %r0
	cmp	%r3, %r3, %r9
	bb0	eq,  %r3, 1f
	st	%r4, %r2, %r0
1:
	or	%r2, %r9, %r0

	br	__atomic_unlock

ENTRY(atomic_swap_uint_mp)
	or	%r5, %r1, %r0		/* save return address */
	bsr	__atomic_lock

	ld	%r4, %r2, %r0
	st	%r3, %r2, %r0
	or	%r2, %r4, %r0

	br	__atomic_unlock

GLOBAL(__atomic_lock)

/*
 * If running a kernel with support for both 88100 and 88110 compiled-in
 * on a 88100 machine, the 88100 code (shorter) will be copied over in
 * vector_init().
 */

#ifdef M88110
ASLOCAL(__atomic_lock_88110)
	/*
	 * This is the 88110 version: disable shadowing and interrupts,
	 * then grab the interlock.
	 */

	or.u	%r6, %r0, %hi16(__atomic_interlock)
	or	%r6, %r6, %lo16(__atomic_interlock)

	ldcr	%r7, PSR
	set	%r8, %r7, 1<PSR_INTERRUPT_DISABLE_BIT>
	set	%r8, %r8, 1<PSR_SHADOW_FREEZE_BIT>
	stcr	%r8, PSR
	FLUSH_PIPELINE
1:
	or	%r9, %r0, 1	/* __SIMPLELOCK_LOCKED */
	xmem	%r9, %r6, %r0
	bcnd	eq0, %r9, 3f
2:
	ld	%r9, %r6, %r0
	bcnd	eq0, %r9, 1b
	br	2b
3:
	jmp	%r1
#endif

#ifdef M88100
GLOBAL(__atomic_lock_88100)
	/*
	 * This is the 88100 version: disable interrupts, then grab
	 * the interlock.
	 */

	or.u	%r6, %r0, %hi16(__atomic_interlock)
	or	%r6, %r6, %lo16(__atomic_interlock)

	ldcr	%r7, PSR
	set	%r8, %r7, 1<PSR_INTERRUPT_DISABLE_BIT>
	stcr	%r8, PSR
	FLUSH_PIPELINE

1:
	or	%r9, %r0, 1	/* __SIMPLELOCK_LOCKED */
	xmem	%r9, %r6, %r0
	bcnd	eq0, %r9, 3f
2:
	ld	%r9, %r6, %r0
	bcnd	eq0, %r9, 1b
	br	2b
3:
	jmp	%r1
GLOBAL(__atomic_lock_88100_end)
#endif

GLOBAL(__atomic_unlock)

/*
 * If running a kernel with support for both 88100 and 88110 compiled-in
 * on a 88100 machine, the 88100 code (shorter) will be copied over in
 * vector_init().
 */

#ifdef M88110
ASLOCAL(__atomic_unlock_88110)
	/*
	 * This is the 88110 version: release the interlock, set up
	 * exception registers to return to our caller with initial
	 * psr restored.
	 */

	st	%r0, %r6, %r0	/* release interlock */

	stcr	%r5, EXIP	/* return address */
	stcr	%r7, EPSR	/* original PSR */

	/*
	 * No need to workaround errata #18 (see m88110_user_rte in
	 * eh_common.S), as we are not returning to user mode.
	 */
	RTE
#endif

#ifdef M88100
GLOBAL(__atomic_unlock_88100)
	/*
	 * This is the 88100 version: release the interlock,
	 * restore psr and return to the caller.
	 */

	st	%r0, %r6, %r0	/* release interlock */

	stcr	%r7, PSR
	FLUSH_PIPELINE
	
	jmp	%r5
GLOBAL(__atomic_unlock_88100_end)
#endif
