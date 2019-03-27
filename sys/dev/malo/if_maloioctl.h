/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007 Marvell Semiconductor, Inc.
 * Copyright (c) 2007 Sam Leffler, Errno Consulting
 * Copyright (c) 2008 Weongyo Jeong <weongyo@freebsd.org>
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

/*
 * Ioctl-related defintions for the Marvel Wireless LAN controller driver.
 */
#ifndef _DEV_MALO_MVIOCTL_H
#define _DEV_MALO_MVIOCTL_H

struct malo_stats {
	struct malo_hal_hwstats hw_stats;	/* XXX tied to h/w defs */
	uint32_t	mst_failure;	/* generic hardware failure */
	uint32_t	mst_rx_badtkipicv;
	uint32_t	mst_tx_discard;
	uint32_t	mst_tx_qstop;
	uint32_t	mst_tx_encap;
	uint32_t	mst_tx_mgmt;
	uint32_t	mst_rx_nombuf;
	uint32_t	mst_rx_busdma;
	uint32_t	mst_rx_tooshort;
	uint32_t	mst_tx_busdma;
	uint32_t	mst_tx_linear;
	uint32_t	mst_tx_nombuf;
	uint32_t	mst_tx_nodata;
	uint32_t	mst_tx_shortpre;
	uint32_t	mst_tx_retries;
	uint32_t	mst_tx_mretries;
	uint32_t	mst_tx_linkerror;
	uint32_t	mst_tx_xretries;
	uint32_t	mst_tx_aging;
	uint32_t	mst_watchdog;
	uint32_t	mst_tx_packets;
	uint32_t	mst_rx_packets;
	int8_t		mst_rx_rssi;
	int8_t		mst_rx_noise;
	uint8_t		mst_tx_rate;
	uint32_t	mst_ant_tx[4];
	uint32_t	mst_ant_rx[4];
};

#define	SIOCGMVSTATS	_IOWR('i', 137, struct ifreq)

/*
 * Radio capture format.
 */
#define MALO_RX_RADIOTAP_PRESENT (		\
	(1 << IEEE80211_RADIOTAP_FLAGS)		| \
	(1 << IEEE80211_RADIOTAP_RATE)		| \
	(1 << IEEE80211_RADIOTAP_CHANNEL)	| \
	(1 << IEEE80211_RADIOTAP_ANTENNA)	| \
	(1 << IEEE80211_RADIOTAP_DBM_ANTSIGNAL)	| \
	(1 << IEEE80211_RADIOTAP_DBM_ANTNOISE)	| \
	0)

struct malo_rx_radiotap_header {
	struct ieee80211_radiotap_header wr_ihdr;
	u_int8_t	wr_flags;
	u_int8_t	wr_rate;
	u_int16_t	wr_chan_freq;
	u_int16_t	wr_chan_flags;
	int8_t		wr_antsignal;
	int8_t		wr_antnoise;
	u_int8_t	wr_antenna;
} __packed __aligned(8);

#define MALO_TX_RADIOTAP_PRESENT (		\
	(1 << IEEE80211_RADIOTAP_FLAGS)		| \
	(1 << IEEE80211_RADIOTAP_RATE)		| \
	(1 << IEEE80211_RADIOTAP_CHANNEL)	| \
	(1 << IEEE80211_RADIOTAP_DBM_TX_POWER)	| \
	(1 << IEEE80211_RADIOTAP_ANTENNA)	| \
	0)

struct malo_tx_radiotap_header {
	struct ieee80211_radiotap_header wt_ihdr;
	u_int8_t	wt_flags;
	u_int8_t	wt_rate;
	u_int16_t	wt_chan_freq;
	u_int16_t	wt_chan_flags;
	u_int8_t	wt_txpower;
	u_int8_t	wt_antenna;
} __packed;

#endif /* _DEV_MALO_MVIOCTL_H */
