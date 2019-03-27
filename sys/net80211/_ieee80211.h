/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Atsushi Onoe
 * Copyright (c) 2002-2008 Sam Leffler, Errno Consulting
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef _NET80211__IEEE80211_H_
#define _NET80211__IEEE80211_H_

/*
 * 802.11 implementation definitions.
 *
 * NB: this file is used by applications.
 */

/*
 * PHY type; mostly used to identify FH phys.
 */
enum ieee80211_phytype {
	IEEE80211_T_DS,			/* direct sequence spread spectrum */
	IEEE80211_T_FH,			/* frequency hopping */
	IEEE80211_T_OFDM,		/* frequency division multiplexing */
	IEEE80211_T_TURBO,		/* high rate OFDM, aka turbo mode */
	IEEE80211_T_HT,			/* high throughput */
	IEEE80211_T_OFDM_HALF,		/* 1/2 rate OFDM */
	IEEE80211_T_OFDM_QUARTER,	/* 1/4 rate OFDM */
	IEEE80211_T_VHT,		/* VHT PHY */
};
#define	IEEE80211_T_CCK	IEEE80211_T_DS	/* more common nomenclature */

/*
 * PHY mode; this is not really a mode as multi-mode devices
 * have multiple PHY's.  Mode is mostly used as a shorthand
 * for constraining which channels to consider in setting up
 * operation.  Modes used to be used more extensively when
 * channels were identified as IEEE channel numbers.
 */
enum ieee80211_phymode {
	IEEE80211_MODE_AUTO	= 0,	/* autoselect */
	IEEE80211_MODE_11A	= 1,	/* 5GHz, OFDM */
	IEEE80211_MODE_11B	= 2,	/* 2GHz, CCK */
	IEEE80211_MODE_11G	= 3,	/* 2GHz, OFDM */
	IEEE80211_MODE_FH	= 4,	/* 2GHz, GFSK */
	IEEE80211_MODE_TURBO_A	= 5,	/* 5GHz, OFDM, 2x clock */
	IEEE80211_MODE_TURBO_G	= 6,	/* 2GHz, OFDM, 2x clock */
	IEEE80211_MODE_STURBO_A	= 7,	/* 5GHz, OFDM, 2x clock, static */
	IEEE80211_MODE_11NA	= 8,	/* 5GHz, w/ HT */
	IEEE80211_MODE_11NG	= 9,	/* 2GHz, w/ HT */
	IEEE80211_MODE_HALF	= 10,	/* OFDM, 1/2x clock */
	IEEE80211_MODE_QUARTER	= 11,	/* OFDM, 1/4x clock */
	IEEE80211_MODE_VHT_2GHZ	= 12,	/* 2GHz, VHT */
	IEEE80211_MODE_VHT_5GHZ	= 13,	/* 5GHz, VHT */
};
#define	IEEE80211_MODE_MAX	(IEEE80211_MODE_VHT_5GHZ+1)
#define	IEEE80211_MODE_BYTES	howmany(IEEE80211_MODE_MAX, NBBY)

/*
 * Operating mode.  Devices do not necessarily support
 * all modes; they indicate which are supported in their
 * capabilities.
 */
enum ieee80211_opmode {
	IEEE80211_M_IBSS 	= 0,	/* IBSS (adhoc) station */
	IEEE80211_M_STA		= 1,	/* infrastructure station */
	IEEE80211_M_WDS		= 2,	/* WDS link */
	IEEE80211_M_AHDEMO	= 3,	/* Old lucent compatible adhoc demo */
	IEEE80211_M_HOSTAP	= 4,	/* Software Access Point */
	IEEE80211_M_MONITOR	= 5,	/* Monitor mode */
	IEEE80211_M_MBSS	= 6,	/* MBSS (Mesh Point) link */
};
#define	IEEE80211_OPMODE_MAX	(IEEE80211_M_MBSS+1)

/*
 * 802.11g/802.11n protection mode.
 */
enum ieee80211_protmode {
	IEEE80211_PROT_NONE	= 0,	/* no protection */
	IEEE80211_PROT_CTSONLY	= 1,	/* CTS to self */
	IEEE80211_PROT_RTSCTS	= 2,	/* RTS-CTS */
};

/*
 * Authentication mode.  The open and shared key authentication
 * modes are implemented within the 802.11 layer.  802.1x and
 * WPA/802.11i are implemented in user mode by setting the
 * 802.11 layer into IEEE80211_AUTH_8021X and deferring
 * authentication to user space programs.
 */
