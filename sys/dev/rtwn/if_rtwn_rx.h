/*-
 * Copyright (c) 2015-2016 Andriy Voskoboinyk <avos@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

#ifndef IF_RTWN_RX_H
#define IF_RTWN_RX_H

#define	RTWN_NOISE_FLOOR	-95


void	rtwn_get_rates(struct rtwn_softc *, const struct ieee80211_rateset *,
	    const struct ieee80211_htrateset *, uint32_t *, int *, int);
void	rtwn_set_basicrates(struct rtwn_softc *, uint32_t);
struct ieee80211_node *	rtwn_rx_common(struct rtwn_softc *, struct mbuf *,
	    void *);
void	rtwn_adhoc_recv_mgmt(struct ieee80211_node *, struct mbuf *, int,
	    const struct ieee80211_rx_stats *, int, int);
void	rtwn_set_multi(struct rtwn_softc *);
void	rtwn_rxfilter_update(struct rtwn_softc *);
void	rtwn_rxfilter_init(struct rtwn_softc *);
void	rtwn_rxfilter_set(struct rtwn_softc *);
void	rtwn_set_rx_bssid_all(struct rtwn_softc *, int);
void	rtwn_set_promisc(struct rtwn_softc *);

#endif	/* IF_RTWN_RX_H */
