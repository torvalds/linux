/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2010 Weongyo Jeong <weongyo@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */

#ifndef _IF_BWNVAR_H
#define	_IF_BWNVAR_H

#include <dev/bhnd/bhnd.h>

struct bwn_softc;
struct bwn_mac;

#define	N(a)			(sizeof(a) / sizeof(a[0]))
#define	BWN_ALIGN			0x1000
#define	BWN_RETRY_SHORT			7
#define	BWN_RETRY_LONG			4
#define	BWN_STAID_MAX			64
#define	BWN_TXPWR_IGNORE_TIME		(1 << 0)
#define	BWN_TXPWR_IGNORE_TSSI		(1 << 1)
#define	BWN_HAS_TXMAG(phy)						\
	(((phy)->rev >= 2) && ((phy)->rf_ver == 0x2050) &&		\
	 ((phy)->rf_rev == 8))
#define	BWN_HAS_LOOPBACK(phy)						\
	(((phy)->rev > 1) || ((phy)->gmode))
#define	BWN_TXERROR_MAX			1000
#define	BWN_GETTIME(v)	do {						\
	struct timespec ts;						\
	nanouptime(&ts);						\
	(v) = ts.tv_nsec / 1000000 + ts.tv_sec * 1000;			\
} while (0)
#define	BWN_ISOLDFMT(mac)		((mac)->mac_fw.rev <= 351)
#define	BWN_TSSI2DBM(num, den)						\
	((int32_t)((num < 0) ? num / den : (num + den / 2) / den))
#define	BWN_HDRSIZE(mac)	bwn_tx_hdrsize(mac)
#define	BWN_MAXTXHDRSIZE	(112 + (sizeof(struct bwn_plcp6)))

#define	BWN_PIO_COOKIE(tq, tp)						\
	((uint16_t)((((uint16_t)tq->tq_index + 1) << 12) | tp->tp_index))
#define	BWN_DMA_COOKIE(dr, slot)					\
	((uint16_t)(((uint16_t)dr->dr_index + 1) << 12) | (uint16_t)slot)
#define	BWN_READ_2(mac, o)						\
	(bus_read_2((mac)->mac_sc->sc_mem_res, (o)))
#define	BWN_READ_4(mac, o)						\
	(bus_read_4((mac)->mac_sc->sc_mem_res, (o)))
#define	BWN_WRITE_2(mac, o, v)						\
	(bus_write_2((mac)->mac_sc->sc_mem_res, (o), (v)))
#define	BWN_WRITE_2_F(mac, o, v) do { \
	(BWN_WRITE_2(mac, o, v)); \
	BWN_READ_2(mac, o); \
} while(0)
#define	BWN_WRITE_SETMASK2(mac, offset, mask, set)			\
	BWN_WRITE_2(mac, offset, (BWN_READ_2(mac, offset) & mask) | set)
#define	BWN_WRITE_4(mac, o, v)						\
	(bus_write_4((mac)->mac_sc->sc_mem_res, (o), (v)))
#define	BWN_WRITE_SETMASK4(mac, offset, mask, set)			\
	BWN_WRITE_4(mac, offset, (BWN_READ_4(mac, offset) & mask) | set)
#define	BWN_PIO_TXQOFFSET(mac)						\
	((bhnd_get_hwrev(mac->mac_sc->sc_dev) >= 11) ? 0x18 : 0)
#define	BWN_PIO_RXQOFFSET(mac)						\
	((bhnd_get_hwrev(mac->mac_sc->sc_dev) >= 11) ? 0x38 : 8)
#define	BWN_SEC_NEWAPI(mac)		(mac->mac_fw.rev >= 351)
#define	BWN_SEC_KEY2FW(mac, idx)					\
	(BWN_SEC_NEWAPI(mac) ? idx : ((idx >= 4) ? idx - 4 : idx))
#define	BWN_RF_READ(mac, r)		(mac->mac_phy.rf_read(mac, r))
#define	BWN_RF_WRITE(mac, r, v)		(mac->mac_phy.rf_write(mac, r, v))
#define	BWN_RF_MASK(mac, o, m)						\
	BWN_RF_WRITE(mac, o, BWN_RF_READ(mac, o) & m)
#define	BWN_RF_SETMASK(mac, offset, mask, set)				\
	BWN_RF_WRITE(mac, offset, (BWN_RF_READ(mac, offset) & mask) | set)
#define	BWN_RF_SET(mac, offset, set)					\
	BWN_RF_WRITE(mac, offset, BWN_RF_READ(mac, offset) | set)
#define	BWN_PHY_READ(mac, r)		(mac->mac_phy.phy_read(mac, r))
#define	BWN_PHY_WRITE(mac, r, v)					\
	(mac->mac_phy.phy_write(mac, r, v))
