/*	$OpenBSD: tcb.h,v 1.3 2021/05/12 01:20:52 jsg Exp $	*/

/*
 * Copyright (c) 2019 Brian Bamsch <bbamsch@google.com>
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

#include <machine/pcb.h>

#define TCB_GET(p)		\
	((void *)(((struct pcb *)(p)->p_addr)->pcb_tf->tf_tp))

#define TCB_SET(p, addr)						\
	do {								\
		((struct pcb *)(p)->p_addr)->pcb_tf->tf_tp = (long)(addr); \
	} while (0)

#else /* _KERNEL */

/* ELF TLS ABI calls for small TCB, with static TLS data after it */
#define TLS_VARIANT	1

static inline void *
__riscv64_read_tcb(void)
{
	void *tcb;
	__asm volatile("mv %0, tp": "=r" (tcb));
	return tcb;
}

#define TCB_GET()		__riscv64_read_tcb()

#endif /* _KERNEL */

#endif /* _MACHINE_TCB_H_ */
