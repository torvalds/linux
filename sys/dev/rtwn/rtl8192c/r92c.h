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

#ifndef RTL8192C_H
#define RTL8192C_H

/*
 * Global definitions.
 */
#define R92C_TXPKTBUF_COUNT	256

#define R92C_TX_PAGE_SIZE	128
#define R92C_RX_DMA_BUFFER_SIZE	0x2800

#define R92C_MAX_FW_SIZE	0x4000
#define R92C_MACID_MAX		31
#define R92C_CAM_ENTRY_COUNT	32

#define R92C_CALIB_THRESHOLD	2


/*
 * Function declarations.
 */
/* r92c_attach.c */
void	r92c_detach_private(struct rtwn_softc *);
void	r92c_read_chipid_vendor(struct rtwn_softc *, uint32_t);

/* r92c_beacon.c */
void	r92c_beacon_init(struct rtwn_softc *, void *, int);
void	r92c_beacon_enable(struct rtwn_softc *, int, int);

/* r92c_calib.c */
void	r92c_iq_calib(struct rtwn_softc *);
void	r92c_lc_calib(struct rtwn_softc *);
void	r92c_temp_measure(struct rtwn_softc *);
uint8_t	r92c_temp_read(struct rtwn_softc *);

/* r92c_chan.c */
void	r92c_get_txpower(struct rtwn_softc *, int,
	    struct ieee80211_channel *, uint8_t[]);
void	r92c_write_txpower(struct rtwn_softc *, int,
	    uint8_t power[]);
void	r92c_set_bw20(struct rtwn_softc *, uint8_t);
void	r92c_set_chan(struct rtwn_softc *, struct ieee80211_channel *);
void	r92c_set_gain(struct rtwn_softc *, uint8_t);
void	r92c_scan_start(struct ieee80211com *);
void	r92c_scan_end(struct ieee80211com *);

/* r92c_fw.c */
#ifndef RTWN_WITHOUT_UCODE
void	r92c_fw_reset(struct rtwn_softc *, int);
void	r92c_fw_download_enable(struct rtwn_softc *, int);
#endif
void	r92c_joinbss_rpt(struct rtwn_softc *, int);
#ifndef RTWN_WITHOUT_UCODE
int	r92c_set_rsvd_page(struct rtwn_softc *, int, int, int);
int	r92c_set_pwrmode(struct rtwn_softc *, struct ieee80211vap *, int);
void	r92c_set_rssi(struct rtwn_softc *);
void	r92c_handle_c2h_report(void *);
#endif

/* r92c_init.c */
int	r92c_check_condition(struct rtwn_softc *, const uint8_t[]);
int	r92c_llt_init(struct rtwn_softc *);
int	r92c_set_page_size(struct rtwn_softc *);
void	r92c_init_bb_common(struct rtwn_softc *);
int	r92c_init_rf_chain(struct rtwn_softc *,
	    const struct rtwn_rf_prog *, int);
void	r92c_init_rf(struct rtwn_softc *);
void	r92c_init_edca(struct rtwn_softc *);
void	r92c_init_ampdu(struct rtwn_softc *);
void	r92c_init_antsel(struct rtwn_softc *);
void	r92c_pa_bias_init(struct rtwn_softc *);

/* r92c_llt.c */
int	r92c_llt_write(struct rtwn_softc *, uint32_t, uint32_t);

/* r92c_rf.c */
uint32_t	r92c_rf_read(struct rtwn_softc *, int, uint8_t);
void		r92c_rf_write(struct rtwn_softc *, int, uint8_t, uint32_t);

/* r92c_rom.c */
void	r92c_efuse_postread(struct rtwn_softc *);
void	r92c_parse_rom(struct rtwn_softc *, uint8_t *);

/* r92c_rx.c */
int	r92c_classify_intr(struct rtwn_softc *, void *, int);
int8_t	r92c_get_rssi_cck(struct rtwn_softc *, void *);
int8_t	r92c_get_rssi_ofdm(struct rtwn_softc *, void *);
uint8_t	r92c_rx_radiotap_flags(const void *);
void	r92c_get_rx_stats(struct rtwn_softc *, struct ieee80211_rx_stats *,
	    const void *, const void *);

/* r92c_tx.c */
void	r92c_tx_enable_ampdu(void *, int);
void	r92c_tx_setup_hwseq(void *);
void	r92c_tx_setup_macid(void *, int);
void	r92c_fill_tx_desc(struct rtwn_softc *, struct ieee80211_node *,
	    struct mbuf *, void *, uint8_t, int);
void	r92c_fill_tx_desc_raw(struct rtwn_softc *, struct ieee80211_node *,
	    struct mbuf *, void *, const struct ieee80211_bpf_params *);
void	r92c_fill_tx_desc_null(struct rtwn_softc *, void *, int, int, int);
uint8_t	r92c_tx_radiotap_flags(const void *);

#endif	/* RTL8192C_H */
