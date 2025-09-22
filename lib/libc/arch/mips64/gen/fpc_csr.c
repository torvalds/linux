/*	$OpenBSD: fpc_csr.c,v 1.1 2010/09/24 13:54:06 miod Exp $	*/

/*
 * Copyright (c) 2010 Miodrag Vallat.
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
 * IRIX-compatible get_fpc_csr() and set_fpc_csr() functions
 */

#include <sys/types.h>
#include <machine/fpu.h>

int
get_fpc_csr()
{
	int32_t csr;

	__asm__("cfc1 %0,$31" : "=r" (csr));
	return csr;
}

int
set_fpc_csr(int csr)
{
	int32_t oldcsr;

	__asm__("cfc1 %0,$31" : "=r" (oldcsr));
	__asm__("ctc1 %0,$31" :: "r" (csr));

	return oldcsr;
}