#define	BWN_PHY_SET(mac, offset, set)	do {				\
	if (mac->mac_phy.phy_maskset != NULL) {				\
		KASSERT(mac->mac_status < BWN_MAC_STATUS_INITED ||	\
		    mac->mac_suspended > 0,				\
		    ("dont access PHY or RF registers after turning on MAC")); \
		mac->mac_phy.phy_maskset(mac, offset, 0xffff, set);	\
	} else								\
		BWN_PHY_WRITE(mac, offset,				\
		    BWN_PHY_READ(mac, offset) | (set));			\
} while (0)
#define	BWN_PHY_SETMASK(mac, offset, mask, set)	do {			\
	if (mac->mac_phy.phy_maskset != NULL) {				\
		KASSERT(mac->mac_status < BWN_MAC_STATUS_INITED ||	\
		    mac->mac_suspended > 0,				\
		    ("dont access PHY or RF registers after turning on MAC")); \
		mac->mac_phy.phy_maskset(mac, offset, mask, set);	\
	} else								\
		BWN_PHY_WRITE(mac, offset,				\
		    (BWN_PHY_READ(mac, offset) & (mask)) | (set));	\
} while (0)
#define	BWN_PHY_MASK(mac, offset, mask)	do {				\
	if (mac->mac_phy.phy_maskset != NULL) {				\
		KASSERT(mac->mac_status < BWN_MAC_STATUS_INITED ||	\
		    mac->mac_suspended > 0,				\
		    ("dont access PHY or RF registers after turning on MAC")); \
		mac->mac_phy.phy_maskset(mac, offset, mask, 0);		\
	} else								\
		BWN_PHY_WRITE(mac, offset,				\
		    BWN_PHY_READ(mac, offset) & mask);			\
} while (0)
#define	BWN_PHY_COPY(mac, dst, src)	do {				\
	KASSERT(mac->mac_status < BWN_MAC_STATUS_INITED ||		\
	    mac->mac_suspended > 0,					\
	    ("dont access PHY or RF registers after turning on MAC"));	\
	BWN_PHY_WRITE(mac, dst, BWN_PHY_READ(mac, src));		\
} while (0)
#define BWN_LO_CALIB_EXPIRE		(1000 * (30 - 2))
#define BWN_LO_PWRVEC_EXPIRE		(1000 * (30 - 2))
#define BWN_LO_TXCTL_EXPIRE		(1000 * (180 - 4))
#define BWN_LPD(L, P, D)		(((L) << 2) | ((P) << 1) | ((D) << 0))
#define BWN_BITREV4(tmp)		(BWN_BITREV8(tmp) >> 4)
#define	BWN_BITREV8(byte)		(bwn_bitrev_table[byte])
#define	BWN_BBATTCMP(a, b)		((a)->att == (b)->att)
#define	BWN_RFATTCMP(a, b)						\
	(((a)->att == (b)->att) && ((a)->padmix == (b)->padmix))
#define	BWN_PIO_WRITE_2(mac, tq, offset, value)				\
	BWN_WRITE_2(mac, (tq)->tq_base + offset, value)
#define	BWN_PIO_READ_4(mac, tq, offset)					\
	BWN_READ_4(mac, tq->tq_base + offset)
#define	BWN_ISCCKRATE(rate)						\
	(rate == BWN_CCK_RATE_1MB || rate == BWN_CCK_RATE_2MB ||	\
	 rate == BWN_CCK_RATE_5MB || rate == BWN_CCK_RATE_11MB)
#define	BWN_ISOFDMRATE(rate)		(!BWN_ISCCKRATE(rate))
#define	BWN_BARRIER(mac, offset, length, flags)			\
	bus_barrier((mac)->mac_sc->sc_mem_res, (offset), (length), (flags))
#define	BWN_DMA_READ(dr, offset)				\
	(BWN_READ_4(dr->dr_mac, dr->dr_base + offset))
#define	BWN_DMA_WRITE(dr, offset, value)			\
	(BWN_WRITE_4(dr->dr_mac, dr->dr_base + offset, value))


typedef enum {
	BWN_PHY_BAND_2G = 0,
	BWN_PHY_BAND_5G_LO = 1,
	BWN_PHY_BAND_5G_MI = 2,
	BWN_PHY_BAND_5G_HI = 3
} bwn_phy_band_t;

typedef enum {
	BWN_BAND_2G,
	BWN_BAND_5G,
} bwn_band_t;

typedef enum {
	BWN_CHAN_TYPE_20,
	BWN_CHAN_TYPE_20_HT,
	BWN_CHAN_TYPE_40_HT_U,
	BWN_CHAN_TYPE_40_HT_D,
} bwn_chan_type_t;

struct bwn_rate {
	uint16_t			rateid;
	uint32_t			flags;
};

#define	BWN_ANT0			0
#define	BWN_ANT1			1
#define	BWN_ANTAUTO0			2
#define	BWN_ANTAUTO1			3
#define	BWN_ANT2			4
#define	BWN_ANT3			8
#define	BWN_ANTAUTO			BWN_ANTAUTO0
#define	BWN_ANT_DEFAULT			BWN_ANTAUTO
#define	BWN_TX_SLOTS_PER_FRAME		2

struct bwn_channel {
	unsigned			freq;
	unsigned			ieee;
	unsigned			maxTxPow;
};

struct bwn_channelinfo {
	struct bwn_channel		channels[IEEE80211_CHAN_MAX];
	unsigned			nchannels;
};

struct bwn_bbatt {
	uint8_t				att;
};

struct bwn_bbatt_list {
	const struct bwn_bbatt		*array;
	uint8_t				len;
	uint8_t				min;
	uint8_t				max;
};

struct bwn_rfatt {
	uint8_t				att;
	int				padmix;
};

struct bwn_rfatt_list {
	const struct bwn_rfatt		*array;
	uint8_t				len;
	uint8_t				min;
	uint8_t				max;
};

#define	BWN_DC_LT_SIZE			32

struct bwn_loctl {
	int8_t				i;
	int8_t				q;
};

typedef enum {
	BWN_TXPWR_RES_NEED_ADJUST,
	BWN_TXPWR_RES_DONE,
} bwn_txpwr_result_t;

struct bwn_lo_calib {
	struct bwn_bbatt		bbatt;
	struct bwn_rfatt		rfatt;
	struct bwn_loctl		ctl;
	unsigned long			calib_time;
	TAILQ_ENTRY(bwn_lo_calib)	list;
};

