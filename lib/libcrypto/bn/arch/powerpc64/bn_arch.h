/*	$OpenBSD: bn_arch.h,v 1.4 2023/02/16 10:41:03 jsing Exp $ */
/*
 * Copyright (c) 2023 Joel Sing <jsing@openbsd.org>
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

#ifndef HEADER_BN_ARCH_H
#define HEADER_BN_ARCH_H

#ifndef OPENSSL_NO_ASM

#if 0 /* Needs testing and enabling. */
#if defined(__GNUC__)
#define HAVE_BN_MULW

static inline void
bn_mulw(BN_ULONG a, BN_ULONG b, BN_ULONG *out_r1, BN_ULONG *out_r0)
{
	BN_ULONG r1, r0;

	/* Unsigned multiplication using a mulhdu/mul pair. */
	__asm__ ("mulhdu %0, %2, %3; mul %1, %2, %3"
	    : "=&r"(r1), "=r"(r0)
	    : "r"(a), "r"(b));

	*out_r1 = r1;
	*out_r0 = r0;
}
#endif /* __GNUC__ */
#endif

#endif
#endif
