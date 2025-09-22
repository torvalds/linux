/*	$OpenBSD: inet.h,v 1.1 2015/09/13 21:36:08 guenther Exp $	*/
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

#ifndef _LIBC_INET_H_
#define	_LIBC_INET_H_

#include_next <arpa/inet.h>

PROTO_DEPRECATED(inet_addr);
PROTO_NORMAL(inet_aton);
PROTO_DEPRECATED(inet_lnaof);
PROTO_DEPRECATED(inet_makeaddr);
PROTO_DEPRECATED(inet_net_ntop);
PROTO_DEPRECATED(inet_net_pton);
PROTO_DEPRECATED(inet_neta);
PROTO_DEPRECATED(inet_netof);
PROTO_NORMAL(inet_network);
PROTO_DEPRECATED(inet_ntoa);
PROTO_NORMAL(inet_ntop);
PROTO_NORMAL(inet_pton);

#endif /* !_LIBC_INET_H_ */