enum ieee80211_authmode {
	IEEE80211_AUTH_NONE	= 0,
	IEEE80211_AUTH_OPEN	= 1,		/* open */
	IEEE80211_AUTH_SHARED	= 2,		/* shared-key */
	IEEE80211_AUTH_8021X	= 3,		/* 802.1x */
	IEEE80211_AUTH_AUTO	= 4,		/* auto-select/accept */
	/* NB: these are used only for ioctls */
	IEEE80211_AUTH_WPA	= 5,		/* WPA/RSN w/ 802.1x/PSK */
};

/*
 * Roaming mode is effectively who controls the operation
 * of the 802.11 state machine when operating as a station.
 * State transitions are controlled either by the driver
 * (typically when management frames are processed by the
 * hardware/firmware), the host (auto/normal operation of
 * the 802.11 layer), or explicitly through ioctl requests
 * when applications like wpa_supplicant want control.
 */
enum ieee80211_roamingmode {
	IEEE80211_ROAMING_DEVICE= 0,	/* driver/hardware control */
	IEEE80211_ROAMING_AUTO	= 1,	/* 802.11 layer control */
	IEEE80211_ROAMING_MANUAL= 2,	/* application control */
};

/*
 * Channels are specified by frequency and attributes.
 */
struct ieee80211_channel {
	uint32_t	ic_flags;	/* see below */
	uint16_t	ic_freq;	/* primary centre frequency in MHz */
	uint8_t		ic_ieee;	/* IEEE channel number */
	int8_t		ic_maxregpower;	/* maximum regulatory tx power in dBm */
	int8_t		ic_maxpower;	/* maximum tx power in .5 dBm */
	int8_t		ic_minpower;	/* minimum tx power in .5 dBm */
	uint8_t		ic_state;	/* dynamic state */
	uint8_t		ic_extieee;	/* HT40 extension channel number */
	int8_t		ic_maxantgain;	/* maximum antenna gain in .5 dBm */
	uint8_t		ic_pad;
	uint16_t	ic_devdata;	/* opaque device/driver data */
	uint8_t		ic_vht_ch_freq1; /* VHT primary freq1 IEEE value */
	uint8_t		ic_vht_ch_freq2; /* VHT secondary 80MHz freq2 IEEE value */
	uint16_t	ic_freq2;	/* VHT secondary 80MHz freq2 MHz */
};

/*
 * Note: for VHT operation we will need significantly more than
 * IEEE80211_CHAN_MAX channels because of the combinations of
 * VHT20, VHT40, VHT80, VHT80+80 and VHT160.
 */
#define	IEEE80211_CHAN_MAX	1024
#define	IEEE80211_CHAN_BYTES	howmany(IEEE80211_CHAN_MAX, NBBY)
#define	IEEE80211_CHAN_ANY	0xffff	/* token for ``any channel'' */
#define	IEEE80211_CHAN_ANYC \
	((struct ieee80211_channel *) IEEE80211_CHAN_ANY)

