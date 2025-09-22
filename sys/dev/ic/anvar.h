/*	$OpenBSD: anvar.h,v 1.24 2024/05/13 01:15:50 jsg Exp $	*/
/*	$NetBSD: anvar.h,v 1.10 2005/02/27 00:27:00 perry Exp $	*/
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
 * $FreeBSD: src/sys/dev/an/if_aironet_ieee.h,v 1.2 2000/11/13 23:04:12 wpaul Exp $
 */

#ifndef _DEV_IC_ANVAR_H
#define _DEV_IC_ANVAR_H

#define AN_TIMEOUT	65536
#define	AN_MAGIC	0x414e

/* The interrupts we will handle */
#define AN_INTRS	(AN_EV_RX | AN_EV_TX | AN_EV_TX_EXC | AN_EV_LINKSTAT)

/*
 * register space access macros
 */
#define CSR_WRITE_2(sc, reg, val)	\
	bus_space_write_2(sc->sc_iot, sc->sc_ioh, reg, val)

#define CSR_READ_2(sc, reg)		\
	bus_space_read_2(sc->sc_iot, sc->sc_ioh, reg)

#ifndef __BUS_SPACE_HAS_STREAM_METHODS
#define bus_space_write_multi_stream_2	bus_space_write_multi_2
#define bus_space_read_multi_stream_2	bus_space_read_multi_2
#endif

#define CSR_WRITE_MULTI_STREAM_2(sc, reg, val, count)	\
	bus_space_write_multi_stream_2(sc->sc_iot, sc->sc_ioh, reg, val, count)
#define CSR_READ_MULTI_STREAM_2(sc, reg, buf, count)	\
	bus_space_read_multi_stream_2(sc->sc_iot, sc->sc_ioh, reg, buf, count)

#define	AN_TX_MAX_LEN		\
		(sizeof(struct an_txframe) + ETHER_TYPE_LEN + ETHER_MAX_LEN)
#define AN_TX_RING_CNT		4
#define AN_INC(x, y)		(x) = (x + 1) % y

struct an_wepkey {
	int			an_wep_key[16];
	int			an_wep_keylen;
};

#define	AN_GAPLEN_MAX	8

#define AN_RX_RADIOTAP_PRESENT	((1 << IEEE80211_RADIOTAP_FLAGS) | \
				 (1 << IEEE80211_RADIOTAP_RATE) | \
				 (1 << IEEE80211_RADIOTAP_CHANNEL) | \
				 (1 << IEEE80211_RADIOTAP_DB_ANTSIGNAL))

struct an_rx_radiotap_header {
	struct ieee80211_radiotap_header	ar_ihdr;
	u_int8_t				ar_flags;
	u_int8_t				ar_rate;
	u_int16_t				ar_chan_freq;
	u_int16_t				ar_chan_flags;
	int8_t					ar_antsignal;
} __packed;

#define AN_TX_RADIOTAP_PRESENT	((1 << IEEE80211_RADIOTAP_FLAGS) | \
				 (1 << IEEE80211_RADIOTAP_RATE) | \
				 (1 << IEEE80211_RADIOTAP_CHANNEL))

struct an_tx_radiotap_header {
	struct ieee80211_radiotap_header	at_ihdr;
	u_int8_t				at_flags;
	u_int8_t				at_rate;
	u_int16_t				at_chan_freq;
	u_int16_t				at_chan_flags;
} __packed;


struct an_softc	{
	struct device		sc_dev;
	struct ieee80211com	sc_ic;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	void			*sc_ih;
	int			(*sc_enable)(struct an_softc *);
	void			(*sc_disable)(struct an_softc *);
	int			(*sc_newstate)(struct ieee80211com *,
				    enum ieee80211_state, int);

	int			sc_enabled;
	int			sc_invalid;
	int			sc_attached;

	int			sc_bap_id;
	int			sc_bap_off;

	int			sc_use_leap;
	struct an_wepkey 	sc_wepkeys[IEEE80211_WEP_NKID];
	int			sc_perskeylen[IEEE80211_WEP_NKID];
	int			sc_tx_key;
	int			sc_tx_perskey;
	int			sc_tx_timer;
	struct an_txdesc {
		int		d_fid;
		int		d_inuse;
	}			sc_txd[AN_TX_RING_CNT];
	int			sc_txnext;
	int			sc_txcur;

	struct an_rid_genconfig	sc_config;
	struct an_rid_caps	sc_caps;
	union {
		u_int16_t		sc_val[1];
		u_int8_t		sc_txbuf[AN_TX_MAX_LEN];
		struct an_rid_ssidlist	sc_ssidlist;
		struct an_rid_aplist	sc_aplist;
		struct an_rid_status	sc_status;
		struct an_rid_wepkey	sc_wepkey;
		struct an_rid_leapkey	sc_leapkey;
		struct an_rid_encap	sc_encap;
	}			sc_buf;

	caddr_t			sc_drvbpf;
	union {
		struct an_rx_radiotap_header	tap;
		u_int8_t			pad[64];
	} sc_rxtapu;
	union {
		struct an_tx_radiotap_header	tap;
		u_int8_t			pad[64];
	} sc_txtapu;
};

#define sc_rxtap	sc_rxtapu.tap
#define sc_txtap	sc_txtapu.tap

int	an_attach(struct an_softc *);
int	an_detach(struct an_softc *);
int	an_intr(void *);
int	an_init(struct ifnet *);
void	an_stop(struct ifnet *, int);

#endif	/* _DEV_IC_ANVAR_H */
