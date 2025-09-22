/* $OpenBSD: subr.S,v 1.34 2025/08/13 16:21:32 miod Exp $	*/
/*
 * Mach Operating System
 * Copyright (c) 1993-1992 Carnegie Mellon University
 * Copyright (c) 1991 OMRON Corporation
 * Copyright (c) 1996 Nivas Madhur
 * Copyright (c) 1998 Steve Murphree, Jr.
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

#include "assym.h"

#include <sys/errno.h>

#include <machine/param.h>
#include <machine/asm.h>
#include <machine/psl.h>
#include <machine/trap.h>

#ifdef	M88100

/*
 * DO_LOAD_ADDRESS
 *
 * 	unsigned int do_load_word(address, supervisor_mode)
 *		vaddr_t address;		\\ in r2
 *		boolean_t supervisor_mode;	\\ in r3
 *
 * Return the word at ADDRESS (from user space if SUPERVISOR_MODE is zero,
 * supervisor space if non-zero).
 *
 */

ENTRY(do_load_word)	/* do_load_word(address, supervisor) */
	bcnd	ne0,%r3,1f
#ifdef ERRATA__XXX_USR
	NOP
	ld.usr	%r2,%r2,%r0
	NOP
	NOP
	NOP
	jmp	%r1
#else
	jmp.n	%r1
	 ld.usr	%r2,%r2,%r0
#endif
1:	jmp.n	%r1
	 ld	%r2,%r2,%r0

ENTRY(do_load_half)	/* do_load_half(address, supervisor) */
	bcnd	ne0,%r3,1f
#ifdef ERRATA__XXX_USR
	NOP
	ld.h.usr	%r2,%r2,%r0
	NOP
	NOP
	NOP
	jmp	%r1
#else
	jmp.n	%r1
	 ld.h.usr	%r2,%r2,%r0
#endif
1:	jmp.n	%r1
	 ld.h	%r2,%r2,%r0

ENTRY(do_load_byte)	/* do_load_byte(address, supervisor) */
	bcnd	ne0,%r3,1f
#ifdef ERRATA__XXX_USR
	NOP
	ld.b.usr	%r2,%r2,%r0
	NOP
	NOP
	NOP
	jmp	%r1
#else
	jmp.n	%r1
	 ld.b.usr	%r2,%r2,%r0
#endif
1:	jmp.n	%r1
	 ld.b	%r2,%r2,%r0

ENTRY(do_store_word)	/* do_store_word(address, data, supervisor) */
	bcnd	ne0,%r4,1f
#ifdef ERRATA__XXX_USR
	NOP
	st.usr	%r3,%r2,%r0
	NOP
	NOP
	NOP
	jmp	%r1
#else
	jmp.n	%r1
	 st.usr	%r3,%r2,%r0
#endif
1:	jmp.n	%r1
	 st	%r3,%r2,%r0

ENTRY(do_store_half)	/* do_store_half(address, data, supervisor) */
	bcnd	ne0,%r4,1f
#ifdef ERRATA__XXX_USR
	NOP
	st.h.usr	%r3,%r2,%r0
	NOP
	NOP
	NOP
	jmp	%r1
#else
	jmp.n	%r1
	 st.h.usr	%r3,%r2,%r0
#endif
1:	jmp.n	%r1
	 st.h	%r3,%r2,%r0

ENTRY(do_store_byte)	/* do_store_byte(address, data, supervisor) */
	bcnd	ne0,%r4,1f
#ifdef ERRATA__XXX_USR
	NOP
	st.b.usr	%r3,%r2,%r0
	NOP
	NOP
	NOP
	jmp	%r1
#else
	jmp.n	%r1
	 st.b.usr	%r3,%r2,%r0
#endif
1:	jmp.n	%r1
	 st.b	%r3,%r2,%r0

ENTRY(do_xmem_word)	/* do_xmem_word(address, data, supervisor) */
	bcnd	ne0,%r4,1f
#ifdef ERRATA__XXX_USR
	NOP
#endif
	xmem.usr	%r3,%r2,%r0
#ifdef ERRATA__XXX_USR
	NOP
	NOP
	NOP
#endif
	jmp.n	%r1
	 or	%r2, %r3, %r0
1:	xmem	%r3,%r2,%r0
	jmp.n	%r1
	 or	%r2, %r3, %r0