struct bwn_rxhdr4 {
	uint16_t			frame_len;
	uint8_t				pad1[2];
	uint16_t			phy_status0;
	union {
		struct {
			uint8_t		rssi;
			uint8_t		sig_qual;
		} __packed abg;
		struct {
			int8_t		power0;
			int8_t		power1;
		} __packed n;
	} __packed phy;
	union {
		struct {
			int8_t		power2;
			uint8_t		pad;
		} __packed n;
		struct {
			uint8_t		pad;
			int8_t		ht_power0;
		} __packed ht;
		uint16_t		phy_status2;
	} __packed ps2;
	union {
		struct {
			uint16_t	phy_status3;
		} __packed lp;
		struct {
			int8_t		phy_ht_power1;
			int8_t		phy_ht_power2;
		} __packed ht;
	} __packed ps3;
	union {
		struct {
			uint32_t	mac_status;
			uint16_t	mac_time;
			uint16_t	channel;
		} __packed r351;
		struct {
			uint16_t	phy_status4;
			uint16_t	phy_status5;
			uint32_t	mac_status;
			uint16_t	mac_time;
			uint16_t	channel;
		} __packed r598;
	} __packed ps4;
} __packed;

struct bwn_txstatus {
	uint16_t			cookie;
	uint16_t			seq;
	uint8_t				phy_stat;
	uint8_t				framecnt;
	uint8_t				rtscnt;
	uint8_t				sreason;
	uint8_t				pm;
	uint8_t				im;
	uint8_t				ampdu;
	uint8_t				ack;
};

#define	BWN_TXCTL_PA3DB			0x40
#define	BWN_TXCTL_PA2DB			0x20
#define	BWN_TXCTL_TXMIX			0x10

struct bwn_txpwr_loctl {
	struct bwn_rfatt_list		rfatt;
	struct bwn_bbatt_list		bbatt;
	uint16_t			dc_lt[BWN_DC_LT_SIZE];
	TAILQ_HEAD(, bwn_lo_calib)	calib_list;
	unsigned long			pwr_vec_read_time;
	unsigned long			txctl_measured_time;
	uint8_t				tx_bias;
	uint8_t				tx_magn;
	uint64_t			power_vector;
};

#define	BWN_OFDMTAB_DIR_UNKNOWN		0
#define	BWN_OFDMTAB_DIR_READ		1
#define	BWN_OFDMTAB_DIR_WRITE		2

struct bwn_phy_g {
	unsigned			pg_flags;
#define	BWN_PHY_G_FLAG_TSSITABLE_ALLOC	(1 << 0)
#define	BWN_PHY_G_FLAG_RADIOCTX_VALID	(1 << 1)
	int				pg_aci_enable;
	int				pg_aci_wlan_automatic;
	int				pg_aci_hw_rssi;
	int				pg_rf_on;
	uint16_t			pg_radioctx_over;
	uint16_t			pg_radioctx_overval;
	uint16_t			pg_minlowsig[2];
	uint16_t			pg_minlowsigpos[2];
	uint16_t			pg_pa0maxpwr;
	int8_t				*pg_tssi2dbm;
	int				pg_idletssi;
	int				pg_curtssi;
	uint8_t				pg_avgtssi;
	struct bwn_bbatt		pg_bbatt;
	struct bwn_rfatt		pg_rfatt;
	uint8_t				pg_txctl;
	int				pg_bbatt_delta;
	int				pg_rfatt_delta;

	struct bwn_txpwr_loctl		pg_loctl;
	int16_t				pg_max_lb_gain;
	int16_t				pg_trsw_rx_gain;
	int16_t				pg_lna_lod_gain;
	int16_t				pg_lna_gain;
	int16_t				pg_pga_gain;
	int				pg_immode;
#define	BWN_INTERFSTACK_SIZE	26
	uint32_t			pg_interfstack[BWN_INTERFSTACK_SIZE];

	int16_t				pg_nrssi[2];
	int32_t				pg_nrssi_slope;
	int8_t				pg_nrssi_lt[64];

	uint16_t			pg_lofcal;

	uint16_t			pg_initval;
	uint16_t			pg_ofdmtab_addr;
	unsigned			pg_ofdmtab_dir;
};

#define	BWN_IMMODE_NONE			0
#define	BWN_IMMODE_NONWLAN		1
#define	BWN_IMMODE_MANUAL		2
#define	BWN_IMMODE_AUTO			3

#define	BWN_PHYLP_TXPCTL_UNKNOWN	0
#define	BWN_PHYLP_TXPCTL_OFF		1
#define	BWN_PHYLP_TXPCTL_ON_SW		2
#define	BWN_PHYLP_TXPCTL_ON_HW		3

struct bwn_phy_lp {
	uint8_t				plp_chan;
	uint8_t				plp_chanfullcal;
	int32_t				plp_antenna;
	uint8_t				plp_txpctlmode;
	uint8_t				plp_txisoband_h;
	uint8_t				plp_txisoband_m;
	uint8_t				plp_txisoband_l;
	uint8_t				plp_rxpwroffset;
	int8_t				plp_txpwridx;
	uint16_t			plp_tssiidx;
	uint16_t			plp_tssinpt;
	uint8_t				plp_rssivf;
	uint8_t				plp_rssivc;
	uint8_t				plp_rssigs;
	uint8_t				plp_rccap;
	uint8_t				plp_bxarch;
	uint8_t				plp_crsusr_off;
	uint8_t				plp_crssys_off;
	uint32_t			plp_div;
	int32_t				plp_tonefreq;
	uint16_t			plp_digfilt[9];
};

