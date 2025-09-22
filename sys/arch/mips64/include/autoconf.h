/*	$OpenBSD: autoconf.h,v 1.2 2017/06/08 12:02:52 visa Exp $	*/

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
 * Common defines used by autoconf on all mips64-based platforms.
 */

#ifndef	_MIPS64_AUTOCONF_H_
#define	_MIPS64_AUTOCONF_H_

#include <machine/cpu.h>		/* for struct cpu_hwinfo */

struct cpu_attach_args {
	struct mainbus_attach_args	 caa_maa;
	struct cpu_hwinfo		*caa_hw;
};

extern struct cpu_hwinfo bootcpu_hwinfo;

void	unmap_startup(void);

#endif /* _MIPS64_AUTOCONF_H_ */