/* channel attributes */
#define	IEEE80211_CHAN_PRIV0	0x00000001 /* driver private bit 0 */
#define	IEEE80211_CHAN_PRIV1	0x00000002 /* driver private bit 1 */
#define	IEEE80211_CHAN_PRIV2	0x00000004 /* driver private bit 2 */
#define	IEEE80211_CHAN_PRIV3	0x00000008 /* driver private bit 3 */
#define	IEEE80211_CHAN_TURBO	0x00000010 /* Turbo channel */
#define	IEEE80211_CHAN_CCK	0x00000020 /* CCK channel */
#define	IEEE80211_CHAN_OFDM	0x00000040 /* OFDM channel */
#define	IEEE80211_CHAN_2GHZ	0x00000080 /* 2 GHz spectrum channel. */
#define	IEEE80211_CHAN_5GHZ	0x00000100 /* 5 GHz spectrum channel */
#define	IEEE80211_CHAN_PASSIVE	0x00000200 /* Only passive scan allowed */
#define	IEEE80211_CHAN_DYN	0x00000400 /* Dynamic CCK-OFDM channel */
#define	IEEE80211_CHAN_GFSK	0x00000800 /* GFSK channel (FHSS PHY) */
#define	IEEE80211_CHAN_GSM	0x00001000 /* 900 MHz spectrum channel */
#define	IEEE80211_CHAN_STURBO	0x00002000 /* 11a static turbo channel only */
#define	IEEE80211_CHAN_HALF	0x00004000 /* Half rate channel */
#define	IEEE80211_CHAN_QUARTER	0x00008000 /* Quarter rate channel */
#define	IEEE80211_CHAN_HT20	0x00010000 /* HT 20 channel */
#define	IEEE80211_CHAN_HT40U	0x00020000 /* HT 40 channel w/ ext above */
#define	IEEE80211_CHAN_HT40D	0x00040000 /* HT 40 channel w/ ext below */
#define	IEEE80211_CHAN_DFS	0x00080000 /* DFS required */
#define	IEEE80211_CHAN_4MSXMIT	0x00100000 /* 4ms limit on frame length */
#define	IEEE80211_CHAN_NOADHOC	0x00200000 /* adhoc mode not allowed */
#define	IEEE80211_CHAN_NOHOSTAP	0x00400000 /* hostap mode not allowed */
#define	IEEE80211_CHAN_11D	0x00800000 /* 802.11d required */
#define	IEEE80211_CHAN_VHT20	0x01000000 /* VHT20 channel */
#define	IEEE80211_CHAN_VHT40U	0x02000000 /* VHT40 channel, ext above */
#define	IEEE80211_CHAN_VHT40D	0x04000000 /* VHT40 channel, ext below */
#define	IEEE80211_CHAN_VHT80	0x08000000 /* VHT80 channel */
#define	IEEE80211_CHAN_VHT80_80	0x10000000 /* VHT80+80 channel */
#define	IEEE80211_CHAN_VHT160	0x20000000 /* VHT160 channel */
/* XXX note: 0x80000000 is used in src/sbin/ifconfig/ifieee80211.c :( */

#define	IEEE80211_CHAN_HT40	(IEEE80211_CHAN_HT40U | IEEE80211_CHAN_HT40D)
#define	IEEE80211_CHAN_HT	(IEEE80211_CHAN_HT20 | IEEE80211_CHAN_HT40)

#define	IEEE80211_CHAN_VHT40	(IEEE80211_CHAN_VHT40U | IEEE80211_CHAN_VHT40D)
#define	IEEE80211_CHAN_VHT	(IEEE80211_CHAN_VHT20 | IEEE80211_CHAN_VHT40 \
				| IEEE80211_CHAN_VHT80 | IEEE80211_CHAN_VHT80_80 \
				| IEEE80211_CHAN_VHT160)

#define	IEEE80211_CHAN_BITS \
	"\20\1PRIV0\2PRIV2\3PRIV3\4PRIV4\5TURBO\6CCK\7OFDM\0102GHZ\0115GHZ" \
	"\12PASSIVE\13DYN\14GFSK\15GSM\16STURBO\17HALF\20QUARTER\21HT20" \
	"\22HT40U\23HT40D\24DFS\0254MSXMIT\26NOADHOC\27NOHOSTAP\03011D" \
	"\031VHT20\032VHT40U\033VHT40D\034VHT80\035VHT80_80\036VHT160"

/*
 * Useful combinations of channel characteristics.
 */
#define	IEEE80211_CHAN_FHSS \
	(IEEE80211_CHAN_2GHZ | IEEE80211_CHAN_GFSK)
#define	IEEE80211_CHAN_A \
	(IEEE80211_CHAN_5GHZ | IEEE80211_CHAN_OFDM)
#define	IEEE80211_CHAN_B \
	(IEEE80211_CHAN_2GHZ | IEEE80211_CHAN_CCK)
#define	IEEE80211_CHAN_PUREG \
	(IEEE80211_CHAN_2GHZ | IEEE80211_CHAN_OFDM)
#define	IEEE80211_CHAN_G \
	(IEEE80211_CHAN_2GHZ | IEEE80211_CHAN_DYN)
#define IEEE80211_CHAN_108A \
	(IEEE80211_CHAN_A | IEEE80211_CHAN_TURBO)
#define	IEEE80211_CHAN_108G \
	(IEEE80211_CHAN_PUREG | IEEE80211_CHAN_TURBO)
#define	IEEE80211_CHAN_ST \
	(IEEE80211_CHAN_108A | IEEE80211_CHAN_STURBO)

#define	IEEE80211_CHAN_ALL \
	(IEEE80211_CHAN_2GHZ | IEEE80211_CHAN_5GHZ | IEEE80211_CHAN_GFSK | \
	 IEEE80211_CHAN_CCK | IEEE80211_CHAN_OFDM | IEEE80211_CHAN_DYN | \
	 IEEE80211_CHAN_HALF | IEEE80211_CHAN_QUARTER | \
	 IEEE80211_CHAN_HT | IEEE80211_CHAN_VHT)
