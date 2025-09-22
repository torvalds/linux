/*	$OpenBSD: if_ether.h,v 1.1 2015/09/14 11:01:47 guenther Exp $	*/
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

#ifndef _LIBC_NETINET_IF_ETHER_H_
#define _LIBC_NETINET_IF_ETHER_H_

#include_next <netinet/if_ether.h>

PROTO_DEPRECATED(ether_aton);
PROTO_DEPRECATED(ether_hostton);
PROTO_NORMAL(ether_line);
PROTO_DEPRECATED(ether_ntoa);
PROTO_DEPRECATED(ether_ntohost);

#endif /* _LIBC_NETINET_IF_ETHER_H_ */