ENTRY(do_xmem_byte)	/* do_xmem_byte(address, data, supervisor) */
	bcnd	ne0,%r4,1f
#ifdef ERRATA__XXX_USR
	NOP
#endif
	xmem.bu.usr	%r3,%r2,%r0
#ifdef ERRATA__XXX_USR
	NOP
	NOP
	NOP
#endif
	jmp.n	%r1
	 or	%r2,%r3,%r0
1:	xmem.bu	%r3,%r2,%r0
	jmp.n	%r1
	 or	%r2,%r3,%r0

#endif	/* M88100 */

/*
 * Copy specified amount of data from user space into the kernel
 * _copyin(from, to, len)
 *	r2 == from (user source address)
 *	r3 == to (kernel destination address)
 *	r4 == length
 */

#define	SRC	%r2
#define	DEST	%r3
#define	LEN	%r4

ENTRY(_copyin)
	/* set up fault handler */
	ldcr	%r5,  CPU
	ld	%r6,  %r5,  CI_CURPCB
	or.u	%r5,  %r0,  %hi16(Lciflt)
	or	%r5,  %r5,  %lo16(Lciflt)
	st	%r5,  %r6,  PCB_ONFAULT	/* pcb_onfault = Lciflt */

	/*
	 * If it's a small length (less than 8), then do byte-by-byte.
	 * Despite not being optimal if len is 4, and from and to
	 * are word-aligned, this is still faster than doing more tests
	 * to save an hyperthetical fraction of cycle.
	 */
	cmp	%r9,  LEN,  8
	bb1	lt,   %r9,  copyin_byte_only

	/* If they're not aligned similarly, use byte only... */
	xor	%r9,  SRC,  DEST
	mask	%r8,  %r9,  0x3
	bcnd	ne0,  %r8,  copyin_byte_only

	/*
	 * At this point, we don't know if they're word aligned or not,
	 * but we know that what needs to be done to one to align
	 * it is what's needed for the other.
	 */
	bb1	0,    SRC,  copyin_left_align_to_halfword
ASLOCAL(copyin_left_aligned_to_halfword)
	bb1	1,    SRC,  copyin_left_align_to_word
ASLOCAL(copyin_left_aligned_to_word)
	bb1	0,    LEN,  copyin_right_align_to_halfword
ASLOCAL(copyin_right_aligned_to_halfword)
	bb1	1,    LEN,  copyin_right_align_to_word
ASLOCAL(copyin_right_aligned_to_word)

	/*
	 * At this point, both SRC and DEST are aligned to a word
	 * boundary, and LEN is a multiple of 4. We want it an even
	 * multiple of 4.
	 */
	bb1.n	2,    LEN,  copyin_right_align_to_doubleword
	 or	%r7,  %r0,  4

ASLOCAL(copyin_right_aligned_to_doubleword)
#ifdef ERRATA__XXX_USR
	NOP
	ld.usr	%r5,  SRC,  %r0
	NOP
	NOP
	NOP
	ld.usr	%r6,  SRC,  %r7
	NOP
	NOP
	NOP
#else
	ld.usr	%r5,  SRC,  %r0
	ld.usr	%r6,  SRC,  %r7
#endif
	subu	LEN,  LEN,  8
	st	%r5,  DEST, %r0
	addu	SRC,  SRC,  8
	st	%r6,  DEST, %r7
	bcnd.n	ne0,  LEN,  copyin_right_aligned_to_doubleword
	 addu	DEST, DEST, 8
	br.n	Lcidone
	 or	%r2, %r0, %r0	/* successful return */

ASLOCAL(copyin_left_align_to_halfword)
#ifdef ERRATA__XXX_USR
	NOP
	ld.b.usr	%r5,  SRC, %r0
	NOP
	NOP
	NOP
#else
	ld.b.usr	%r5,  SRC, %r0
#endif
	subu	LEN,  LEN,  1
	st.b	%r5,  DEST, %r0
	addu	SRC,  SRC,  1
	br.n	copyin_left_aligned_to_halfword
	 addu	DEST, DEST, 1

ASLOCAL(copyin_left_align_to_word)
#ifdef ERRATA__XXX_USR
	NOP
	ld.h.usr	%r5,   SRC,  %r0
	NOP
	NOP
	NOP
