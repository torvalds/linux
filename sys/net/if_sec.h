/*	$OpenBSD: if_sec.h,v 1.2 2025/03/04 15:11:30 bluhm Exp $ */

/*
 * Copyright (c) 2023 David Gwynne <dlg@openbsd.org>
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

#ifndef _NET_IF_SEC_H
#define _NET_IF_SEC_H

#ifdef _KERNEL
struct sec_softc;
struct tdb;

/*
 * let the IPsec stack hand packets to sec(4) for input
 */

struct sec_softc	*sec_get(unsigned int);
void			 sec_input(struct sec_softc * , int, int,
			     struct mbuf *, struct netstack *);
void			 sec_put(struct sec_softc *);

/*
 * let the IPsec stack give tdbs to sec(4) for output
 */

void			 sec_tdb_insert(struct tdb *);
void			 sec_tdb_remove(struct tdb *);

#endif /* _KERNEL */

#endif /* _NET_IF_SEC_H */
