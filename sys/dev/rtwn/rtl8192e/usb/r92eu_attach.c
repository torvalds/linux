/*-
 * Copyright (c) 2017 Kevin Lo <kevlo@FreeBSD.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/taskqueue.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/linker.h>

#include <net/if.h>
#include <net/ethernet.h>
#include <net/if_media.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>

#include <dev/rtwn/if_rtwnreg.h>
#include <dev/rtwn/if_rtwnvar.h>

#include <dev/rtwn/if_rtwn_nop.h>

#include <dev/rtwn/usb/rtwn_usb_var.h>

#include <dev/rtwn/rtl8192c/usb/r92cu.h>

#include <dev/rtwn/rtl8188e/r88e.h>

#include <dev/rtwn/rtl8192e/r92e_priv.h>
#include <dev/rtwn/rtl8192e/r92e_reg.h>
#include <dev/rtwn/rtl8192e/r92e_var.h>
#include <dev/rtwn/rtl8192e/usb/r92eu.h>

#include <dev/rtwn/rtl8812a/usb/r12au.h>
#include <dev/rtwn/rtl8812a/usb/r12au_tx_desc.h>
#include <dev/rtwn/rtl8821a/usb/r21au.h>

#include <dev/rtwn/rtl8821a/r21a_reg.h>

void	r92eu_attach(struct rtwn_usb_softc *);

static void
r92eu_attach_private(struct rtwn_softc *sc)
{
	struct r92e_softc *rs;

	rs = malloc(sizeof(struct r92e_softc), M_RTWN_PRIV, M_WAITOK | M_ZERO);

	rs->ac_usb_dma_size		= 0x06;
	rs->ac_usb_dma_time             = 0x20;

	sc->sc_priv = rs;
}

void
r92e_detach_private(struct rtwn_softc *sc)
{
	struct r92e_softc *rs = sc->sc_priv;

	free(rs, M_RTWN_PRIV);
}

static void
r92eu_adj_devcaps(struct rtwn_softc *sc)
{
	/* XXX TODO? */
}

