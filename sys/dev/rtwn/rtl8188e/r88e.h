/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
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
 * $OpenBSD: if_urtwnreg.h,v 1.3 2010/11/16 18:02:59 damien Exp $
 * $FreeBSD$
 */

#ifndef RTL8188E_H
#define RTL8188E_H

/*
 * Global definitions.
 */
#define R88E_TXPKTBUF_COUNT	177

#define R88E_MACID_MAX		63
#define R88E_RX_DMA_BUFFER_SIZE	0x2400

#define R88E_INTR_MSG_LEN	60

#define R88E_CALIB_THRESHOLD	4


/*
 * Function declarations.
 */
/* r88e_beacon.c */
void	r88e_beacon_enable(struct rtwn_softc *, int, int);

/* r88e_calib.c */
void	r88e_iq_calib(struct rtwn_softc *);
void	r88e_temp_measure(struct rtwn_softc *);
uint8_t	r88e_temp_read(struct rtwn_softc *);

/* r88e_chan.c */
void	r88e_get_txpower(struct rtwn_softc *, int,
	    struct ieee80211_channel *, uint8_t[]);
void	r88e_set_bw20(struct rtwn_softc *, uint8_t);
void	r88e_set_gain(struct rtwn_softc *, uint8_t);

/* r88e_fw.c */
#ifndef RTWN_WITHOUT_UCODE
int	r88e_fw_cmd(struct rtwn_softc *, uint8_t, const void *, int);
void	r88e_fw_reset(struct rtwn_softc *, int);
void	r88e_fw_download_enable(struct rtwn_softc *, int);
#endif
void	r88e_macid_enable_link(struct rtwn_softc *, int, int);
void	r88e_set_media_status(struct rtwn_softc *, int);
#ifndef RTWN_WITHOUT_UCODE
int	r88e_set_rsvd_page(struct rtwn_softc *, int, int, int);
int	r88e_set_pwrmode(struct rtwn_softc *, struct ieee80211vap *, int);
#endif

/* r88e_init.c */
void	r88e_init_bb_common(struct rtwn_softc *);
void	r88e_init_rf(struct rtwn_softc *);

/* r88e_led.c */
void	r88e_set_led(struct rtwn_softc *, int, int);

/* r88e_rf.c */
void	r88e_rf_write(struct rtwn_softc *, int, uint8_t, uint32_t);

/* r88e_rom.c */
void	r88e_parse_rom(struct rtwn_softc *, uint8_t *);

/* r88e_rx.c */
int	r88e_classify_intr(struct rtwn_softc *, void *, int);
void	r88e_ratectl_tx_complete(struct rtwn_softc *, uint8_t *, int);
void	r88e_handle_c2h_report(struct rtwn_softc *, uint8_t *, int);
int8_t	r88e_get_rssi_cck(struct rtwn_softc *, void *);
int8_t	r88e_get_rssi_ofdm(struct rtwn_softc *, void *);
void	r88e_get_rx_stats(struct rtwn_softc *, struct ieee80211_rx_stats *,
	    const void *, const void *);

/* r88e_tx.c */
void	r88e_tx_enable_ampdu(void *, int);
void	r88e_tx_setup_hwseq(void *);
void	r88e_tx_setup_macid(void *, int);

#endif	/* RTL8188E_H */