#define	IEEE80211_CHAN_ALLTURBO \
	(IEEE80211_CHAN_ALL | IEEE80211_CHAN_TURBO | IEEE80211_CHAN_STURBO)

#define	IEEE80211_IS_CHAN_FHSS(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_FHSS) == IEEE80211_CHAN_FHSS)
#define	IEEE80211_IS_CHAN_A(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_A) == IEEE80211_CHAN_A)
#define	IEEE80211_IS_CHAN_B(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_B) == IEEE80211_CHAN_B)
#define	IEEE80211_IS_CHAN_PUREG(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_PUREG) == IEEE80211_CHAN_PUREG)
#define	IEEE80211_IS_CHAN_G(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_G) == IEEE80211_CHAN_G)
#define	IEEE80211_IS_CHAN_ANYG(_c) \
	(IEEE80211_IS_CHAN_PUREG(_c) || IEEE80211_IS_CHAN_G(_c))
#define	IEEE80211_IS_CHAN_ST(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_ST) == IEEE80211_CHAN_ST)
#define	IEEE80211_IS_CHAN_108A(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_108A) == IEEE80211_CHAN_108A)
#define	IEEE80211_IS_CHAN_108G(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_108G) == IEEE80211_CHAN_108G)

#define	IEEE80211_IS_CHAN_2GHZ(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_2GHZ) != 0)
#define	IEEE80211_IS_CHAN_5GHZ(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_5GHZ) != 0)
#define	IEEE80211_IS_CHAN_PASSIVE(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_PASSIVE) != 0)
#define	IEEE80211_IS_CHAN_OFDM(_c) \
	(((_c)->ic_flags & (IEEE80211_CHAN_OFDM | IEEE80211_CHAN_DYN)) != 0)
#define	IEEE80211_IS_CHAN_CCK(_c) \
	(((_c)->ic_flags & (IEEE80211_CHAN_CCK | IEEE80211_CHAN_DYN)) != 0)
#define	IEEE80211_IS_CHAN_DYN(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_DYN) == IEEE80211_CHAN_DYN)
#define	IEEE80211_IS_CHAN_GFSK(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_GFSK) != 0)
#define	IEEE80211_IS_CHAN_TURBO(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_TURBO) != 0)
#define	IEEE80211_IS_CHAN_STURBO(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_STURBO) != 0)
#define	IEEE80211_IS_CHAN_DTURBO(_c) \
	(((_c)->ic_flags & \
	(IEEE80211_CHAN_TURBO | IEEE80211_CHAN_STURBO)) == IEEE80211_CHAN_TURBO)
#define	IEEE80211_IS_CHAN_HALF(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_HALF) != 0)
#define	IEEE80211_IS_CHAN_QUARTER(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_QUARTER) != 0)
#define	IEEE80211_IS_CHAN_FULL(_c) \
	(((_c)->ic_flags & (IEEE80211_CHAN_QUARTER | IEEE80211_CHAN_HALF)) == 0)
#define	IEEE80211_IS_CHAN_GSM(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_GSM) != 0)
#define	IEEE80211_IS_CHAN_HT(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_HT) != 0)
#define	IEEE80211_IS_CHAN_HT20(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_HT20) != 0)
#define	IEEE80211_IS_CHAN_HT40(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_HT40) != 0)
#define	IEEE80211_IS_CHAN_HT40U(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_HT40U) != 0)
#define	IEEE80211_IS_CHAN_HT40D(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_HT40D) != 0)
#define	IEEE80211_IS_CHAN_HTA(_c) \
	(IEEE80211_IS_CHAN_5GHZ(_c) && \
	 ((_c)->ic_flags & IEEE80211_CHAN_HT) != 0)
#define	IEEE80211_IS_CHAN_HTG(_c) \
	(IEEE80211_IS_CHAN_2GHZ(_c) && \
	 ((_c)->ic_flags & IEEE80211_CHAN_HT) != 0)
#define	IEEE80211_IS_CHAN_DFS(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_DFS) != 0)
#define	IEEE80211_IS_CHAN_NOADHOC(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_NOADHOC) != 0)
#define	IEEE80211_IS_CHAN_NOHOSTAP(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_NOHOSTAP) != 0)
#define	IEEE80211_IS_CHAN_11D(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_11D) != 0)

