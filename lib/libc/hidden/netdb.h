/*	$OpenBSD: netdb.h,v 1.2 2015/09/14 07:38:38 guenther Exp $	*/
/*
 * Copyright (c) 2015 Theo de Raadt <deraadt@openbsd.org>
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

#ifndef _LIBC_NETDB_H
#define _LIBC_NETDB_H

#include_next <netdb.h>

__BEGIN_HIDDEN_DECLS
extern int _net_stayopen;
extern struct protoent_data _protoent_data;
extern struct servent_data _servent_data;
__END_HIDDEN_DECLS

PROTO_DEPRECATED(endhostent);
PROTO_DEPRECATED(endnetent);
PROTO_DEPRECATED(endprotoent);
PROTO_NORMAL(endprotoent_r);
PROTO_DEPRECATED(endservent);
PROTO_NORMAL(endservent_r);
PROTO_NORMAL(freeaddrinfo);
PROTO_NORMAL(freerrset);
PROTO_NORMAL(gai_strerror);
PROTO_NORMAL(getaddrinfo);
PROTO_DEPRECATED(gethostbyaddr);
PROTO_NORMAL(gethostbyname);
PROTO_NORMAL(gethostbyname2);
PROTO_DEPRECATED(gethostent);
PROTO_NORMAL(getnameinfo);
PROTO_DEPRECATED(getnetbyaddr);
PROTO_DEPRECATED(getnetbyname);
PROTO_DEPRECATED(getnetent);
PROTO_NORMAL(getprotobyname);
PROTO_NORMAL(getprotobyname_r);
PROTO_DEPRECATED(getprotobynumber);
PROTO_NORMAL(getprotobynumber_r);
PROTO_DEPRECATED(getprotoent);
PROTO_NORMAL(getprotoent_r);
PROTO_DEPRECATED(getrrsetbyname);
PROTO_NORMAL(getservbyname);
PROTO_NORMAL(getservbyname_r);
PROTO_DEPRECATED(getservbyport);
PROTO_NORMAL(getservbyport_r);
PROTO_DEPRECATED(getservent);
PROTO_NORMAL(getservent_r);
PROTO_DEPRECATED(herror);
PROTO_NORMAL(hstrerror);
PROTO_DEPRECATED(sethostent);
PROTO_DEPRECATED(setnetent);
PROTO_DEPRECATED(setprotoent);
PROTO_NORMAL(setprotoent_r);
PROTO_DEPRECATED(setservent);
PROTO_NORMAL(setservent_r);

#endif	/* !_LIBC_NETDB_H */