/* for LP */
struct bwn_txgain {
	uint16_t			tg_gm;
	uint16_t			tg_pga;
	uint16_t			tg_pad;
	uint16_t			tg_dac;
};

struct bwn_rxcompco {
	uint8_t				rc_chan;
	int8_t				rc_c1;
	int8_t				rc_c0;
};

struct bwn_phy_lp_iq_est {
	uint32_t			ie_iqprod;
	uint32_t			ie_ipwr;
	uint32_t			ie_qpwr;
};

struct bwn_txgain_entry {
	uint8_t				te_gm;
	uint8_t				te_pga;
	uint8_t				te_pad;
	uint8_t				te_dac;
	uint8_t				te_bbmult;
};

/* only for LP PHY */
struct bwn_stxtable {
	uint16_t			st_phyoffset;
	uint16_t			st_physhift;
	uint16_t			st_rfaddr;
	uint16_t			st_rfshift;
	uint16_t			st_mask;
};

struct bwn_b206x_chan {
	uint8_t				bc_chan;
	uint16_t			bc_freq;
	const uint8_t			*bc_data;
};

struct bwn_b206x_rfinit_entry {
	uint16_t			br_offset;
	uint16_t			br_valuea;
	uint16_t			br_valueg;
	uint8_t				br_flags;
};

struct bwn_phy_n;

struct bwn_phy {
	uint8_t				type;
	uint8_t				rev;
	uint8_t				analog;

	int				supports_2ghz;
	int				supports_5ghz;

	int				gmode;
	struct bwn_phy_g		phy_g;
	struct bwn_phy_lp		phy_lp;

	/*
	 * I'd like the newer PHY code to not hide in the top-level
	 * structs..
	 */
	struct bwn_phy_n		*phy_n;

	uint16_t			rf_manuf;
	uint16_t			rf_ver;
	uint8_t				rf_rev;
	int				rf_on;
	int				phy_do_full_init;

	int				txpower;
	int				hwpctl;
	unsigned long			nexttime;
	unsigned int			chan;
	int				txerrors;

	int				(*attach)(struct bwn_mac *);
	void				(*detach)(struct bwn_mac *);
	int				(*prepare_hw)(struct bwn_mac *);
	void				(*init_pre)(struct bwn_mac *);
	int				(*init)(struct bwn_mac *);
	void				(*exit)(struct bwn_mac *);
	uint16_t			(*phy_read)(struct bwn_mac *, uint16_t);
	void				(*phy_write)(struct bwn_mac *, uint16_t,
					    uint16_t);
	void				(*phy_maskset)(struct bwn_mac *,
					    uint16_t, uint16_t, uint16_t);
	uint16_t			(*rf_read)(struct bwn_mac *, uint16_t);
	void				(*rf_write)(struct bwn_mac *, uint16_t,
					    uint16_t);
	int				(*use_hwpctl)(struct bwn_mac *);
	void				(*rf_onoff)(struct bwn_mac *, int);
	void				(*switch_analog)(struct bwn_mac *, int);
	int				(*switch_channel)(struct bwn_mac *,
					    unsigned int);
	uint32_t			(*get_default_chan)(struct bwn_mac *);
	void				(*set_antenna)(struct bwn_mac *, int);
	int				(*set_im)(struct bwn_mac *, int);
	bwn_txpwr_result_t		(*recalc_txpwr)(struct bwn_mac *, int);
	void				(*set_txpwr)(struct bwn_mac *);
	void				(*task_15s)(struct bwn_mac *);
	void				(*task_60s)(struct bwn_mac *);
};

struct bwn_chan_band {
	uint32_t			flags;
	uint8_t				nchan;
#define	BWN_MAX_CHAN_PER_BAND		14
	uint8_t				chan[BWN_MAX_CHAN_PER_BAND];
};

#define	BWN_NR_WMEPARAMS		16
enum {
	BWN_WMEPARAM_TXOP = 0,
	BWN_WMEPARAM_CWMIN,
	BWN_WMEPARAM_CWMAX,
	BWN_WMEPARAM_CWCUR,
	BWN_WMEPARAM_AIFS,
	BWN_WMEPARAM_BSLOTS,
	BWN_WMEPARAM_REGGAP,
	BWN_WMEPARAM_STATUS,
};

#define	BWN_WME_PARAMS(queue)	\
	(BWN_SHARED_EDCFQ + (BWN_NR_WMEPARAMS * sizeof(uint16_t) * (queue)))
#define	BWN_WME_BACKGROUND	BWN_WME_PARAMS(0)
#define	BWN_WME_BESTEFFORT	BWN_WME_PARAMS(1)
#define	BWN_WME_VIDEO		BWN_WME_PARAMS(2)
#define	BWN_WME_VOICE		BWN_WME_PARAMS(3)

/*
 * Radio capture format.
 */
#define	BWN_RX_RADIOTAP_PRESENT (		\
	(1 << IEEE80211_RADIOTAP_TSFT)		| \
	(1 << IEEE80211_RADIOTAP_FLAGS)		| \
	(1 << IEEE80211_RADIOTAP_RATE)		| \
	(1 << IEEE80211_RADIOTAP_CHANNEL)	| \
	(1 << IEEE80211_RADIOTAP_ANTENNA)	| \
	(1 << IEEE80211_RADIOTAP_DBM_ANTSIGNAL)	| \
	(1 << IEEE80211_RADIOTAP_DBM_ANTNOISE)	| \
	0)

