/*	$OpenBSD: dirent.h,v 1.2 2024/04/15 15:47:58 florian Exp $	*/
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

#ifndef _LIBC_DIRENT_H_
#define	_LIBC_DIRENT_H_

#include_next <dirent.h>

__BEGIN_HIDDEN_DECLS
DIR	*__fdopendir(int fd);
__END_HIDDEN_DECLS

PROTO_DEPRECATED(alphasort);
PROTO_NORMAL(closedir);
PROTO_NORMAL(dirfd);
PROTO_NORMAL(fdopendir);
PROTO_NORMAL(getdents);
PROTO_NORMAL(opendir);
PROTO_NORMAL(readdir);
PROTO_DEPRECATED(readdir_r);
PROTO_DEPRECATED(rewinddir);
PROTO_DEPRECATED(scandir);
PROTO_DEPRECATED(scandirat);
PROTO_NORMAL(seekdir);
PROTO_NORMAL(telldir);

#endif /* !_LIBC_DIRENT_H_ */
