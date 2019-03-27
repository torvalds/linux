/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

	.ident	"%Z%%M%	%I%	%E% SMI"

	.file	"%M%"

#define	_ASM
#include <sys/asm_linkage.h>

#include <machine/asi.h>

/* Userland needs different ASIs. */
#ifdef _KERNEL
#define	__ASI_ATOMIC	ASI_N
#else
#define	__ASI_ATOMIC	ASI_P
#endif

	/*
	 * NOTE: If atomic_add_64 and atomic_add_64_nv are ever
	 * separated, you need to also edit the libc sparcv9 platform
	 * specific mapfile and remove the NODYNSORT attribute
	 * from atomic_add_64_nv.
	 */
	ENTRY(atomic_add_64)
	ALTENTRY(atomic_add_64_nv)
	ALTENTRY(atomic_add_ptr)
	ALTENTRY(atomic_add_ptr_nv)
	ALTENTRY(atomic_add_long)
	ALTENTRY(atomic_add_long_nv)
add_64:
	ldx	[%o0], %o2
1:
	add	%o2, %o1, %o3
	casxa	[%o0] __ASI_ATOMIC, %o2, %o3
	cmp	%o2, %o3
	bne,a,pn %xcc, 1b
	  mov	%o3, %o2
	retl
	add	%o2, %o1, %o0		! return new value
	SET_SIZE(atomic_add_long_nv)
	SET_SIZE(atomic_add_long)
	SET_SIZE(atomic_add_ptr_nv)
	SET_SIZE(atomic_add_ptr)
	SET_SIZE(atomic_add_64_nv)
	SET_SIZE(atomic_add_64)

	/*
	 * NOTE: If atomic_or_8 and atomic_or_8_nv are ever
	 * separated, you need to also edit the libc sparcv9 platform
	 * specific mapfile and remove the NODYNSORT attribute
	 * from atomic_or_8_nv.
	 */
	ENTRY(atomic_or_8)
	ALTENTRY(atomic_or_8_nv)
	ALTENTRY(atomic_or_uchar)
	and	%o0, 0x3, %o4		! %o4 = byte offset, left-to-right
	xor	%o4, 0x3, %g1		! %g1 = byte offset, right-to-left
	sll	%g1, 3, %g1		! %g1 = bit offset, right-to-left
	set	0xff, %o3		! %o3 = mask
	sll	%o3, %g1, %o3		! %o3 = shifted to bit offset
	sll	%o1, %g1, %o1		! %o1 = shifted to bit offset
	and	%o1, %o3, %o1		! %o1 = single byte value
	andn	%o0, 0x3, %o0		! %o0 = word address
	ld	[%o0], %o2		! read old value
1:
	or	%o2, %o1, %o5		! or in the new value
	casa	[%o0] __ASI_ATOMIC, %o2, %o5
	cmp	%o2, %o5
	bne,a,pn %icc, 1b
	  mov	%o5, %o2		! %o2 = old value
	or	%o2, %o1, %o5
	and	%o5, %o3, %o5
	retl
	srl	%o5, %g1, %o0		! %o0 = new value
	SET_SIZE(atomic_or_uchar)
	SET_SIZE(atomic_or_8_nv)
	SET_SIZE(atomic_or_8)

	/*
	 * Spitfires and Blackbirds have a problem with membars in the
	 * delay slot (SF_ERRATA_51).  For safety's sake, we assume
	 * that the whole world needs the workaround.
	 */

	ENTRY(membar_producer)
	membar	#StoreStore
	retl
	nop
	SET_SIZE(membar_producer)