#define	IEEE80211_IS_CHAN_VHT(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_VHT) != 0)
#define	IEEE80211_IS_CHAN_VHT_2GHZ(_c) \
	(IEEE80211_IS_CHAN_2GHZ(_c) && \
	 ((_c)->ic_flags & IEEE80211_CHAN_VHT) != 0)
#define	IEEE80211_IS_CHAN_VHT_5GHZ(_c) \
	(IEEE80211_IS_CHAN_5GHZ(_c) && \
	 ((_c)->ic_flags & IEEE80211_CHAN_VHT) != 0)
#define	IEEE80211_IS_CHAN_VHT20(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_VHT20) != 0)
#define	IEEE80211_IS_CHAN_VHT40(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_VHT40) != 0)
#define	IEEE80211_IS_CHAN_VHT40U(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_VHT40U) != 0)
#define	IEEE80211_IS_CHAN_VHT40D(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_VHT40D) != 0)
#define	IEEE80211_IS_CHAN_VHTA(_c) \
	(IEEE80211_IS_CHAN_5GHZ(_c) && \
	 ((_c)->ic_flags & IEEE80211_CHAN_VHT) != 0)
#define	IEEE80211_IS_CHAN_VHTG(_c) \
	(IEEE80211_IS_CHAN_2GHZ(_c) && \
	 ((_c)->ic_flags & IEEE80211_CHAN_VHT) != 0)
#define	IEEE80211_IS_CHAN_VHT80(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_VHT80) != 0)
#define	IEEE80211_IS_CHAN_VHT80_80(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_VHT80_80) != 0)
#define	IEEE80211_IS_CHAN_VHT160(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_VHT160) != 0)

#define	IEEE80211_CHAN2IEEE(_c)		(_c)->ic_ieee

/* dynamic state */
#define	IEEE80211_CHANSTATE_RADAR	0x01	/* radar detected */
#define	IEEE80211_CHANSTATE_CACDONE	0x02	/* CAC completed */
#define	IEEE80211_CHANSTATE_CWINT	0x04	/* interference detected */
#define	IEEE80211_CHANSTATE_NORADAR	0x10	/* post notify on radar clear */

#define	IEEE80211_IS_CHAN_RADAR(_c) \
	(((_c)->ic_state & IEEE80211_CHANSTATE_RADAR) != 0)
#define	IEEE80211_IS_CHAN_CACDONE(_c) \
	(((_c)->ic_state & IEEE80211_CHANSTATE_CACDONE) != 0)
#define	IEEE80211_IS_CHAN_CWINT(_c) \
	(((_c)->ic_state & IEEE80211_CHANSTATE_CWINT) != 0)

/* ni_chan encoding for FH phy */
#define	IEEE80211_FH_CHANMOD	80
#define	IEEE80211_FH_CHAN(set,pat)	(((set)-1)*IEEE80211_FH_CHANMOD+(pat))
#define	IEEE80211_FH_CHANSET(chan)	((chan)/IEEE80211_FH_CHANMOD+1)
#define	IEEE80211_FH_CHANPAT(chan)	((chan)%IEEE80211_FH_CHANMOD)

#define	IEEE80211_TID_SIZE	(WME_NUM_TID+1)	/* WME TID's +1 for non-QoS */
#define	IEEE80211_NONQOS_TID	WME_NUM_TID	/* index for non-QoS sta */

/*
 * The 802.11 spec says at most 2007 stations may be
 * associated at once.  For most AP's this is way more
 * than is feasible so we use a default of 128.  This
 * number may be overridden by the driver and/or by
 * user configuration but may not be less than IEEE80211_AID_MIN.
 */
#define	IEEE80211_AID_DEF		128
#define	IEEE80211_AID_MIN		16

/*
 * 802.11 rate set.
 */
#define	IEEE80211_RATE_SIZE	8		/* 802.11 standard */
#define	IEEE80211_RATE_MAXSIZE	15		/* max rates we'll handle */

struct ieee80211_rateset {
	uint8_t		rs_nrates;
	uint8_t		rs_rates[IEEE80211_RATE_MAXSIZE];
};

/*
 * 802.11n variant of ieee80211_rateset.  Instead of
 * legacy rates the entries are MCS rates.  We define
 * the structure such that it can be used interchangeably
 * with an ieee80211_rateset (modulo structure size).
 */
#define	IEEE80211_HTRATE_MAXSIZE	77

struct ieee80211_htrateset {
	uint8_t		rs_nrates;
	uint8_t		rs_rates[IEEE80211_HTRATE_MAXSIZE];
};

#define	IEEE80211_RATE_MCS	0x80