#else
	ld.h.usr	%r5,   SRC,  %r0
#endif
	subu	LEN,  LEN,  2
	st.h	%r5,  DEST, %r0
	addu	SRC,  SRC,  2
	br.n	copyin_left_aligned_to_word
	 addu	DEST, DEST, 2

ASLOCAL(copyin_right_align_to_halfword)
	subu	LEN,  LEN,  1
#ifdef ERRATA__XXX_USR
	NOP
	ld.b.usr	%r5,  SRC, LEN
	NOP
	NOP
	NOP
#else
	ld.b.usr	%r5,  SRC, LEN
#endif
	br.n	copyin_right_aligned_to_halfword
	 st.b	%r5,  DEST, LEN

ASLOCAL(copyin_right_align_to_word)
	subu	LEN,  LEN,  2
#ifdef ERRATA__XXX_USR
	NOP
	ld.h.usr	%r5,  SRC, LEN
	NOP
	NOP
	NOP
#else
	ld.h.usr	%r5,  SRC, LEN
#endif
	br.n	copyin_right_aligned_to_word
	 st.h	%r5,  DEST, LEN

ASLOCAL(copyin_right_align_to_doubleword)
	subu	LEN,  LEN,  4
#ifdef ERRATA__XXX_USR
	NOP
	ld.usr	%r5,  SRC,  LEN
	NOP
	NOP
	NOP
#else
	ld.usr	%r5,  SRC,  LEN
#endif
	bcnd.n	ne0,  LEN, copyin_right_aligned_to_doubleword
	 st	%r5,  DEST, LEN
	br.n	Lcidone
   	 or	%r2, %r0, %r0	/* successful return */

ASLOCAL(copyin_byte_only)
	bcnd	eq0, LEN, 2f
1:
	subu	LEN, LEN, 1
#ifdef ERRATA__XXX_USR
	NOP
	ld.b.usr	%r5, SRC, LEN
	NOP
	NOP
	NOP
#else
	ld.b.usr	%r5, SRC, LEN
#endif
	bcnd.n	ne0, LEN, 1b
	 st.b	%r5, DEST, LEN
2:
	or	%r2, %r0, %r0	/* successful return */
	/* FALLTHROUGH */

ASLOCAL(Lcidone)
	ldcr	%r5,  CPU
	ld	%r6,  %r5,  CI_CURPCB
	jmp.n	%r1
	 st	%r0,  %r6,  PCB_ONFAULT

ASLOCAL(Lciflt)
	br.n	Lcidone
	 or	%r2, %r0, EFAULT	/* return fault */

#undef	SRC
#undef	DEST
#undef	LEN

/*
 * Specific flavour for a single 32-bit word copy.
 * copyin32(from, to)
 *	r2 == from (user source address)
 *	r3 == to (kernel destination address)
 */

#define	SRC	%r2
#define	DEST	%r3

ENTRY(copyin32)
	/* check for source alignment */
	mask	%r8,  SRC,  0x3
	bcnd	ne0,  %r8,  copyin32_misaligned

	/* set up fault handler */
	ldcr	%r5,  CPU
	ld	%r6,  %r5,  CI_CURPCB
	or.u	%r5,  %r0,  %hi16(Lciflt)
	or	%r5,  %r5,  %lo16(Lciflt)
	st	%r5,  %r6,  PCB_ONFAULT	/* pcb_onfault = Lciflt */

#ifdef ERRATA__XXX_USR
	NOP
	ld.usr	%r5,  SRC,  %r0
	NOP
	NOP
	NOP
#else
	ld.usr	%r5,  SRC,  %r0
#endif
	st	%r5,  DEST, %r0
	br.n	Lcidone
   	 or	%r2, %r0, %r0	/* successful return */

ASLOCAL(copyin32_misaligned)
	jmp.n	%r1
	 or	%r2, %r0, EFAULT	/* return fault */

#undef	SRC
#undef	DEST

/*######################################################################*/
/*######################################################################*/

/*
 * Copy a null terminated string from the user space to the kernel
 * address space.
 *
 * _copyinstr(from, to, maxlen, &lencopied)
 * r2 == from
 * r3 == to
 * r4 == maxlen
 * r5 == len actually transferred (includes the terminating NUL!!!)
 * r6 & r7 - used as temporaries
 */
