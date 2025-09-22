/*	$OpenBSD: mman.h,v 1.5 2022/10/07 15:21:04 deraadt Exp $	*/
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

#ifndef _LIBC_SYS_MMAN_H_
#define _LIBC_SYS_MMAN_H_

#include_next <sys/mman.h>

PROTO_NORMAL(madvise);
PROTO_NORMAL(minherit);
PROTO_NORMAL(mlock);
PROTO_NORMAL(mlockall);
PROTO_NORMAL(mmap);
PROTO_NORMAL(mprotect);
PROTO_NORMAL(mimmutable);
PROTO_NORMAL(mquery);
PROTO_CANCEL(msync);
PROTO_NORMAL(munlock);
PROTO_NORMAL(munlockall);
PROTO_NORMAL(munmap);
PROTO_DEPRECATED(posix_madvise);
PROTO_DEPRECATED(shm_mkstemp);
PROTO_NORMAL(shm_open);
PROTO_DEPRECATED(shm_unlink);

#endif /* _LIBC_SYS_MMAN_H_ */