/*
 * Per-mode transmit parameters/controls visible to user space.
 * These can be used to set fixed transmit rate for all operating
 * modes or on a per-client basis according to the capabilities
 * of the client (e.g. an 11b client associated to an 11g ap).
 *
 * MCS are distinguished from legacy rates by or'ing in 0x80.
 */
struct ieee80211_txparam {
	uint8_t		ucastrate;	/* ucast data rate (legacy/MCS|0x80) */
	uint8_t		mgmtrate;	/* mgmt frame rate (legacy/MCS|0x80) */
	uint8_t		mcastrate;	/* multicast rate (legacy/MCS|0x80) */
	uint8_t		maxretry;	/* max unicast data retry count */
};

/*
 * Per-mode roaming state visible to user space.  There are two
 * thresholds that control whether roaming is considered; when
 * either is exceeded the 802.11 layer will check the scan cache
 * for another AP.  If the cache is stale then a scan may be
 * triggered.
 */
struct ieee80211_roamparam {
	int8_t		rssi;		/* rssi thresh (.5 dBm) */
	uint8_t		rate;		/* tx rate thresh (.5 Mb/s or MCS) */
	uint16_t	pad;		/* reserve */
};

/*
 * Regulatory Information.
 */
struct ieee80211_regdomain {
	uint16_t	regdomain;	/* SKU */
	uint16_t	country;	/* ISO country code */
	uint8_t		location;	/* I (indoor), O (outdoor), other */
	uint8_t		ecm;		/* Extended Channel Mode */
	char		isocc[2];	/* country code string */
	short		pad[2];
};

/*
 * MIMO antenna/radio state.
 */
#define	IEEE80211_MAX_CHAINS		4
/*
 * This is the number of sub-channels for a channel.
 * 0 - pri20
 * 1 - sec20 (HT40, VHT40)
 * 2 - sec40 (VHT80)
 * 3 - sec80 (VHT80+80, VHT160)
 */
#define	IEEE80211_MAX_CHAIN_PRISEC	4
#define	IEEE80211_MAX_EVM_DWORDS	16	/* 16 pilots, 4 chains */
#define	IEEE80211_MAX_EVM_PILOTS	16	/* 468 subcarriers, 16 pilots */

struct ieee80211_mimo_chan_info {
	int8_t	rssi[IEEE80211_MAX_CHAIN_PRISEC];
	int8_t	noise[IEEE80211_MAX_CHAIN_PRISEC];
};

struct ieee80211_mimo_info {
	struct ieee80211_mimo_chan_info ch[IEEE80211_MAX_CHAINS];
	uint32_t	evm[IEEE80211_MAX_EVM_DWORDS];
};

/*
 * ic_caps/iv_caps: device driver capabilities
 */
/* 0x2e available */
#define	IEEE80211_C_STA		0x00000001	/* CAPABILITY: STA available */
#define	IEEE80211_C_8023ENCAP	0x00000002	/* CAPABILITY: 802.3 encap */
#define	IEEE80211_C_FF		0x00000040	/* CAPABILITY: ATH FF avail */
#define	IEEE80211_C_TURBOP	0x00000080	/* CAPABILITY: ATH Turbo avail*/
#define	IEEE80211_C_IBSS	0x00000100	/* CAPABILITY: IBSS available */
#define	IEEE80211_C_PMGT	0x00000200	/* CAPABILITY: Power mgmt */
#define	IEEE80211_C_HOSTAP	0x00000400	/* CAPABILITY: HOSTAP avail */
#define	IEEE80211_C_AHDEMO	0x00000800	/* CAPABILITY: Old Adhoc Demo */
#define	IEEE80211_C_SWRETRY	0x00001000	/* CAPABILITY: sw tx retry */
#define	IEEE80211_C_TXPMGT	0x00002000	/* CAPABILITY: tx power mgmt */
#define	IEEE80211_C_SHSLOT	0x00004000	/* CAPABILITY: short slottime */
#define	IEEE80211_C_SHPREAMBLE	0x00008000	/* CAPABILITY: short preamble */
#define	IEEE80211_C_MONITOR	0x00010000	/* CAPABILITY: monitor mode */
#define	IEEE80211_C_DFS		0x00020000	/* CAPABILITY: DFS/radar avail*/
#define	IEEE80211_C_MBSS	0x00040000	/* CAPABILITY: MBSS available */
#define	IEEE80211_C_SWSLEEP	0x00080000	/* CAPABILITY: do sleep here */
#define	IEEE80211_C_SWAMSDUTX	0x00100000	/* CAPABILITY: software A-MSDU TX */
/* 0x7c0000 available */
#define	IEEE80211_C_WPA1	0x00800000	/* CAPABILITY: WPA1 avail */
#define	IEEE80211_C_WPA2	0x01000000	/* CAPABILITY: WPA2 avail */
#define	IEEE80211_C_WPA		0x01800000	/* CAPABILITY: WPA1+WPA2 avail*/
#define	IEEE80211_C_BURST	0x02000000	/* CAPABILITY: frame bursting */
#define	IEEE80211_C_WME		0x04000000	/* CAPABILITY: WME avail */
#define	IEEE80211_C_WDS		0x08000000	/* CAPABILITY: 4-addr support */
/* 0x10000000 reserved */
#define	IEEE80211_C_BGSCAN	0x20000000	/* CAPABILITY: bg scanning */
#define	IEEE80211_C_TXFRAG	0x40000000	/* CAPABILITY: tx fragments */
#define	IEEE80211_C_TDMA	0x80000000	/* CAPABILITY: TDMA avail */
/* XXX protection/barker? */