#define	SRC	%r2
#define	DEST	%r3
#define	CNT	%r4
#define	LEN	%r5

ENTRY(_copyinstr)

	/* setup fault handler */
	ldcr	%r6,  CPU
	ld	%r7,  %r6,   CI_CURPCB
	or.u	%r6,  %r0,   %hi16(Lcisflt)
	or	%r6,  %r6,   %lo16(Lcisflt)
	st	%r6,  %r7,   PCB_ONFAULT
	or	%r6,  %r0,   0
	bcnd	lt0,  CNT,  Lcisflt
	bcnd	eq0,  CNT,  Lcistoolong
1:
#ifdef ERRATA__XXX_USR
	NOP
	ld.bu.usr	%r7,  SRC, %r6
	NOP
	NOP
	NOP
#else
	ld.bu.usr	%r7,  SRC,  %r6
#endif
	st.b	%r7,  DEST, %r6
	bcnd.n	eq0,  %r7,  2f		/* all done */
	 addu	%r6,  %r6,  1
	cmp	%r7,  %r6,  CNT
	bb1	lt,   %r7,  1b

ASLOCAL(Lcistoolong)
	or	%r2,   %r0, ENAMETOOLONG	/* overflow */

ASLOCAL(Lcisnull)
	bcnd	eq0,%r6, Lcisdone		/* do not attempt to clear last byte */
					/* if we did not write to the string */
	subu	%r6,  %r6,  1
	st.b	%r0,  DEST, %r6		/* clear last byte */
	br.n	Lcisdone
	 addu	%r6,  %r6,  1
2:					/* all done */
	or	%r2,  %r0,  0

ASLOCAL(Lcisdone)
	bcnd	eq0, LEN, 3f
	st	%r6, %r0, LEN
3:
	ldcr	%r5,  CPU
	ld	%r6,  %r5,  CI_CURPCB
	jmp.n	%r1
	 st	%r0,  %r6,  PCB_ONFAULT	/* clear the handler */

ASLOCAL(Lcisflt)
	br.n	Lcisnull
	 or	%r2,  %r0,  EFAULT	/* return fault */

#undef	SRC
#undef	DEST
#undef	CNT
#undef	LEN

/*
 * Copy specified amount of data from kernel to the user space
 * Copyout(from, to, len)
 *	r2 == from (kernel source address)
 *	r3 == to (user destination address)
 *	r4 == length
 */

#define	SRC	%r2
#define	DEST	%r3
#define	LEN	%r4

ENTRY(copyout)
	/* setup fault handler */
	ldcr	%r5,  CPU
	ld	%r6,  %r5,  CI_CURPCB
	or.u	%r5,  %r0,  %hi16(Lcoflt)
	or	%r5,  %r5,  %lo16(Lcoflt)
	st	%r5,  %r6,  PCB_ONFAULT	/* pcb_onfault = Lcoflt */

	/*
	 * If it's a small length (less than 8), then do byte-by-byte.
	 * Despite not being optimal if len is 4, and from and to
	 * are word-aligned, this is still faster than doing more tests
	 * to save an hyperthetical fraction of cycle.
	 */
	cmp	%r9,  LEN,  8
	bb1	lt,   %r9,   copyout_byte_only

	/* If they're not aligned similarly, use byte only... */
	xor	%r9,  SRC,  DEST
	mask	%r8,  %r9,  0x3
	bcnd	ne0,  %r8,  copyout_byte_only

	/*
	 * At this point, we don't know if they're word aligned or not,
	 * but we know that what needs to be done to one to align
	 * it is what's needed for the other.
	 */
	bb1	0,    SRC,  copyout_left_align_to_halfword
ASLOCAL(copyout_left_aligned_to_halfword)
	bb1	1,    SRC,  copyout_left_align_to_word
ASLOCAL(copyout_left_aligned_to_word)
	bb1	0,    LEN,  copyout_right_align_to_halfword
ASLOCAL(copyout_right_aligned_to_halfword)
	bb1	1,    LEN,  copyout_right_align_to_word
ASLOCAL(copyout_right_aligned_to_word)

	/*
	 * At this point, both SRC and DEST are aligned to a word
	 * boundary, and LEN is a multiple of 4. We want it an even
	 * multiple of 4.
	 */
	bb1.n	2,    LEN,  copyout_right_align_to_doubleword
	 or	%r7,  %r0,  4

