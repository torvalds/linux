/*-
 * Copyright (c) 2017 Kevin Lo <kevlo@FreeBSD.org>
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

#ifndef RTL8192E_H
#define RTL8192E_H

/*
 * Global definitions.
 */
#define R92E_PUBQ_NPAGES	222
#define R92E_TX_PAGE_COUNT	243

#define R92E_TX_PAGE_SIZE	256
#define R92E_RX_DMA_BUFFER_SIZE	0x3d00

#define R92E_MAX_FW_SIZE	0x8000


/*
 * Function declarations.
 */
/* r92e_attach.c */
void	r92e_detach_private(struct rtwn_softc *);

/* r92e_chan.c */
void	r92e_set_chan(struct rtwn_softc *, struct ieee80211_channel *);

/* r92e_fw.c */
#ifndef RTWN_WITHOUT_UCODE
void	r92e_fw_reset(struct rtwn_softc *, int);
void	r92e_set_media_status(struct rtwn_softc *, int);
int	r92e_set_pwrmode(struct rtwn_softc *, struct ieee80211vap *, int);
#endif

/* r92e_init.c */
int	r92e_llt_init(struct rtwn_softc *);
void	r92e_init_bb(struct rtwn_softc *);
void	r92e_init_rf(struct rtwn_softc *);
int	r92e_power_on(struct rtwn_softc *);
void	r92e_power_off(struct rtwn_softc *);

/* r92e_led.c */
void	r92e_set_led(struct rtwn_softc *, int, int);

/* r92e_rf.c */
uint32_t	r92e_rf_read(struct rtwn_softc *, int, uint8_t);
void		r92e_rf_write(struct rtwn_softc *, int, uint8_t, uint32_t);

/* r92e_rom.c */
void	r92e_parse_rom_common(struct rtwn_softc *, uint8_t *);
void	r92e_parse_rom(struct rtwn_softc *, uint8_t *);

/* r92e_rx.c */
void	r92e_handle_c2h_report(struct rtwn_softc *, uint8_t *, int);
int8_t	r92e_get_rssi_cck(struct rtwn_softc *, void *);

#endif	/* RTL8192E_H */
