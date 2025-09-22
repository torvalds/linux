/*	$OpenBSD: tib.h,v 1.4 2022/12/27 17:10:06 jmc Exp $	*/
/*
 * Copyright (c) 2015 Philip Guenther <guenther@openbsd.org>
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

#ifndef	_LIBC_TIB_H_
#define	_LIBC_TIB_H_

#include_next <tib.h>

__BEGIN_HIDDEN_DECLS

#ifndef PIC
/*
 * Handling for static TLS allocation in statically linked programs
 */
/* Given the base of a TIB allocation, initialize the static TLS for a thread */
struct tib *_static_tls_init(char *_base, void *_thread);

/* size of static TLS allocation */
extern size_t	_static_tls_size;

/* alignment of static TLS allocation */
extern int	_static_tls_align;

/* base-offset alignment of static TLS allocation */
extern int	_static_tls_align_offset;
#endif

/* saved handle to callbacks from ld.so */
extern const dl_cb *_dl_cb;

#if ! TCB_HAVE_MD_GET
/*
 * For archs without a fast TCB_GET(): the pointer to the TCB in
 * single-threaded programs, whether linked statically or dynamically.
 */
extern void	*_libc_single_tcb;
#endif

__END_HIDDEN_DECLS


PROTO_NORMAL(__get_tcb);
PROTO_NORMAL(__set_tcb);

#endif /* _LIBC_TIB_H_ */
