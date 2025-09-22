/*
 * Copyright (c) 2015 Kazuya GODA <goda@openbsd.org>
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

#ifndef _NET_IF_ETHERIP_H_
#define _NET_IF_ETHERIP_H_

int etherip_sysctl(int *, uint, void *, size_t *, void *, size_t);
int ip_etherip_output(struct ifnet *, struct mbuf *);
int ip_etherip_input(struct mbuf **, int *, int, int, struct netstack *);

#ifdef INET6
int ip6_etherip_output(struct ifnet *, struct mbuf *);
int ip6_etherip_input(struct mbuf **, int *, int, int, struct netstack *);
#endif /* INET6 */

#endif /* _NET_IF_ETHERIP_H_ */
