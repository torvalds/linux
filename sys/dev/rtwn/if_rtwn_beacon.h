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

#ifndef IF_RTWN_BEACON_H
#define IF_RTWN_BEACON_H

void	rtwn_switch_bcnq(struct rtwn_softc *, int);
int	rtwn_setup_beacon(struct rtwn_softc *, struct ieee80211_node *);
void	rtwn_update_beacon(struct ieee80211vap *, int);
void	rtwn_tx_beacon_csa(void *arg, int);
int	rtwn_tx_beacon_check(struct rtwn_softc *, struct rtwn_vap *);

#endif	/* IF_RTWN_BEACON_H */
