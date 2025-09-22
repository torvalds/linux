/*	$OpenBSD: arc4random.h,v 1.4 2015/01/15 06:57:18 deraadt Exp $	*/

/*
 * Copyright (c) 1996, David Mazieres <dm@uun.org>
 * Copyright (c) 2008, Damien Miller <djm@openbsd.org>
 * Copyright (c) 2013, Markus Friedl <markus@openbsd.org>
 * Copyright (c) 2014, Theo de Raadt <deraadt@openbsd.org>
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
 * Stub functions for portability.
 */
#include <sys/mman.h>

#include <signal.h>

#include "thread_private.h"

static inline void
_getentropy_fail(void)
{
	raise(SIGKILL);
}

static inline int
_rs_allocate(struct _rs **rsp, struct _rsx **rsxp)
{
	struct {
		struct _rs rs;
		struct _rsx rsx;
	} *p;

	if ((p = mmap(NULL, sizeof(*p), PROT_READ|PROT_WRITE,
	    MAP_ANON|MAP_PRIVATE, -1, 0)) == MAP_FAILED)
		return (-1);
	if (minherit(p, sizeof(*p), MAP_INHERIT_ZERO) == -1) {
		munmap(p, sizeof(*p));
		return (-1);
	}

	*rsp = &p->rs;
	*rsxp = &p->rsx;
	return (0);
}

static inline void
_rs_forkdetect(void)
{
}
