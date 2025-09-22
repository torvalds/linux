/*	$OpenBSD: getopt.h,v 1.2 2015/09/19 04:02:21 guenther Exp $	*/
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

#ifndef _LIBC_GETOPT_H_
#define _LIBC_GETOPT_H_

#include_next <getopt.h>

#if 0
extern PROTO_NORMAL(opterr);
extern PROTO_NORMAL(optind);
extern PROTO_NORMAL(optopt);

/* alas, COMMON symbols alias differently */
extern PROTO_NORMAL(optarg);
extern PROTO_NORMAL(optreset);
#endif

PROTO_DEPRECATED(getopt);
PROTO_DEPRECATED(getopt_long);
PROTO_DEPRECATED(getopt_long_only);
 
#endif /* !_LIBC_GETOPT_H_ */
