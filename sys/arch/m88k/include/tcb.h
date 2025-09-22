/*	$OpenBSD: tcb.h,v 1.6 2019/12/11 07:21:40 guenther Exp $	*/

/*
 * Copyright (c) 2011 Philip Guenther <guenther@openbsd.org>
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

#ifndef _MACHINE_TCB_H_
#define _MACHINE_TCB_H_

/*
 * In userspace, register %r27 contains the address of the thread's TCB.
 * It is the responsibility of the kernel to set %r27 to the proper value
 * when creating the thread.
 */

#ifdef _KERNEL

#include <machine/reg.h>

#define TCB_GET(p)		\
	((void *)(p)->p_md.md_tf->tf_r[27])
#define TCB_SET(p, addr)	\
	((p)->p_md.md_tf->tf_r[27] = (register_t)(addr))

#else /* _KERNEL */

/*
 * It is unknown whether the m88k ELF ABI mentions TLS. On m88k, since only
 * unsigned offsets in (register + immediate offset) addressing is supported
 * on all processors, it makes sense to use a small TCB, with static TLS data
 * after it.
 */
#define TLS_VARIANT	1

#if defined(__GNUC__) && __GNUC__ >= 4

register void *__tcb __asm__ ("%r27");
#define	TCB_GET()	(__tcb)
#define	TCB_SET(tcb)	((__tcb) = (tcb))

#else /* __GNUC__ >= 4 */

/* Get a pointer to the TCB itself */
static inline void *
__m88k_get_tcb(void)
{
	void *val;
	__asm__ ("or %0,%%r27,%%r0" : "=r" (val));
	return val;
}

#define TCB_GET()	__m88k_get_tcb()
#define TCB_SET(tcb)	__asm volatile("or %%r27,%0,%r0" : : "r" (tcb))

#endif /* __GNUC__ >= 4 */

#endif /* _KERNEL */
#endif /* _MACHINE_TCB_H_ */
