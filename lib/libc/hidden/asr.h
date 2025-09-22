/*	$OpenBSD: asr.h,v 1.4 2019/10/24 05:57:42 otto Exp $	*/
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

#ifndef _LIBC_ASR_H_
#define _LIBC_ASR_H_

#include_next <asr.h>

PROTO_DEPRECATED(asr_abort);
PROTO_NORMAL(asr_resolver_from_string);
PROTO_NORMAL(asr_resolver_free);
PROTO_NORMAL(asr_run);
PROTO_NORMAL(asr_run_sync);
PROTO_NORMAL(getaddrinfo_async);
PROTO_NORMAL(gethostbyaddr_async);
PROTO_NORMAL(gethostbyname2_async);
PROTO_NORMAL(gethostbyname_async);
PROTO_NORMAL(getnameinfo_async);
PROTO_NORMAL(getnetbyaddr_async);
PROTO_NORMAL(getnetbyname_async);
PROTO_NORMAL(getrrsetbyname_async);
PROTO_NORMAL(res_query_async);
PROTO_NORMAL(res_search_async);
PROTO_NORMAL(res_send_async);

#endif	/* _LIBC_ASR_H_ */
