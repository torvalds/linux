/*-
 * Copyright (c) 2016 Andriy Voskoboinyk <avos@FreeBSD.org>
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

#ifndef IF_RTWN_NOP_H
#define IF_RTWN_NOP_H

static __inline void
rtwn_nop_softc(struct rtwn_softc *sc)
{
}

static __inline int
rtwn_nop_int_softc(struct rtwn_softc *sc)
{
	return (0);
}

static __inline int
rtwn_nop_int_softc_mbuf(struct rtwn_softc *sc, struct mbuf *m)
{
	return (0);
}

static __inline void
rtwn_nop_softc_int(struct rtwn_softc *sc, int id)
{
}

static __inline void
rtwn_nop_softc_uint32(struct rtwn_softc *sc, uint32_t reg)
{
}

static __inline void
rtwn_nop_softc_chan(struct rtwn_softc *sc, struct ieee80211_channel *c)
{
}

static __inline void
rtwn_nop_softc_vap(struct rtwn_softc *sc, struct ieee80211vap *vap)
{
}

static __inline void
rtwn_nop_softc_uint8_int(struct rtwn_softc *sc, uint8_t *buf, int len)
{
}

static __inline void
rtwn_nop_void_int(void *buf, int is5ghz)
{
}

#endif	/* IF_RTWN_NOP_H */