struct bwn_rx_radiotap_header {
	struct ieee80211_radiotap_header wr_ihdr;
	uint64_t			wr_tsf;
	u_int8_t			wr_flags;
	u_int8_t			wr_rate;
	u_int16_t			wr_chan_freq;
	u_int16_t			wr_chan_flags;
	int8_t				wr_antsignal;
	int8_t				wr_antnoise;
	u_int8_t			wr_antenna;
} __packed __aligned(8);

#define	BWN_TX_RADIOTAP_PRESENT (		\
	(1 << IEEE80211_RADIOTAP_FLAGS)		| \
	(1 << IEEE80211_RADIOTAP_RATE)		| \
	(1 << IEEE80211_RADIOTAP_CHANNEL)	| \
	(1 << IEEE80211_RADIOTAP_DBM_TX_POWER)	| \
	(1 << IEEE80211_RADIOTAP_ANTENNA)	| \
	0)

struct bwn_tx_radiotap_header {
	struct ieee80211_radiotap_header wt_ihdr;
	u_int8_t			wt_flags;
	u_int8_t			wt_rate;
	u_int16_t			wt_chan_freq;
	u_int16_t			wt_chan_flags;
	u_int8_t			wt_txpower;
	u_int8_t			wt_antenna;
} __packed;

struct bwn_stats {
	int32_t				rtsfail;
	int32_t				rts;
	int32_t				link_noise;
};

/* Noise Calculation (Link Quality) */
struct bwn_noise {
	uint8_t				noi_running;
	uint8_t				noi_nsamples;
	int8_t				noi_samples[8][4];
};

struct bwn_dmadesc_meta {
	bus_dmamap_t			mt_dmap;
	bus_addr_t			mt_paddr;
	struct mbuf			*mt_m;
	struct ieee80211_node		*mt_ni;
	uint8_t				mt_txtype;
#define	BWN_DMADESC_METATYPE_HEADER	0
#define	BWN_DMADESC_METATYPE_BODY	1
	uint8_t				mt_islast;
};

#define	BWN_DMAINTR_FATALMASK	\
	((1 << 10) | (1 << 11) | (1 << 12) | (1 << 14) | (1 << 15))
#define	BWN_DMAINTR_NONFATALMASK	(1 << 13)
#define	BWN_DMAINTR_RX_DONE		(1 << 16)

#define	BWN_DMA32_DCTL_BYTECNT		0x00001fff
#define	BWN_DMA32_DCTL_ADDREXT_MASK	0x00030000
#define	BWN_DMA32_DCTL_ADDREXT_SHIFT	16
#define	BWN_DMA32_DCTL_DTABLEEND	0x10000000
#define	BWN_DMA32_DCTL_IRQ		0x20000000
#define	BWN_DMA32_DCTL_FRAMEEND		0x40000000
#define	BWN_DMA32_DCTL_FRAMESTART	0x80000000
struct bwn_dmadesc32 {
	uint32_t			control;
	uint32_t			address;
} __packed;

#define	BWN_DMA64_DCTL0_DTABLEEND	0x10000000
#define	BWN_DMA64_DCTL0_IRQ		0x20000000
#define	BWN_DMA64_DCTL0_FRAMEEND	0x40000000
#define	BWN_DMA64_DCTL0_FRAMESTART	0x80000000
#define	BWN_DMA64_DCTL1_BYTECNT		0x00001fff
#define	BWN_DMA64_DCTL1_ADDREXT_MASK	0x00030000
#define	BWN_DMA64_DCTL1_ADDREXT_SHIFT	16
struct bwn_dmadesc64 {
	uint32_t			control0;
	uint32_t			control1;
	uint32_t			address_low;
	uint32_t			address_high;
} __packed;

struct bwn_dmadesc_generic {
	union {
		struct bwn_dmadesc32 dma32;
		struct bwn_dmadesc64 dma64;
	} __packed dma;
} __packed;

struct bwn_dma_ring;

struct bwn_dma_ring {
	struct bwn_mac			*dr_mac;
	const struct bwn_dma_ops	*dr_ops;
	struct bwn_dmadesc_meta		*dr_meta;
	void				*dr_txhdr_cache;
	bus_dma_tag_t			dr_ring_dtag;
	bus_dma_tag_t			dr_txring_dtag;
	bus_dmamap_t			dr_spare_dmap; /* only for RX */
	bus_dmamap_t			dr_ring_dmap;
	bus_addr_t			dr_txring_paddr;
	void				*dr_ring_descbase;
	bus_addr_t			dr_ring_dmabase;
	int				dr_numslots;
	int				dr_usedslot;
	int				dr_curslot;
	uint32_t			dr_frameoffset;
	uint16_t			dr_rx_bufsize;
	uint16_t			dr_base;
	int				dr_index;
	uint8_t				dr_tx;
	uint8_t				dr_stop;
	int				dr_type;

	void				(*getdesc)(struct bwn_dma_ring *,
					    int, struct bwn_dmadesc_generic **,
					    struct bwn_dmadesc_meta **);
	void				(*setdesc)(struct bwn_dma_ring *,
					    struct bwn_dmadesc_generic *,
					    bus_addr_t, uint16_t, int, int,
					    int);
	void				(*start_transfer)(struct bwn_dma_ring *,
					    int);
	void				(*suspend)(struct bwn_dma_ring *);
	void				(*resume)(struct bwn_dma_ring *);
	int				(*get_curslot)(struct bwn_dma_ring *);
	void				(*set_curslot)(struct bwn_dma_ring *,
					    int);
};

struct bwn_dma {
	bus_dma_tag_t			parent_dtag;
	bus_dma_tag_t			rxbuf_dtag;
	bus_dma_tag_t			txbuf_dtag;
	struct bhnd_dma_translation	translation;
	u_int				addrext_shift;

