/*	$OpenBSD: sysarch.h,v 1.2 2012/12/05 23:20:13 deraadt Exp $	*/

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

#ifndef	_MIPS64_SYSARCH_H_
#define	_MIPS64_SYSARCH_H_

/*
 * Architecture specific syscalls (mips64)
 */

#define	MIPS64_CACHEFLUSH	0

/*
 * Argument structure and defines to mimic IRIX cacheflush() system call
 */

struct	mips64_cacheflush_args {
	vaddr_t	va;
	size_t	sz;
	int	which;
#define	ICACHE	0x01
#define	DCACHE	0x02
#define	BCACHE	(ICACHE | DCACHE)
};

#ifndef _KERNEL

#include <sys/cdefs.h>

__BEGIN_DECLS
int	cacheflush(void *, int, int);
int	_flush_cache(char *, int, int);
int	sysarch(int, void *);
__END_DECLS
#endif	/* _KERNEL */

#endif	/* _MIPS64_SYSARCH_H_ */