ASLOCAL(copyout_right_aligned_to_doubleword)
	ld 	%r5,  SRC,  %r0
	ld    	%r6,  SRC,  %r7
	subu	LEN,  LEN,  8
#ifdef ERRATA__XXX_USR
	NOP
	st.usr	%r5,  DEST, %r0
	NOP
	NOP
	NOP
#else
	st.usr	%r5,  DEST, %r0
#endif
	addu	SRC,  SRC,  8
#ifdef ERRATA__XXX_USR
	NOP
	st.usr	%r6,  DEST, %r7
	NOP
	NOP
	NOP
#else
	st.usr	%r6,  DEST, %r7
#endif
	bcnd.n	ne0,  LEN,  copyout_right_aligned_to_doubleword
	 addu	DEST, DEST, 8
	or	%r2,  %r0,  %r0	/* successful return */
	br	Lcodone

	/***************************************************/
ASLOCAL(copyout_left_align_to_halfword)
	ld.b	%r5,  SRC,  %r0
	subu	LEN,  LEN,  1
#ifdef ERRATA__XXX_USR
	NOP
	st.b.usr	%r5,  DEST, %r0
	NOP
	NOP
	NOP
#else
	st.b.usr	%r5,  DEST, %r0
#endif
	addu	SRC,  SRC,  1
	br.n	copyout_left_aligned_to_halfword
	 addu	DEST, DEST, 1

ASLOCAL(copyout_left_align_to_word)
	ld.h	%r5,  SRC,  %r0
	subu	LEN,  LEN,  2
#ifdef ERRATA__XXX_USR
	NOP
	st.h.usr	%r5,  DEST, %r0
	NOP
	NOP
	NOP
#else
	st.h.usr	%r5,  DEST, %r0
#endif
	addu	SRC,  SRC,  2
	br.n	copyout_left_aligned_to_word
	 addu	DEST, DEST, 2

ASLOCAL(copyout_right_align_to_halfword)
	subu	LEN,  LEN,  1
	ld.b	%r5,  SRC,  LEN
#ifdef ERRATA__XXX_USR
	NOP
	st.b.usr	%r5,  DEST, LEN
	NOP
	NOP
	NOP
	br	copyout_right_aligned_to_halfword
#else
	br.n	copyout_right_aligned_to_halfword
	 st.b.usr	%r5,  DEST, LEN
#endif

ASLOCAL(copyout_right_align_to_word)
	subu	LEN,  LEN,  2
	ld.h	%r5,  SRC,  LEN
#ifdef ERRATA__XXX_USR
	NOP
	st.h.usr	%r5,  DEST, LEN
	NOP
	NOP
	NOP
	br	copyout_right_aligned_to_word
#else
	br.n	copyout_right_aligned_to_word
	 st.h.usr	%r5,  DEST, LEN
#endif

ASLOCAL(copyout_right_align_to_doubleword)
	subu	LEN,  LEN,  4
	ld	%r5,  SRC,  LEN
#ifdef ERRATA__XXX_USR
	NOP
	st.usr	%r5,  DEST, LEN
	NOP
	NOP
	NOP
	bcnd	ne0,  LEN, copyout_right_aligned_to_doubleword
#else
	bcnd.n	ne0,  LEN, copyout_right_aligned_to_doubleword
	 st.usr	%r5,  DEST, LEN
#endif
	br.n	Lcodone
	 or	%r2, %r0, %r0	/* successful return */

ASLOCAL(copyout_byte_only)
	bcnd	eq0, LEN, 2f
1:
	subu	LEN, LEN, 1
	ld.b	%r5, SRC, LEN
#ifdef ERRATA__XXX_USR
	NOP
	st.b.usr	%r5, DEST, LEN
	NOP
	NOP
	NOP
	bcnd	ne0, LEN, 1b
#else
	bcnd.n	ne0, LEN, 1b
	 st.b.usr	%r5, DEST, LEN
#endif

2:
	or	%r2, %r0, %r0	/* successful return */
	/* FALLTHROUGH */

ASLOCAL(Lcodone)
	ldcr	%r5,  CPU
	ld	%r6,  %r5,  CI_CURPCB
	jmp.n	%r1
	 st	%r0,  %r6,  PCB_ONFAULT	/* clear the handler */

