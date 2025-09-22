/*	$OpenBSD: if_enc.h,v 1.13 2021/12/01 21:48:00 deraadt Exp $	*/

/*
 * Copyright (c) 2010 Reyk Floeter <reyk@vantronix.net>
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

#ifndef _NET_ENC_H
#define _NET_ENC_H

#define ENCMTU		1536		/* XXX should be bigger, maybe LOMTU */
#define ENC_HDRLEN	12

struct enchdr {
	u_int32_t af;
	u_int32_t spi;
	u_int32_t flags;	/* similar to mbuf m_flags */
};

#ifdef _KERNEL
struct enc_softc {
	struct ifnet		 sc_if;		/* virtual interface */
	u_int			 sc_unit;
	struct ifaddr		 sc_ifa;	/* needed to attach rtentry */
};

struct ifnet	*enc_getif(u_int, u_int);
struct ifaddr	*enc_getifa(u_int, u_int);
#endif /* _KERNEL */

#endif /* _NET_ENC_H */