	struct bwn_dma_ring		*wme[5];
	struct bwn_dma_ring		*mcast;
	struct bwn_dma_ring		*rx;
	uint64_t			lastseq;	/* XXX FIXME */
};

struct bwn_pio_rxqueue {
	struct bwn_mac			*prq_mac;
	uint16_t			prq_base;
	uint8_t				prq_rev;
};

struct bwn_pio_txqueue;
struct bwn_pio_txpkt {
	struct bwn_pio_txqueue		*tp_queue;
	struct ieee80211_node		*tp_ni;
	struct mbuf			*tp_m;
	uint8_t				tp_index;
	TAILQ_ENTRY(bwn_pio_txpkt)	tp_list;
};

#define	BWN_PIO_MAX_TXPACKETS		32
struct bwn_pio_txqueue {
	uint16_t			tq_base;
	uint16_t			tq_size;
	uint16_t			tq_used;
	uint16_t			tq_free;
	uint8_t				tq_index;
	struct bwn_pio_txpkt		tq_pkts[BWN_PIO_MAX_TXPACKETS];
	TAILQ_HEAD(, bwn_pio_txpkt)	tq_pktlist;
};

struct bwn_pio {
	struct bwn_pio_txqueue		wme[5];
	struct bwn_pio_txqueue		mcast;
	struct bwn_pio_rxqueue		rx;
};

struct bwn_plcp4 {
	union {
		uint32_t		data;
		uint8_t			raw[4];
	} __packed o;
} __packed;

struct bwn_plcp6 {
	union {
		uint32_t		data;
		uint8_t			raw[6];
	} __packed o;
} __packed;

struct bwn_txhdr {
	uint32_t			macctl;
	uint8_t				macfc[2];
	uint16_t			tx_festime;
	uint16_t			phyctl;
	uint16_t			phyctl_1;
	uint16_t			phyctl_1fb;
	uint16_t			phyctl_1rts;
	uint16_t			phyctl_1rtsfb;
	uint8_t				phyrate;
	uint8_t				phyrate_rts;
	uint8_t				eftypes;	/* extra frame types */
	uint8_t				chan;
	uint8_t				iv[16];
	uint8_t				addr1[IEEE80211_ADDR_LEN];
	uint16_t			tx_festime_fb;
	struct bwn_plcp6		rts_plcp_fb;
	uint16_t			rts_dur_fb;
	struct bwn_plcp6		plcp_fb;
	uint16_t			dur_fb;
	uint16_t			mimo_modelen;
	uint16_t			mimo_ratelen_fb;
	uint32_t			timeout;

	union {
		/* format <= r351 */
		struct {
			uint8_t		pad0[2];
			uint16_t	cookie;
			uint16_t	tx_status;
			struct bwn_plcp6	rts_plcp;
			uint8_t		rts_frame[16];
			uint8_t		pad1[2];
			struct bwn_plcp6	plcp;
		} __packed r351;
		/* format > r410 < r598 */
		struct {
			uint16_t	mimo_antenna;
			uint16_t	preload_size;
			uint8_t		pad0[2];
			uint16_t	cookie;
			uint16_t	tx_status;
			struct bwn_plcp6	rts_plcp;
			uint8_t		rts_frame[16];
			uint8_t		pad1[2];
			struct bwn_plcp6	plcp;
		} __packed r410;
		struct {
			uint16_t	mimo_antenna;
			uint16_t	preload_size;
			uint8_t		pad0[2];
			uint16_t	cookie;
			uint16_t	tx_status;
			uint16_t	max_n_mpdus;
			uint16_t	max_a_bytes_mrt;
			uint16_t	max_a_bytes_fbr;
			uint16_t	min_m_bytes;
			struct bwn_plcp6	rts_plcp;
			uint8_t		rts_frame[16];
			uint8_t		pad1[2];
			struct bwn_plcp6	plcp;
		} __packed r598;
	} __packed body;
} __packed;

#define	BWN_FWTYPE_UCODE		'u'
#define	BWN_FWTYPE_PCM			'p'
#define	BWN_FWTYPE_IV			'i'
struct bwn_fwhdr {
	uint8_t				type;
	uint8_t				ver;
	uint8_t				pad[2];
	uint32_t			size;
} __packed;

#define	BWN_FWINITVALS_OFFSET_MASK	0x7fff
#define	BWN_FWINITVALS_32BIT		0x8000
struct bwn_fwinitvals {
	uint16_t			offset_size;
	union {
		uint16_t		d16;
		uint32_t		d32;
	} __packed data;
} __packed;

enum bwn_fw_hdr_format {
	BWN_FW_HDR_598,
	BWN_FW_HDR_410,
	BWN_FW_HDR_351,
};

enum bwn_fwtype {
	BWN_FWTYPE_DEFAULT,
	BWN_FWTYPE_OPENSOURCE,
	BWN_NR_FWTYPES,
};

struct bwn_fwfile {
	const char			*filename;
	const struct firmware		*fw;
	enum bwn_fwtype			type;
};

struct bwn_key {
	void				*keyconf;
	uint8_t				algorithm;
};

struct bwn_fw {
	struct bwn_fwfile		ucode;
	struct bwn_fwfile		pcm;
	struct bwn_fwfile		initvals;
	struct bwn_fwfile		initvals_band;
	enum bwn_fw_hdr_format		fw_hdr_format;

	uint16_t			rev;
	uint16_t			patch;
	uint8_t				opensource;
	uint8_t				no_pcmfile;
};

struct bwn_lo_g_sm {
	int				curstate;
	int				nmeasure;
	int				multipler;
	uint16_t			feedth;
	struct bwn_loctl		loctl;
};

