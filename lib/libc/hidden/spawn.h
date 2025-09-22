/*	$OpenBSD: spawn.h,v 1.1 2015/10/04 07:57:21 guenther Exp $	*/
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

#ifndef _LIBC_SPAWN_H_
#define _LIBC_SPAWN_H_

#include_next <spawn.h>

PROTO_DEPRECATED(posix_spawn);
PROTO_DEPRECATED(posix_spawn_file_actions_addclose);
PROTO_DEPRECATED(posix_spawn_file_actions_adddup2);
PROTO_DEPRECATED(posix_spawn_file_actions_addopen);
PROTO_DEPRECATED(posix_spawn_file_actions_destroy);
PROTO_DEPRECATED(posix_spawn_file_actions_init);
PROTO_DEPRECATED(posix_spawnattr_destroy);
PROTO_DEPRECATED(posix_spawnattr_getflags);
PROTO_DEPRECATED(posix_spawnattr_getpgroup);
PROTO_DEPRECATED(posix_spawnattr_getschedparam);
PROTO_DEPRECATED(posix_spawnattr_getschedpolicy);
PROTO_DEPRECATED(posix_spawnattr_getsigdefault);
PROTO_DEPRECATED(posix_spawnattr_getsigmask);
PROTO_DEPRECATED(posix_spawnattr_init);
PROTO_DEPRECATED(posix_spawnattr_setflags);
PROTO_DEPRECATED(posix_spawnattr_setpgroup);
PROTO_DEPRECATED(posix_spawnattr_setschedparam);
PROTO_DEPRECATED(posix_spawnattr_setschedpolicy);
PROTO_DEPRECATED(posix_spawnattr_setsigdefault);
PROTO_DEPRECATED(posix_spawnattr_setsigmask);
PROTO_DEPRECATED(posix_spawnp);

#endif /* !_LIBC_SPAWN_H_ */
