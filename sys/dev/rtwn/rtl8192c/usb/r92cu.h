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

#ifndef RTL8192CU_H
#define RTL8192CU_H

#include <dev/rtwn/rtl8192c/r92c.h>


/*
 * Global definitions.
 */
#define R92CU_PUBQ_NPAGES	231
#define R92CU_TX_PAGE_COUNT	248


/*
 * Function declarations.
 */
/* r92cu_init.c */
void	r92cu_init_bb(struct rtwn_softc *);
int	r92cu_power_on(struct rtwn_softc *);
void	r92cu_power_off(struct rtwn_softc *);
void	r92cu_init_intr(struct rtwn_softc *);
void	r92cu_init_tx_agg(struct rtwn_softc *);
void	r92cu_init_rx_agg(struct rtwn_softc *);
void	r92cu_post_init(struct rtwn_softc *);

/* r92cu_led.c */
void	r92cu_set_led(struct rtwn_softc *, int, int);

/* r92cu_rx.c */
int	r92cu_align_rx(int, int);

/* r92cu_tx.c */
void	r92cu_dump_tx_desc(struct rtwn_softc *, const void *);

#endif	/* RTL8192CU_H */
