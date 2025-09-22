/*	$OpenBSD: tcb.h,v 1.7 2017/04/20 10:03:40 kettenis Exp $	*/

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

#include <machine/reg.h>

/*
 * In userspace, register %g7 contains the address of the thread's TCB
 */
#define TCB_GET(p)		\
	((void *)(p)->p_md.md_tf->tf_global[7])
#define TCB_SET(p, addr)	\
	((p)->p_md.md_tf->tf_global[7] = (int64_t)(addr))

#else /* _KERNEL */

/* ELF TLS ABI calls for big TCB, with static TLS data at negative offsets */
#define TLS_VARIANT	2

register void *__tcb __asm__ ("g7");
#define TCB_GET()		(__tcb)
#define TCB_SET(tcb)		((__tcb) = (tcb))

#endif /* _KERNEL */
#endif /* _MACHINE_TCB_H_ */