ASLOCAL(Lcoflt)
	br.n	Lcodone
	 or	%r2, %r0, EFAULT	/* return fault */

#undef	SRC
#undef	DEST
#undef	LEN

/*
 * Copy a null terminated string from the kernel space to the user
 * address space.
 *
 * copyoutstr(from, to, maxlen, &lencopied)
 * r2 == from
 * r3 == to
 * r4 == maxlen that can be copied
 * r5 == len actually copied (including the terminating NUL!!!)
 */

#define	SRC	%r2
#define	DEST	%r3
#define	CNT	%r4
#define	LEN	%r5

ENTRY(copyoutstr)
	/* setup fault handler */
	ldcr	%r6,  CPU
	ld	%r7,  %r6,  CI_CURPCB
	or.u	%r6,  %r0,  %hi16(Lcosflt)
	or	%r6,  %r6,  %lo16(Lcosflt)
	st	%r6,  %r7,  PCB_ONFAULT
	bcnd	lt0,  CNT,  Lcosflt
	bcnd	eq0,  CNT,  2f
	or	%r6,  %r0,  0
1:
	ld.bu	%r7,  SRC,  %r6
#ifdef ERRATA__XXX_USR
	NOP
	st.b.usr	%r7,  DEST,  %r6
	NOP
	NOP
	NOP
#else
	st.b.usr	%r7,  DEST,  %r6
#endif
	bcnd.n	eq0,  %r7, 3f		/* all done */
	 addu	%r6,  %r6, 1
	cmp	%r7,  %r6, CNT
	bb1	lt,   %r7, 1b
2:
	br.n	Lcosdone
	 or	%r2,  %r0, ENAMETOOLONG
3:
	br.n	Lcosdone
	 or	%r2,  %r0, 0

ASLOCAL(Lcosflt)
	br.n	Lcosdone
	 or	%r2, %r0, EFAULT

ASLOCAL(Lcosdone)
	bcnd	eq0, LEN, 3f
	st	%r6, %r0, LEN
3:
	ldcr	%r5, CPU
	ld	%r6, %r5, CI_CURPCB
	jmp.n	%r1
	 st	%r0, %r6, PCB_ONFAULT	/* clear the handler */

#undef	SRC
#undef	DEST
#undef	CNT
#undef	LEN

/*######################################################################*/

/*
 * kcopy(const void *src, void *dst, size_t len);
 *
 * Copy len bytes from src to dst, aborting if we encounter a page fault.
 */
ENTRY(kcopy)
	subu	%r31, %r31, 16
	ldcr	%r5,  CPU
	st	%r1,  %r31, 4
	ld	%r6,  %r5,  CI_CURPCB
	or.u	%r8,  %r0,  %hi16(kcopy_fault)
	ld	%r7,  %r6,  PCB_ONFAULT
	or	%r8,  %r8,  %lo16(kcopy_fault)
	st	%r7,  %r31, 0			/* save old pcb_onfault */
	st	%r8,  %r6,  PCB_ONFAULT		/* pcb_onfault = kcopy_fault */

	bsr.n	bcopy
	 addu	%r1, %r1, 1f - . - 4
1:
	or	%r2,   %r0,  0		/* return success */

ASLOCAL(kcopy_out_fault)
	ld	%r1,  %r31, 4
	ldcr	%r5,  CPU
	ld	%r7,  %r31, 0
	ld	%r6,  %r5,  CI_CURPCB
	addu	%r31, %r31, 16
	jmp.n	%r1			/* all done, return to caller */
	 st	%r7,  %r6,  PCB_ONFAULT	/* restore previous pcb_onfault */

ASLOCAL(kcopy_fault)
	br.n	kcopy_out_fault
	 or	%r2,  %r0,  EFAULT	/* return fault */

#ifdef DDB
/*
 * non-local goto
 *	int setjmp(label_t *);
 *	void longjmp(label_t*);
 */
