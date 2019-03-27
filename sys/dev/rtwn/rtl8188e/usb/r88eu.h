/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
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
 * $OpenBSD: if_urtwnreg.h,v 1.3 2010/11/16 18:02:59 damien Exp $
 * $FreeBSD$
 */

#ifndef RTL8188EU_H
#define RTL8188EU_H

#include <dev/rtwn/rtl8188e/r88e.h>


/*
 * Global definitions.
 */
#define R88EU_PUBQ_NPAGES	142
#define R88EU_TX_PAGE_COUNT	169


/*
 * Function declarations.
 */
/* r88eu_init.c */
void	r88eu_init_bb(struct rtwn_softc *);
int	r88eu_power_on(struct rtwn_softc *);
void	r88eu_power_off(struct rtwn_softc *);
void	r88eu_init_intr(struct rtwn_softc *);
void	r88eu_init_rx_agg(struct rtwn_softc *);
void	r88eu_post_init(struct rtwn_softc *);

#endif	/* RTL8188EU_H */