#define	IEEE80211_C_OPMODE \
	(IEEE80211_C_STA | IEEE80211_C_IBSS | IEEE80211_C_HOSTAP | \
	 IEEE80211_C_AHDEMO | IEEE80211_C_MONITOR | IEEE80211_C_WDS | \
	 IEEE80211_C_TDMA | IEEE80211_C_MBSS)

#define	IEEE80211_C_BITS \
	"\20\1STA\002803ENCAP\7FF\10TURBOP\11IBSS\12PMGT" \
	"\13HOSTAP\14AHDEMO\15SWRETRY\16TXPMGT\17SHSLOT\20SHPREAMBLE" \
	"\21MONITOR\22DFS\23MBSS\30WPA1\31WPA2\32BURST\33WME\34WDS\36BGSCAN" \
	"\37TXFRAG\40TDMA"

/*
 * ic_htcaps/iv_htcaps: HT-specific device/driver capabilities
 *
 * NB: the low 16-bits are the 802.11 definitions, the upper
 *     16-bits are used to define s/w/driver capabilities.
 */
#define	IEEE80211_HTC_AMPDU	0x00010000	/* CAPABILITY: A-MPDU tx */
#define	IEEE80211_HTC_AMSDU	0x00020000	/* CAPABILITY: A-MSDU tx */
/* NB: HT40 is implied by IEEE80211_HTCAP_CHWIDTH40 */
#define	IEEE80211_HTC_HT	0x00040000	/* CAPABILITY: HT operation */
#define	IEEE80211_HTC_SMPS	0x00080000	/* CAPABILITY: MIMO power save*/
#define	IEEE80211_HTC_RIFS	0x00100000	/* CAPABILITY: RIFS support */
#define	IEEE80211_HTC_RXUNEQUAL	0x00200000	/* CAPABILITY: RX unequal MCS */
#define	IEEE80211_HTC_RXMCS32	0x00400000	/* CAPABILITY: MCS32 support */
#define	IEEE80211_HTC_TXUNEQUAL	0x00800000	/* CAPABILITY: TX unequal MCS */
#define	IEEE80211_HTC_TXMCS32	0x01000000	/* CAPABILITY: MCS32 support */
#define	IEEE80211_HTC_TXLDPC	0x02000000	/* CAPABILITY: TX using LDPC */

#define	IEEE80211_C_HTCAP_BITS \
	"\20\1LDPC\2CHWIDTH40\5GREENFIELD\6SHORTGI20\7SHORTGI40\10TXSTBC" \
	"\21AMPDU\22AMSDU\23HT\24SMPS\25RIFS\32TXLDPC"

/*
 * RX status notification - which fields are valid.
 */
#define	IEEE80211_R_NF		0x00000001	/* global NF value valid */
#define	IEEE80211_R_RSSI	0x00000002	/* global RSSI value valid */
#define	IEEE80211_R_C_CHAIN	0x00000004	/* RX chain count valid */
#define	IEEE80211_R_C_NF	0x00000008	/* per-chain NF value valid */
#define	IEEE80211_R_C_RSSI	0x00000010	/* per-chain RSSI value valid */
#define	IEEE80211_R_C_EVM	0x00000020	/* per-chain EVM valid */
#define	IEEE80211_R_C_HT40	0x00000040	/* RX'ed packet is 40mhz, pilots 4,5 valid */
#define	IEEE80211_R_FREQ	0x00000080	/* Freq value populated, MHz */
#define	IEEE80211_R_IEEE	0x00000100	/* IEEE value populated */
#define	IEEE80211_R_BAND	0x00000200	/* Frequency band populated */
#define	IEEE80211_R_TSF32	0x00004000	/* 32 bit TSF */
#define	IEEE80211_R_TSF64	0x00008000	/* 64 bit TSF */
#define	IEEE80211_R_TSF_START	0x00010000	/* TSF is sampled at start of frame */
#define	IEEE80211_R_TSF_END	0x00020000	/* TSF is sampled at end of frame */