struct bwn_lo_g_value {
	uint8_t				old_channel;
	uint16_t			phy_lomask;
	uint16_t			phy_extg;
	uint16_t			phy_dacctl_hwpctl;
	uint16_t			phy_dacctl;
	uint16_t			phy_hpwr_tssictl;
	uint16_t			phy_analogover;
	uint16_t			phy_analogoverval;
	uint16_t			phy_rfover;
	uint16_t			phy_rfoverval;
	uint16_t			phy_classctl;
	uint16_t			phy_crs0;
	uint16_t			phy_pgactl;
	uint16_t			phy_syncctl;
	uint16_t			phy_cck0;
	uint16_t			phy_cck1;
	uint16_t			phy_cck2;
	uint16_t			phy_cck3;
	uint16_t			phy_cck4;
	uint16_t			reg0;
	uint16_t			reg1;
	uint16_t			rf0;
	uint16_t			rf1;
	uint16_t			rf2;
};

#define	BWN_LED_MAX			4

#define	BWN_LED_EVENT_NONE		-1
#define	BWN_LED_EVENT_POLL		0
#define	BWN_LED_EVENT_TX		1
#define	BWN_LED_EVENT_RX		2
#define	BWN_LED_SLOWDOWN(dur)		(dur) = (((dur) * 3) / 2)

struct bwn_led {
	uint8_t				led_flags;	/* BWN_LED_F_ */
	uint8_t				led_act;	/* BWN_LED_ACT_ */
	uint8_t				led_mask;
};

#define	BWN_LED_F_ACTLOW		0x1
#define	BWN_LED_F_BLINK			0x2
#define	BWN_LED_F_POLLABLE		0x4
#define	BWN_LED_F_SLOW			0x8

struct bwn_mac {
	struct bwn_softc		*mac_sc;
	unsigned			mac_status;
#define	BWN_MAC_STATUS_UNINIT		0
#define	BWN_MAC_STATUS_INITED		1
#define	BWN_MAC_STATUS_STARTED		2
	unsigned			mac_flags;
	/* use "Bad Frames Preemption" */
#define	BWN_MAC_FLAG_BADFRAME_PREEMP	(1 << 0)
#define	BWN_MAC_FLAG_DFQVALID		(1 << 1)
#define	BWN_MAC_FLAG_RADIO_ON		(1 << 2)
#define	BWN_MAC_FLAG_DMA		(1 << 3)
#define	BWN_MAC_FLAG_WME		(1 << 4)
#define	BWN_MAC_FLAG_HWCRYPTO		(1 << 5)

	struct resource			*mac_res_irq;
	int				 mac_rid_irq;
	void				*mac_intrhand;

	struct bwn_noise		mac_noise;
	struct bwn_phy			mac_phy;
	struct bwn_stats		mac_stats;
	uint32_t			mac_reason_intr;
	uint32_t			mac_reason[6];
	uint32_t			mac_intr_mask;
	int				mac_suspended;

	struct bwn_fw			mac_fw;

	int				mac_dmatype;
	union {
		struct bwn_dma		dma;
		struct bwn_pio		pio;
	} mac_method;

	uint16_t			mac_ktp;	/* Key table pointer */
	uint8_t				mac_max_nr_keys;
	struct bwn_key			mac_key[58];

	unsigned int			mac_task_state;
	struct task			mac_intrtask;
	struct task			mac_hwreset;
	struct task			mac_txpower;

	TAILQ_ENTRY(bwn_mac)	mac_list;
};

static inline int
bwn_tx_hdrsize(struct bwn_mac *mac)
{
	switch (mac->mac_fw.fw_hdr_format) {
	case BWN_FW_HDR_598:
		return (112 + (sizeof(struct bwn_plcp6)));
	case BWN_FW_HDR_410:
		return (104 + (sizeof(struct bwn_plcp6)));
	case BWN_FW_HDR_351:
		return (100 + (sizeof(struct bwn_plcp6)));
	default:
		printf("%s: unknown header format (%d)\n", __func__,
		    mac->mac_fw.fw_hdr_format);
		return (112 + (sizeof(struct bwn_plcp6)));
	}
}

/*
 * Driver-specific vap state.
 */
struct bwn_vap {
	struct ieee80211vap		bv_vap;	/* base class */
	int				(*bv_newstate)(struct ieee80211vap *,
					    enum ieee80211_state, int);
};
#define	BWN_VAP(vap)			((struct bwn_vap *)(vap))
#define	BWN_VAP_CONST(vap)		((const struct mwl_vap *)(vap))

enum bwn_quirk {
	/**
	 * The ucode PCI slowclock workaround is required on this device.
	 * @see BWN_HF_PCI_SLOWCLOCK_WORKAROUND.
	 */
	BWN_QUIRK_UCODE_SLOWCLOCK_WAR	= (1<<0),

	/**
	 * DMA is unsupported on this device; PIO should be used instead.
	 */
	BWN_QUIRK_NODMA			= (1<<1),
};

struct bwn_softc {
	device_t			sc_dev;
	struct bhnd_board_info		sc_board_info;
	struct bhnd_chipid		sc_cid;
	uint32_t			sc_quirks;	/**< @see bwn_quirk */
	struct resource			*sc_mem_res;
	int				sc_mem_rid;

	device_t			sc_chipc;	/**< ChipCommon device */
	device_t			sc_gpio;	/**< GPIO device */
	device_t			sc_pmu;		/**< PMU device, or NULL if unsupported */

