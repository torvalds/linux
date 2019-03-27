/*-
 * Copyright (c) 2018 Farhan Khan <khanzf@gmail.com>
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

#ifndef RTL8188EE_H
#define RTL8188EE_H

#include <dev/rtwn/rtl8188e/r88e.h>


/*
 * Global definitions.
 */
#define R88EE_PUBQ_NPAGES	115
#define R88EE_HPQ_NPAGES	41
#define R88EE_NPQ_NPAGES	1
#define R88EE_LPQ_NPAGES	13
#define R88EE_TX_PAGE_COUNT	\
	(R88EE_PUBQ_NPAGES + R88EE_HPQ_NPAGES + \
	 R88EE_NPQ_NPAGES + R88EE_LPQ_NPAGES)


/*
 * Function declarations.
 */
/* r88ee_init.c */
void	r88ee_init_bb(struct rtwn_softc *);
void	r88ee_init_intr(struct rtwn_softc *);
int	r88ee_power_on(struct rtwn_softc *);
void	r88ee_power_off(struct rtwn_softc *);

/* r88ee_rx.c */
int	r88ee_get_intr_status(struct rtwn_pci_softc *, int *);
void	r88ee_enable_intr(struct rtwn_pci_softc *);
void	r88ee_start_xfers(struct rtwn_softc *);
void	r88ee_post_init(struct rtwn_softc *);

#endif	/* RTL8188EE_H */

