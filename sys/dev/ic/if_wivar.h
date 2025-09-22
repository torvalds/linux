/*	$OpenBSD: if_wivar.h,v 1.30 2010/08/30 20:42:27 deraadt Exp $	*/

/*
 * Copyright (c) 1997, 1998, 1999
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 *	From: if_wireg.h,v 1.8.2.2 2001/08/25 00:48:25 nsayer Exp $
 */

#include <dev/ic/if_wi_hostap.h>

struct wi_softc	{
	struct device		sc_dev;
	struct ieee80211com	sc_ic;
	struct ifmedia		sc_media;
	bus_space_handle_t	wi_bhandle;
	bus_space_tag_t		wi_btag;
	bus_space_handle_t	wi_lhandle;
	bus_space_tag_t		wi_ltag;
	bus_size_t		wi_cor_offset;
	int			wi_tx_data_id;
	int			wi_tx_mgmt_id;
	int			wi_flags;
	int			wi_if_flags;
	int			wi_cmd_count;
	u_int16_t		wi_procframe;
	u_int16_t		wi_ptype;
	u_int16_t		wi_portnum;
	u_int16_t		wi_max_data_len;
	u_int16_t		wi_rts_thresh;
	u_int16_t		wi_ap_density;
	u_int16_t		wi_tx_rate;
	u_int16_t		wi_create_ibss;
	u_int16_t		wi_channel;
	u_int16_t		wi_pm_enabled;
	u_int16_t		wi_mor_enabled;
	u_int16_t		wi_max_sleep;
	u_int16_t		wi_authtype;
	u_int16_t		wi_roaming;
	u_int16_t		wi_supprates;
	u_int16_t		wi_diversity;

	u_int8_t		wi_rxbuf[1596];
	u_int8_t		wi_txbuf[1596];
	u_int8_t		wi_scanbuf[1596];
	int			wi_scan_lock;
	struct timeout		wi_scan_timeout;

	u_int8_t		wi_scanbuf_len;

	struct ieee80211_nwid	wi_node_name;
	struct ieee80211_nwid	wi_net_name;
	struct ieee80211_nwid	wi_ibss_name;

	int			wi_use_wep;
	int			wi_enh_security;
	int			wi_crypto_algorithm;
	int			wi_tx_key;
	struct wi_ltv_keys	wi_keys;
	struct wi_counters	wi_stats;
	void			*sc_ih;
	struct timeout		sc_timo;
	int			sc_firmware_type;
	int			sc_sta_firmware_ver;
	int			sc_pci;
	struct wihap_info	wi_hostap_info;
	u_int32_t		wi_icv;
	int			wi_icv_flag;
	int			wi_ibss_port;
	int16_t			wi_txpower;

	struct {
		u_int16_t		wi_sleep;
		u_int16_t		wi_delaysupp;
		u_int16_t		wi_txsupp;
		u_int16_t		wi_monitor;
		u_int16_t		wi_ledtest;
		u_int16_t		wi_ledtest_param0;
		u_int16_t		wi_ledtest_param1;
		u_int16_t		wi_conttx;
		u_int16_t		wi_conttx_param0;
		u_int16_t		wi_contrx;
		u_int16_t		wi_sigstate;
		u_int16_t		wi_sigstate_param0;
		u_int16_t		wi_confbits;
		u_int16_t		wi_confbits_param0;
	} wi_debug;
	struct wi_usb_softc		*wi_usb_cdata;

	struct wi_funcs			*sc_funcs;
};
#define wi_cmd		sc->sc_funcs->f_cmd
#define wi_read_record	sc->sc_funcs->f_read_record
#define wi_write_record	sc->sc_funcs->f_write_record
#define wi_alloc_nicmem	sc->sc_funcs->f_alloc_nicmem
#define wi_read_data	sc->sc_funcs->f_read_data
#define wi_write_data	sc->sc_funcs->f_write_data
#define wi_get_fid	sc->sc_funcs->f_get_fid
#define wi_init		sc->sc_funcs->f_init

/* Values for wi_flags. */
#define WI_FLAGS_ATTACHED		0x0001
#define WI_FLAGS_INITIALIZED		0x0002
#define WI_FLAGS_HAS_WEP		0x0004
#define WI_FLAGS_HAS_IBSS		0x0008
#define WI_FLAGS_HAS_CREATE_IBSS	0x0010
#define WI_FLAGS_HAS_MOR		0x0020
#define WI_FLAGS_HAS_ROAMING		0x0040
#define WI_FLAGS_HAS_DIVERSITY		0x0080
#define WI_FLAGS_HAS_HOSTAP		0x0100
#define WI_FLAGS_BUS_PCMCIA		0x0200
#define WI_FLAGS_BUS_USB		0x0400
#define WI_FLAGS_HAS_ENH_SECURITY	0x0800
#define WI_FLAGS_TXPOWER		0x1000
#define WI_FLAGS_BUSY			0x2000

#define WI_PRT_FMT "%s"
#define WI_PRT_ARG(sc)	(sc)->sc_dev.dv_xname

struct wi_funcs {
	int (*f_cmd)(struct wi_softc *sc, int cmd, int val0, int val1,
	    int val2);
	int (*f_read_record)(struct wi_softc *sc, struct wi_ltv_gen *ltv);
	int (*f_write_record)(struct wi_softc *sc,
	    struct wi_ltv_gen *ltv);
	int (*f_alloc_nicmem)(struct wi_softc *sc, int len, int *id);
	int (*f_read_data)(struct wi_softc *sc, int id, int off,
	    caddr_t buf, int len);
	int (*f_write_data)(struct wi_softc *sc, int id, int off,
	    caddr_t buf, int len);
	int (*f_get_fid)(struct wi_softc *sc, int fid);
	void (*f_init)(struct wi_softc *sc);

	void (*f_start)(struct ifnet *ifp);
	int (*f_ioctl)(struct ifnet *, u_long, caddr_t);
	void (*f_watchdog)(struct ifnet *ifp);
	void (*f_inquire)(void *xsc);
};

extern struct wi_funcs wi_func_io;

int	wi_attach(struct wi_softc *, struct wi_funcs *);
void	wi_detach(struct wi_softc *);
int	wi_intr(void *);
void	wi_stop(struct wi_softc *);
void	wi_cor_reset(struct wi_softc *);
int	wi_mgmt_xmit(struct wi_softc *, caddr_t, int);

void wi_update_stats(struct wi_softc *sc);
void wi_rxeof(struct wi_softc *sc);
void wi_txeof(struct wi_softc *sc, int status);
