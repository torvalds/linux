/*	$OpenBSD: if_rtwnreg.h,v 1.3 2015/06/14 08:02:47 stsp Exp $	*/

/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2015 Stefan Sperling <stsp@openbsd.org>
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

#ifndef RTL8192CE_H
#define RTL8192CE_H

#include <dev/rtwn/rtl8192c/r92c.h>


/*
 * Global definitions.
 */
#define R92CE_PUBQ_NPAGES	176
#define R92CE_HPQ_NPAGES	41
#define R92CE_LPQ_NPAGES	28
#define R92CE_TX_PAGE_COUNT	\
	(R92CE_PUBQ_NPAGES + R92CE_HPQ_NPAGES + R92CE_LPQ_NPAGES)


/*
 * Function declarations.
 */
/* r92ce_calib.c */
void	r92ce_iq_calib(struct rtwn_softc *);

/* r92ce_fw.c */
#ifndef RTWN_WITHOUT_UCODE
void	r92ce_fw_reset(struct rtwn_softc *, int);
#endif

/* r92ce_init.c */
void	r92ce_init_intr(struct rtwn_softc *);
void	r92ce_init_edca(struct rtwn_softc *);
void	r92ce_init_bb(struct rtwn_softc *);
int	r92ce_power_on(struct rtwn_softc *);
void	r92ce_power_off(struct rtwn_softc *);
void	r92ce_init_ampdu(struct rtwn_softc *);
void	r92ce_post_init(struct rtwn_softc *);

/* r92ce_led.c */
void	r92ce_set_led(struct rtwn_softc *, int, int);

/* r92ce_rx.c */
int	r92ce_get_intr_status(struct rtwn_pci_softc *, int *);
void	r92ce_enable_intr(struct rtwn_pci_softc *);
void	r92ce_start_xfers(struct rtwn_softc *);

/* r92ce_tx.c */
void	r92ce_setup_tx_desc(struct rtwn_pci_softc *, void *, uint32_t);
void	r92ce_tx_postsetup(struct rtwn_pci_softc *, void *,
	    bus_dma_segment_t[]);
void	r92ce_copy_tx_desc(void *, const void *);
void	r92ce_dump_tx_desc(struct rtwn_softc *, const void *);

#endif	/* RTL8192CE_H */