ENTRY(setjmp)
	st	%r1,  %r2, 0
	st	%r14, %r2, 4
	st	%r15, %r2, 2*4
	st	%r16, %r2, 3*4
	st	%r17, %r2, 4*4
	st	%r18, %r2, 5*4
	st	%r19, %r2, 6*4
	st	%r20, %r2, 7*4
	st	%r21, %r2, 8*4
	st	%r22, %r2, 9*4
	st	%r23, %r2, 10*4
	st	%r24, %r2, 11*4
	st	%r25, %r2, 12*4
	st	%r26, %r2, 13*4
	st	%r27, %r2, 14*4
	st	%r28, %r2, 15*4
	st	%r29, %r2, 16*4
	st	%r30, %r2, 17*4
	st	%r31, %r2, 18*4
	jmp.n	%r1
	 or	%r2,  %r0, %r0

ENTRY(longjmp)
	ld	%r1,  %r2, 0
	ld	%r14, %r2, 4
	ld	%r15, %r2, 2*4
	ld	%r16, %r2, 3*4
	ld	%r17, %r2, 4*4
	ld	%r18, %r2, 5*4
	ld	%r19, %r2, 6*4
	ld	%r20, %r2, 7*4
	ld	%r21, %r2, 8*4
	ld	%r22, %r2, 9*4
	ld	%r23, %r2, 10*4
	ld	%r24, %r2, 11*4
	ld	%r25, %r2, 12*4
	ld	%r26, %r2, 13*4
	ld	%r27, %r2, 14*4
	ld	%r28, %r2, 15*4
	ld	%r29, %r2, 16*4
	ld	%r30, %r2, 17*4
	ld	%r31, %r2, 18*4
	jmp.n	%r1
	 or	%r2,  %r0, 1
#endif

/*
 * Signal trampoline code.
 * The kernel arranges for the handler to be invoked directly, and return
 * here.
 */
	 .section .rodata
	 .align	3
	 .type	sigcode,@function
GLOBAL(sigcode)			/* r31 points to sigframe */
	ld	%r2,  %r31, 0	/* pick sigcontext* */
	or	%r13, %r0,  SYS_sigreturn
GLOBAL(sigcodecall)
GLOBAL(sigcoderet)
	tb0	0,    %r0,  450	/* syscall trap, calling sigreturn */
	NOP			| failure return
#ifdef dontbother		/* sigreturn will not return unless it fails */
	NOP			| success return
#endif
GLOBAL(esigcode)
	/* FALLTHROUGH */
GLOBAL(sigfill)
	tb0	0, %r0, 130	/* breakpoint */
GLOBAL(sigfillsiz)
	.word	sigfillsiz - sigfill

/*
 * Helper functions for pmap_copy_page() and pmap_zero_page().
 */

#ifdef M88100

/*
 * void copypage(vaddr_t src, vaddr_t dst);
 *
 * This copies PAGE_SIZE bytes from src to dst in 32 byte chunks.
 */
ENTRY(m8820x_copypage)
	addu	%r12, %r2, PAGE_SIZE
1:
	ld.d	%r4,  %r2, 0x00
	ld.d	%r6,  %r2, 0x08
	st.d	%r4,  %r3, 0x00
	ld.d	%r8,  %r2, 0x10
	st.d	%r6,  %r3, 0x08
	ld.d	%r10, %r2, 0x18
	st.d	%r8,  %r3, 0x10
	addu	%r2,  %r2, 0x20
	st.d	%r10, %r3, 0x18
	cmp	%r4,  %r2, %r12
	bb1.n	ne,   %r4, 1b
	 addu	%r3,  %r3, 0x20
	jmp	%r1

/*
 * void zeropage(vaddr_t dst);
 *
 * This zeroes PAGE_SIZE bytes from src to dst in 64 byte chunks.
 */
ENTRY(m8820x_zeropage)
	addu	%r12, %r2, PAGE_SIZE
	or	%r3,  %r1, %r0
	or	%r1,  %r0, %r0
1:
	st.d	%r0,  %r2, 0x00
	st.d	%r0,  %r2, 0x08
	st.d	%r0,  %r2, 0x10
	st.d	%r0,  %r2, 0x18
	st.d	%r0,  %r2, 0x20
	st.d	%r0,  %r2, 0x28
	st.d	%r0,  %r2, 0x30
	st.d	%r0,  %r2, 0x38
	addu	%r2,  %r2, 0x40
	cmp	%r4,  %r2, %r12
	bb1	ne,   %r4, 1b
	jmp	%r3

#endif	/* M88100 */

#ifdef M88110