	struct mtx			sc_mtx;
	struct ieee80211com		sc_ic;
	struct mbufq			sc_snd;
	unsigned			sc_flags;
#define	BWN_FLAG_ATTACHED		(1 << 0)
#define	BWN_FLAG_INVALID		(1 << 1)
#define	BWN_FLAG_NEED_BEACON_TP		(1 << 2)
#define	BWN_FLAG_RUNNING		(1 << 3)
	unsigned			sc_debug;

	struct bwn_mac		*sc_curmac;
	TAILQ_HEAD(, bwn_mac)	sc_maclist;

	uint8_t				sc_bssid[IEEE80211_ADDR_LEN];
	unsigned int			sc_filters;
	uint8_t				sc_beacons[2];
	uint8_t				sc_rf_enabled;

	struct wmeParams		sc_wmeParams[4];

	struct callout			sc_rfswitch_ch;	/* for laptop */
	struct callout			sc_task_ch;
	struct callout			sc_watchdog_ch;
	int				sc_watchdog_timer;
	struct taskqueue		*sc_tq;	/* private task queue */
	int				(*sc_newstate)(struct ieee80211com *,
					    enum ieee80211_state, int);
	void				(*sc_node_cleanup)(
					    struct ieee80211_node *);

	int				sc_rx_rate;
	int				sc_tx_rate;

	int				sc_led_blinking;
	int				sc_led_ticks;
	struct bwn_led			*sc_blink_led;
	struct callout			sc_led_blink_ch;
	int				sc_led_blink_offdur;
	struct bwn_led			sc_leds[BWN_LED_MAX];
	int				sc_led_idle;
	int				sc_led_blink;

	uint8_t				sc_ant2g;	/**< available 2GHz antennas */
	uint8_t				sc_ant5g;	/**< available 5GHz antennas */

	struct bwn_tx_radiotap_header	sc_tx_th;
	struct bwn_rx_radiotap_header	sc_rx_th;
};

#define	BWN_LOCK_INIT(sc) \
	mtx_init(&(sc)->sc_mtx, device_get_nameunit((sc)->sc_dev), \
	    MTX_NETWORK_LOCK, MTX_DEF)
#define	BWN_LOCK_DESTROY(sc)	mtx_destroy(&(sc)->sc_mtx)
#define	BWN_LOCK(sc)		mtx_lock(&(sc)->sc_mtx)
#define	BWN_UNLOCK(sc)		mtx_unlock(&(sc)->sc_mtx)
#define	BWN_ASSERT_LOCKED(sc)	mtx_assert(&(sc)->sc_mtx, MA_OWNED)

static inline bwn_band_t
bwn_channel_band(struct bwn_mac *mac, struct ieee80211_channel *c)
{
	if (IEEE80211_IS_CHAN_5GHZ(c))
		return BWN_BAND_5G;
	/* XXX check 2g, log error if not 2g or 5g? */
	return BWN_BAND_2G;
}

static inline bwn_band_t
bwn_current_band(struct bwn_mac *mac)
{
	struct ieee80211com *ic = &mac->mac_sc->sc_ic;
	if (IEEE80211_IS_CHAN_5GHZ(ic->ic_curchan))
		return BWN_BAND_5G;
	/* XXX check 2g, log error if not 2g or 5g? */
	return BWN_BAND_2G;
}

static inline bool
bwn_is_40mhz(struct bwn_mac *mac)
{
	struct ieee80211com *ic = &mac->mac_sc->sc_ic;

	return !! (IEEE80211_IS_CHAN_HT40(ic->ic_curchan));
}

static inline int
bwn_get_centre_freq(struct bwn_mac *mac)
{

	struct ieee80211com *ic = &mac->mac_sc->sc_ic;
	/* XXX TODO: calculate correctly for HT40 mode */
	return ic->ic_curchan->ic_freq;
}

static inline int
bwn_get_chan_centre_freq(struct bwn_mac *mac, struct ieee80211_channel *chan)
{

	/* XXX TODO: calculate correctly for HT40 mode */
	return chan->ic_freq;
}

static inline int
bwn_get_chan(struct bwn_mac *mac)
{

	struct ieee80211com *ic = &mac->mac_sc->sc_ic;
	/* XXX TODO: calculate correctly for HT40 mode */
	return ic->ic_curchan->ic_ieee;
}

static inline struct ieee80211_channel *
bwn_get_channel(struct bwn_mac *mac)
{

	struct ieee80211com *ic = &mac->mac_sc->sc_ic;
	return ic->ic_curchan;
}

static inline bool
bwn_is_chan_passive(struct bwn_mac *mac)
{

	struct ieee80211com *ic = &mac->mac_sc->sc_ic;
	return !! IEEE80211_IS_CHAN_PASSIVE(ic->ic_curchan);
}

static inline bwn_chan_type_t
bwn_get_chan_type(struct bwn_mac *mac, struct ieee80211_channel *c)
{
	struct ieee80211com *ic = &mac->mac_sc->sc_ic;
	if (c == NULL)
		c = ic->ic_curchan;
	if (IEEE80211_IS_CHAN_HT40U(c))
		return BWN_CHAN_TYPE_40_HT_U;
	else if (IEEE80211_IS_CHAN_HT40D(c))
		return BWN_CHAN_TYPE_40_HT_D;
	else if (IEEE80211_IS_CHAN_HT20(c))
		return BWN_CHAN_TYPE_20_HT;
	else
		return BWN_CHAN_TYPE_20;
}

static inline int
bwn_get_chan_power(struct bwn_mac *mac, struct ieee80211_channel *c)
{

	/* return in dbm */
	return c->ic_maxpower / 2;
}

#endif	/* !_IF_BWNVAR_H */
