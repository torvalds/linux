/*	$OpenBSD: radius_subr.h,v 1.1 2024/07/14 15:31:49 yasuoka Exp $	*/

/*
 * Copyright (c) 2013, 2023 Internet Initiative Japan Inc.
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
#ifndef RADIUS_UTIL_H
#define RADIUS_UTIL_H 1

#include <sys/types.h>

__BEGIN_DECLS
void	 radius_attr_hide(const char *, const char *, const u_char *, u_char *,
	    int);
void	 radius_attr_unhide(const char *, const char *, const u_char *,
	    u_char *, int);

__END_DECLS
#endif