/*
 * RX status notification - describe the packet.
 */
#define	IEEE80211_RX_F_STBC		0x00000001
#define	IEEE80211_RX_F_LDPC		0x00000002
#define	IEEE80211_RX_F_AMSDU		0x00000004 /* This is the start of an decap AMSDU list */
#define	IEEE80211_RX_F_AMSDU_MORE	0x00000008 /* This is another decap AMSDU frame in the batch */
#define	IEEE80211_RX_F_AMPDU		0x00000010 /* This is the start of an decap AMPDU list */
#define	IEEE80211_RX_F_AMPDU_MORE	0x00000020 /* This is another decap AMPDU frame in the batch */
#define	IEEE80211_RX_F_FAIL_FCSCRC	0x00000040 /* Failed CRC/FCS */
#define	IEEE80211_RX_F_FAIL_MIC		0x00000080 /* Failed MIC check */
#define	IEEE80211_RX_F_DECRYPTED	0x00000100 /* Hardware decrypted */
#define	IEEE80211_RX_F_IV_STRIP		0x00000200 /* Decrypted; IV stripped */
#define	IEEE80211_RX_F_MMIC_STRIP	0x00000400 /* Decrypted; MMIC stripped */
#define	IEEE80211_RX_F_SHORTGI		0x00000800 /* This is a short-GI frame */
#define	IEEE80211_RX_F_CCK		0x00001000
#define	IEEE80211_RX_F_OFDM		0x00002000
#define	IEEE80211_RX_F_HT		0x00004000
#define	IEEE80211_RX_F_VHT		0x00008000

/* Channel width */
#define	IEEE80211_RX_FW_20MHZ		1
#define	IEEE80211_RX_FW_40MHZ		2
#define	IEEE80211_RX_FW_80MHZ		3

/* PHY type */
#define	IEEE80211_RX_FP_11B		1
#define	IEEE80211_RX_FP_11G		2
#define	IEEE80211_RX_FP_11A		3
#define	IEEE80211_RX_FP_11NA		4
#define	IEEE80211_RX_FP_11NG		5

struct ieee80211_rx_stats {
	uint32_t r_flags;		/* IEEE80211_R_* flags */
	uint32_t c_pktflags;		/* IEEE80211_RX_F_* flags */

	uint64_t c_rx_tsf;		/* 32 or 64 bit TSF */

	/* All DWORD aligned */
	int16_t c_nf_ctl[IEEE80211_MAX_CHAINS];	/* per-chain NF */
	int16_t c_nf_ext[IEEE80211_MAX_CHAINS];	/* per-chain NF */
	int16_t c_rssi_ctl[IEEE80211_MAX_CHAINS];	/* per-chain RSSI */
	int16_t c_rssi_ext[IEEE80211_MAX_CHAINS];	/* per-chain RSSI */

	/* 32 bits */
	uint8_t c_nf;			/* global NF */
	uint8_t c_rssi;			/* global RSSI */
	uint8_t c_chain;		/* number of RX chains involved */
	uint8_t c_rate;			/* legacy; 11n rate code; VHT MCS */

	/* 32 bits */
	uint16_t c_freq;		/* Frequency, MHz */
	uint8_t c_ieee;			/* Channel */
	uint8_t c_width;		/* channel width, FW flags above */

	/* Force alignment to DWORD */
	union {
		uint8_t evm[IEEE80211_MAX_CHAINS][IEEE80211_MAX_EVM_PILOTS];
		    /* per-chain, per-pilot EVM values */
		uint32_t __aln[8];
	} evm;

	/* 32 bits */
	uint8_t c_phytype;		/* PHY type, FW flags above */
	uint8_t c_vhtnss;		/* VHT - number of spatial streams */
	uint8_t c_pad2[2];
};

struct ieee80211_rx_params {
	struct ieee80211_rx_stats params;
};

#endif /* _NET80211__IEEE80211_H_ */