void
r92eu_attach(struct rtwn_usb_softc *uc)
{
	struct rtwn_softc *sc		= &uc->uc_sc;

	/* USB part. */
	uc->uc_align_rx			= r12au_align_rx;
	uc->tx_agg_desc_num		= 3;

	/* Common part. */
	sc->sc_flags			= RTWN_FLAG_EXT_HDR;

	sc->sc_set_chan			= r92e_set_chan;
	sc->sc_fill_tx_desc		= r12a_fill_tx_desc;
	sc->sc_fill_tx_desc_raw 	= r12a_fill_tx_desc_raw;
	sc->sc_fill_tx_desc_null	= r12a_fill_tx_desc_null;
	sc->sc_dump_tx_desc		= r12au_dump_tx_desc;
	sc->sc_tx_radiotap_flags	= r12a_tx_radiotap_flags;
	sc->sc_rx_radiotap_flags	= r12a_rx_radiotap_flags;
	sc->sc_get_rx_stats		= r12a_get_rx_stats;
	sc->sc_get_rssi_cck		= r92e_get_rssi_cck;
	sc->sc_get_rssi_ofdm		= r88e_get_rssi_ofdm;
	sc->sc_classify_intr		= r12au_classify_intr;
	sc->sc_handle_tx_report		= r12a_ratectl_tx_complete;
	sc->sc_handle_c2h_report	= r92e_handle_c2h_report;
	sc->sc_check_frame		= rtwn_nop_int_softc_mbuf;
	sc->sc_rf_read			= r92e_rf_read;
	sc->sc_rf_write			= r92e_rf_write;
	sc->sc_check_condition		= r92c_check_condition;
	sc->sc_efuse_postread		= rtwn_nop_softc;
	sc->sc_parse_rom		= r92e_parse_rom;
	sc->sc_set_led			= r92e_set_led;
	sc->sc_power_on			= r92e_power_on;
	sc->sc_power_off		= r92e_power_off;
#ifndef RTWN_WITHOUT_UCODE
	sc->sc_fw_reset			= r92e_fw_reset;
	sc->sc_fw_download_enable	= r12a_fw_download_enable;
#endif
	sc->sc_llt_init			= r92e_llt_init;
	sc->sc_set_page_size		= rtwn_nop_int_softc;
	sc->sc_lc_calib			= r92c_lc_calib;
	sc->sc_iq_calib			= rtwn_nop_softc;	/* XXX TODO */
	sc->sc_read_chipid_vendor	= rtwn_nop_softc_uint32;
	sc->sc_adj_devcaps		= r92eu_adj_devcaps;
	sc->sc_vap_preattach		= rtwn_nop_softc_vap;
	sc->sc_postattach		= rtwn_nop_softc;
	sc->sc_detach_private		= r92e_detach_private;
#ifndef RTWN_WITHOUT_UCODE
	sc->sc_set_media_status		= r92e_set_media_status;
	sc->sc_set_rsvd_page		= r88e_set_rsvd_page;
	sc->sc_set_pwrmode		= r92e_set_pwrmode;
	sc->sc_set_rssi			= rtwn_nop_softc;	/* XXX TODO? */
#else
	sc->sc_set_media_status		= rtwn_nop_softc_int;
#endif
	sc->sc_beacon_init		= r12a_beacon_init;
	sc->sc_beacon_enable		= r92c_beacon_enable;
	sc->sc_beacon_set_rate		= rtwn_nop_void_int;
	sc->sc_beacon_select		= r21a_beacon_select;
	sc->sc_temp_measure		= r88e_temp_measure;
	sc->sc_temp_read		= r88e_temp_read;
	sc->sc_init_tx_agg		= r21au_init_tx_agg;
	sc->sc_init_rx_agg		= r92eu_init_rx_agg;
	sc->sc_init_ampdu		= rtwn_nop_softc;
	sc->sc_init_intr		= r12a_init_intr;
	sc->sc_init_edca		= r92c_init_edca;
	sc->sc_init_bb			= r92e_init_bb;
	sc->sc_init_rf			= r92e_init_rf;
	sc->sc_init_antsel		= rtwn_nop_softc;
	sc->sc_post_init		= r92eu_post_init;
	sc->sc_init_bcnq1_boundary	= rtwn_nop_int_softc;

	sc->mac_prog			= &rtl8192eu_mac[0];
	sc->mac_size			= nitems(rtl8192eu_mac);
	sc->bb_prog			= &rtl8192eu_bb[0];
	sc->bb_size			= nitems(rtl8192eu_bb);
	sc->agc_prog			= &rtl8192eu_agc[0];
	sc->agc_size			= nitems(rtl8192eu_agc);
	sc->rf_prog			= &rtl8192eu_rf[0];

	sc->name			= "RTL8192EU";
	sc->fwname			= "rtwn-rtl8192eufw";
	sc->fwsig			= 0x92e;

	sc->page_count			= R92E_TX_PAGE_COUNT;
	sc->pktbuf_count		= 0;			/* Unused */
	sc->ackto			= 0x40;
	sc->npubqpages			= R92E_PUBQ_NPAGES;
	sc->page_size			= R92E_TX_PAGE_SIZE;
	sc->txdesc_len			= sizeof(struct r12au_tx_desc);
	sc->efuse_maxlen		= R92E_EFUSE_MAX_LEN;
	sc->efuse_maplen		= R92E_EFUSE_MAP_LEN;
	sc->rx_dma_size			= R92E_RX_DMA_BUFFER_SIZE;

	sc->macid_limit			= R12A_MACID_MAX + 1;
	sc->cam_entry_limit		= R12A_CAM_ENTRY_COUNT;
	sc->fwsize_limit		= R92E_MAX_FW_SIZE;
	sc->temp_delta			= R88E_CALIB_THRESHOLD;

	sc->bcn_status_reg[0]		= R92C_TDECTRL;
	sc->bcn_status_reg[1]		= R21A_DWBCN1_CTRL;
	sc->rcr				= 0;

	sc->ntxchains			= 2;
	sc->nrxchains			= 2;

	r92eu_attach_private(sc);
}