/*
 * void copypage(vaddr_t src, vaddr_t dst);
 *
 * This copies PAGE_SIZE bytes from src to dst in 32 byte chunks (one
 * cache line).
 */
ENTRY(m88110_copypage)
	addu	%r12, %r2, PAGE_SIZE
1:
	ld.h	%r0,  %r3, 0x00	| load allocate
	ld.d	%r4,  %r2, 0x00
	ld.d	%r6,  %r2, 0x08
	st.d	%r4,  %r3, 0x00
	ld.d	%r8,  %r2, 0x10
	st.d	%r6,  %r3, 0x08
	ld.d	%r10, %r2, 0x18
	st.d	%r8,  %r3, 0x10
	addu	%r2,  %r2, 0x20
	st.d	%r10, %r3, 0x18
	cmp	%r4,  %r2, %r12
	addu	%r3,  %r3, 0x20
	bb1	ne,   %r4, 1b
	jmp	%r1

/*
 * void zeropage(vaddr_t dst);
 *
 * This zeroes PAGE_SIZE bytes from src to dst in 32 byte chunks.
 */
ENTRY(m88110_zeropage)
	addu	%r12, %r2, PAGE_SIZE
	or	%r3,  %r1, %r0
	or	%r1,  %r0, %r0
1:
	ld.h	%r0,  %r2, 0x00	| load allocate
	st.d	%r0,  %r2, 0x00
	st.d	%r0,  %r2, 0x08
	st.d	%r0,  %r2, 0x10
	st.d	%r0,  %r2, 0x18
	addu	%r2,  %r2, 0x20
	cmp	%r4,  %r2, %r12
	bb1	ne,   %r4, 1b
	jmp	%r3

#endif	/* M88110 */

/*
 * PSR initialization code, invoked from locore on every processor startup.
 */
ASENTRY(setup_psr)
	ldcr	%r2,  PID
	extu	%r3,  %r2, 8<8>

	/*
	 * Ensure that the PSR is as we like:
	 *	supervisor mode
	 *	big-endian byte ordering
	 *	concurrent operation allowed
	 *	carry bit clear (I don't think we really care about this)
	 *	FPU enabled
	 *	misaligned access raises an exception
	 *	interrupts disabled
	 *	shadow registers frozen
	 *
	 * The manual says not to disable interrupts and freeze shadowing
	 * at the same time because interrupts are not actually disabled
	 * until after the next instruction. Well, if an interrupt
	 * occurs now, we're in deep trouble anyway, so I'm going to do
	 * the two together.
	 *
	 * Upon a reset (or poweron, I guess), the PSR indicates:
	 *   supervisor mode
	 *   interrupts, shadowing, FPU, misaligned exception: all disabled
	 *
	 * We'll just construct our own turning on what we want.
	 *
	 *	jfriedl@omron.co.jp
	 */

	cmp	%r4, %r3, CPU_88110
	bb1	eq,  %r4, 1f	/* if it's a mc88110, skip SSBR */
	stcr	%r0, SSBR	/* clear this for later */
1:
	stcr	%r0, SR1	/* clear the CPU flags */

	or.u	%r2, %r0, %hi16(KERNEL_PSR)
	or	%r2, %r2, %lo16(KERNEL_PSR)
	stcr	%r2, PSR
	FLUSH_PIPELINE

	jmp	%r1

/*
 * Update the VBR value.
 * This needs to be done with interrupts and shadowing disabled.
 */
GLOBAL(set_vbr)
	ldcr	%r3, PSR
	set	%r4, %r3, 1<PSR_INTERRUPT_DISABLE_BIT>
	set	%r4, %r4, 1<PSR_SHADOW_FREEZE_BIT>
	stcr	%r4, PSR
	FLUSH_PIPELINE

	stcr	%r2, VBR
	FLUSH_PIPELINE

#if defined(M88100) && defined(M88110)
	ldcr	%r2, PID
	extu	%r5, %r2, 8<8>
	cmp	%r4, %r5, CPU_88110
	bb1	eq,  %r4, 1f
#endif
#ifdef M88100
	/* 88100 */
	stcr	%r3, PSR
	FLUSH_PIPELINE
	jmp	%r1
#endif
#ifdef M88110
1:
	/* 88110 */
	stcr	%r1, EXIP
	stcr	%r3, EPSR
	RTE
#endif
