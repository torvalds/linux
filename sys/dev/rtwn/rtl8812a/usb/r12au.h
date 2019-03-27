/*-
 * Copyright (c) 2016 Andriy Voskoboinyk <avos@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef RTL8812AU_H
#define RTL8812AU_H

#include <dev/rtwn/rtl8812a/r12a.h>


/*
 * Function declarations.
 */
/* r12au_init.c */
void	r12au_init_rx_agg(struct rtwn_softc *);
void	r12au_init_burstlen_usb2(struct rtwn_softc *);
void	r12au_init_burstlen(struct rtwn_softc *);
void	r12au_init_ampdu_fwhw(struct rtwn_softc *);
void	r12au_init_ampdu(struct rtwn_softc *);
void	r12au_post_init(struct rtwn_softc *);

/* r12au_rx.c */
int	r12au_classify_intr(struct rtwn_softc *, void *, int);
int	r12au_align_rx(int, int);

/* r12au_tx.c */
void	r12au_dump_tx_desc(struct rtwn_softc *, const void *);

#endif	/* RTL8812AU_H */
