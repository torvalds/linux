/*	$OpenBSD: tcb.h,v 1.4 2016/09/04 08:49:35 guenther Exp $	*/
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

#ifdef _KERNEL

#include <machine/sysarch.h>
#include <machine/pcb.h>

#define TCB_GET(p)		\
	((void *)i386_get_threadbase(p, TSEG_GS))
#define TCB_SET(p, addr)	\
	i386_set_threadbase(p, (uint32_t)(addr), TSEG_GS)

#else /* _KERNEL */

/* ELF TLS ABI calls for big TCB, with static TLS data at negative offsets */
#define TLS_VARIANT	2

/* Read a slot from the TCB */
static inline void *
__i386_read_tcb(int offset)
{
	void	*val;
	__asm__ ("movl %%gs:(%1),%0" : "=r" (val) : "r" (offset));
	return val;
}

/* Get a pointer to the TCB itself */
#define TCB_GET()		__i386_read_tcb(0)

/* Setting the TCB pointer can only be done via syscall, so no TCB_SET() */

#endif /* _KERNEL */
#endif /* _MACHINE_TCB_H_ */
