/*	$OpenBSD: sysarch.h,v 1.1 2021/09/14 12:03:49 jca Exp $	*/

/*
 * Copyright (c) 2021 Jeremie Courreges-Anglas <jca@openbsd.org>
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

#ifndef	_RISCV64_SYSARCH_H_
#define	_RISCV64_SYSARCH_H_

/*
 * Architecture specific syscalls (riscv64)
 */

#define	RISCV_SYNC_ICACHE	0

struct riscv_sync_icache_args {
	u_int64_t	addr;		/* Virtual start address */
	size_t		len;		/* Region size */
};

#ifndef _KERNEL

#include <sys/cdefs.h>

__BEGIN_DECLS
int	sysarch(int, void *);
__END_DECLS

#endif	/* _KERNEL */

#endif	/* _RISCV64_SYSARCH_H_ */
