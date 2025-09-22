/*	$OpenBSD: rfc3779.c,v 1.2 2023/10/18 06:30:40 tb Exp $ */
/*
 * Copyright (c) 2021 Theo Buehler <tb@openbsd.org>
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

#include <stddef.h>

#include <openssl/asn1.h>
#include <openssl/x509v3.h>

#include "extern.h"

/*
 * These should really have been part of the public OpenSSL RFC 3779 API...
 */

IPAddrBlocks *
IPAddrBlocks_new(void)
{
	IPAddrBlocks *addrs;

	/*
	 * XXX The comparison function IPAddressFamily_cmp() isn't public.
	 * Install it using a side effect of the lovely X509v3_addr_canonize().
	 */
	if ((addrs = sk_IPAddressFamily_new_null()) == NULL)
		return NULL;
	if (!X509v3_addr_canonize(addrs)) {
		IPAddrBlocks_free(addrs);
		return NULL;
	}

	return addrs;
}

void
IPAddrBlocks_free(IPAddrBlocks *addr)
{
	sk_IPAddressFamily_pop_free(addr, IPAddressFamily_free);
}
