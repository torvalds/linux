/* $FreeBSD$ */
/* $NetBSD: ieee80211_radiotap.h,v 1.16 2007/01/06 05:51:15 dyoung Exp $ */

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2003, 2004 David Young.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of David Young may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY DAVID YOUNG ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL DAVID
 * YOUNG BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */
#ifndef _NET80211_IEEE80211_RADIOTAP_H_
#define _NET80211_IEEE80211_RADIOTAP_H_

/* A generic radio capture format is desirable. It must be
 * rigidly defined (e.g., units for fields should be given),
 * and easily extensible.
 *
 * The following is an extensible radio capture format. It is
 * based on a bitmap indicating which fields are present.
 *
 * I am trying to describe precisely what the application programmer
 * should expect in the following, and for that reason I tell the
 * units and origin of each measurement (where it applies), or else I
 * use sufficiently weaselly language ("is a monotonically nondecreasing
 * function of...") that I cannot set false expectations for lawyerly
 * readers.
 */
#if defined(__KERNEL__) || defined(_KERNEL)
#ifndef DLT_IEEE802_11_RADIO
#define	DLT_IEEE802_11_RADIO	127	/* 802.11 plus WLAN header */
#endif
#endif /* defined(__KERNEL__) || defined(_KERNEL) */

#define	IEEE80211_RADIOTAP_HDRLEN	64	/* XXX deprecated */

struct ieee80211_radiotap_vendor_header {
	uint8_t		vh_oui[3];	/* 3 byte vendor OUI */
	uint8_t		vh_sub_ns;	/* Sub namespace of this section */
	uint16_t	vh_skip_len;	/* Length of this vendor section */
} __packed;

/*
 * The radio capture header precedes the 802.11 header.
 *
 * Note well: all radiotap fields are little-endian.
 */
struct ieee80211_radiotap_header {
	uint8_t		it_version;	/* Version 0. Only increases
					 * for drastic changes,
					 * introduction of compatible
					 * new fields does not count.
					 */
	uint8_t		it_pad;
	uint16_t	it_len;		/* length of the whole
					 * header in bytes, including
					 * it_version, it_pad,
					 * it_len, and data fields.
					 */
	uint32_t	it_present;	/* A bitmap telling which
					 * fields are present. Set bit 31
					 * (0x80000000) to extend the
					 * bitmap by another 32 bits.
					 * Additional extensions are made
					 * by setting bit 31.
					 */
} __packed;

/*
 * Name                                 Data type       Units
 * ----                                 ---------       -----
 *
 * IEEE80211_RADIOTAP_TSFT              uint64_t        microseconds
 *
 *      Value in microseconds of the MAC's 64-bit 802.11 Time
 *      Synchronization Function timer when the first bit of the
 *      MPDU arrived at the MAC. For received frames, only.
 *
 * IEEE80211_RADIOTAP_CHANNEL           2 x uint16_t    MHz, bitmap
 *
 *      Tx/Rx frequency in MHz, followed by flags (see below).
 *
 * IEEE80211_RADIOTAP_FHSS              uint16_t        see below
 *
 *      For frequency-hopping radios, the hop set (first byte)
 *      and pattern (second byte).
 *
 * IEEE80211_RADIOTAP_RATE              uint8_t         500kb/s or index
 *
 *      Tx/Rx data rate.  If bit 0x80 is set then it represents an
 *	an MCS index and not an IEEE rate.
 *
 * IEEE80211_RADIOTAP_DBM_ANTSIGNAL     int8_t          decibels from
 *                                                      one milliwatt (dBm)
 *
 *      RF signal power at the antenna, decibel difference from
 *      one milliwatt.
 *
 * IEEE80211_RADIOTAP_DBM_ANTNOISE      int8_t          decibels from
 *                                                      one milliwatt (dBm)
 *
 *      RF noise power at the antenna, decibel difference from one
 *      milliwatt.
 *
 * IEEE80211_RADIOTAP_DB_ANTSIGNAL      uint8_t         decibel (dB)
 *
 *      RF signal power at the antenna, decibel difference from an
 *      arbitrary, fixed reference.
 *
 * IEEE80211_RADIOTAP_DB_ANTNOISE       uint8_t         decibel (dB)
 *
 *      RF noise power at the antenna, decibel difference from an
 *      arbitrary, fixed reference point.
 *
 * IEEE80211_RADIOTAP_LOCK_QUALITY      uint16_t        unitless
 *
 *      Quality of Barker code lock. Unitless. Monotonically
 *      nondecreasing with "better" lock strength. Called "Signal
 *      Quality" in datasheets.  (Is there a standard way to measure
 *      this?)
 *
 * IEEE80211_RADIOTAP_TX_ATTENUATION    uint16_t        unitless
 *
 *      Transmit power expressed as unitless distance from max
 *      power set at factory calibration.  0 is max power.
 *      Monotonically nondecreasing with lower power levels.
 *
 * IEEE80211_RADIOTAP_DB_TX_ATTENUATION uint16_t        decibels (dB)
 *
 *      Transmit power expressed as decibel distance from max power
 *      set at factory calibration.  0 is max power.  Monotonically
 *      nondecreasing with lower power levels.
 *
 * IEEE80211_RADIOTAP_DBM_TX_POWER      int8_t          decibels from
 *                                                      one milliwatt (dBm)
 *
 *      Transmit power expressed as dBm (decibels from a 1 milliwatt
 *      reference). This is the absolute power level measured at
 *      the antenna port.
 *
 * IEEE80211_RADIOTAP_FLAGS             uint8_t         bitmap
 *
 *      Properties of transmitted and received frames. See flags
 *      defined below.
 *
 * IEEE80211_RADIOTAP_ANTENNA           uint8_t         antenna index
 *
 *      Unitless indication of the Rx/Tx antenna for this packet.
 *      The first antenna is antenna 0.
 *
 * IEEE80211_RADIOTAP_XCHANNEL          uint32_t        bitmap
 *                                      uint16_t        MHz
 *                                      uint8_t         channel number
 *                                      int8_t          .5 dBm
 *
 *      Extended channel specification: flags (see below) followed by
 *      frequency in MHz, the corresponding IEEE channel number, and
 *      finally the maximum regulatory transmit power cap in .5 dBm
 *      units.  This property supersedes IEEE80211_RADIOTAP_CHANNEL
 *      and only one of the two should be present.
 * IEEE80211_RADIOTAP_RX_FLAGS          guint16       bitmap
 *
 *     Properties of received frames. See flags defined below.
 *
 * IEEE80211_RADIOTAP_TX_FLAGS          guint16       bitmap
 *
 *     Properties of transmitted frames. See flags defined below.
 *
 * IEEE80211_RADIOTAP_RTS_RETRIES       u8           data
 *
 *     Number of rts retries a transmitted frame used.
 *
 * IEEE80211_RADIOTAP_DATA_RETRIES      u8           data
 *
 *     Number of unicast retries a transmitted frame used.
 *
 * IEEE80211_RADIOTAP_MCS       u8, u8, u8              unitless
 *
 *     Contains a bitmap of known fields/flags, the flags, and
 *     the MCS index.
 *
 * IEEE80211_RADIOTAP_AMPDU_STATUS      u32, u16, u8, u8        unitlesss
 *
 *      Contains the AMPDU information for the subframe.
 */
enum ieee80211_radiotap_type {
	IEEE80211_RADIOTAP_TSFT = 0,
	IEEE80211_RADIOTAP_FLAGS = 1,
	IEEE80211_RADIOTAP_RATE = 2,
	IEEE80211_RADIOTAP_CHANNEL = 3,
	IEEE80211_RADIOTAP_FHSS = 4,
	IEEE80211_RADIOTAP_DBM_ANTSIGNAL = 5,
	IEEE80211_RADIOTAP_DBM_ANTNOISE = 6,
	IEEE80211_RADIOTAP_LOCK_QUALITY = 7,
	IEEE80211_RADIOTAP_TX_ATTENUATION = 8,
	IEEE80211_RADIOTAP_DB_TX_ATTENUATION = 9,
	IEEE80211_RADIOTAP_DBM_TX_POWER = 10,
	IEEE80211_RADIOTAP_ANTENNA = 11,
	IEEE80211_RADIOTAP_DB_ANTSIGNAL = 12,
	IEEE80211_RADIOTAP_DB_ANTNOISE = 13,
	/*
	 * 14-17 are from Linux, they overlap the netbsd-specific
	 * fields.
	 */
	IEEE80211_RADIOTAP_RX_FLAGS = 14,
	IEEE80211_RADIOTAP_TX_FLAGS = 15,
	IEEE80211_RADIOTAP_RTS_RETRIES = 16,
	IEEE80211_RADIOTAP_DATA_RETRIES = 17,

	IEEE80211_RADIOTAP_XCHANNEL = 18,
	IEEE80211_RADIOTAP_MCS = 19,
	IEEE80211_RADIOTAP_AMPDU_STATUS = 20,
	IEEE80211_RADIOTAP_VHT = 21,

        IEEE80211_RADIOTAP_RADIOTAP_NAMESPACE = 29,
	IEEE80211_RADIOTAP_VENDOREXT = 30,
	IEEE80211_RADIOTAP_EXT = 31,
};

#ifndef _KERNEL
/* channel attributes */
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
#endif /* !_KERNEL */

/* For IEEE80211_RADIOTAP_FLAGS */
#define	IEEE80211_RADIOTAP_F_CFP	0x01	/* sent/received
						 * during CFP
						 */
#define	IEEE80211_RADIOTAP_F_SHORTPRE	0x02	/* sent/received
						 * with short
						 * preamble
						 */
#define	IEEE80211_RADIOTAP_F_WEP	0x04	/* sent/received
						 * with WEP encryption
						 */
#define	IEEE80211_RADIOTAP_F_FRAG	0x08	/* sent/received
						 * with fragmentation
						 */
#define	IEEE80211_RADIOTAP_F_FCS	0x10	/* frame includes FCS */
#define	IEEE80211_RADIOTAP_F_DATAPAD	0x20	/* frame has padding between
						 * 802.11 header and payload
						 * (to 32-bit boundary)
						 */
#define	IEEE80211_RADIOTAP_F_BADFCS	0x40	/* does not pass FCS check */
#define	IEEE80211_RADIOTAP_F_SHORTGI	0x80	/* HT short GI */

/* For IEEE80211_RADIOTAP_RX_FLAGS */
#define	IEEE80211_RADIOTAP_F_RX_BADPLCP	0x0002	/* bad PLCP */

/* For IEEE80211_RADIOTAP_TX_FLAGS */
#define	IEEE80211_RADIOTAP_F_TX_FAIL	0x0001	/* failed due to excessive
						 * retries */
#define	IEEE80211_RADIOTAP_F_TX_CTS	0x0002	/* used cts 'protection' */
#define	IEEE80211_RADIOTAP_F_TX_RTS	0x0004	/* used rts/cts handshake */


/* For IEEE80211_RADIOTAP_MCS */
#define	IEEE80211_RADIOTAP_MCS_HAVE_BW		0x01
#define	IEEE80211_RADIOTAP_MCS_HAVE_MCS		0x02
#define	IEEE80211_RADIOTAP_MCS_HAVE_GI		0x04
#define	IEEE80211_RADIOTAP_MCS_HAVE_FMT		0x08
#define	IEEE80211_RADIOTAP_MCS_HAVE_FEC		0x10
#define	IEEE80211_RADIOTAP_MCS_HAVE_STBC	0x20
#define	IEEE80211_RADIOTAP_MCS_HAVE_NESS	0x40
#define	IEEE80211_RADIOTAP_MCS_NESS_BIT1	0x80

#define	IEEE80211_RADIOTAP_MCS_BW_MASK		0x03
#define	    IEEE80211_RADIOTAP_MCS_BW_20	0
#define	    IEEE80211_RADIOTAP_MCS_BW_40	1
#define	 IEEE80211_RADIOTAP_MCS_BW_20L		2
#define	    IEEE80211_RADIOTAP_MCS_BW_20U	3
#define	IEEE80211_RADIOTAP_MCS_SGI		0x04
#define	IEEE80211_RADIOTAP_MCS_FMT_GF		0x08
#define	IEEE80211_RADIOTAP_MCS_FEC_LDPC		0x10
#define	IEEE80211_RADIOTAP_MCS_STBC_MASK	0x60
#define	IEEE80211_RADIOTAP_MCS_STBC_SHIFT	5
#define	    IEEE80211_RADIOTAP_MCS_STBC_1	1
#define	    IEEE80211_RADIOTAP_MCS_STBC_2	2
#define	    IEEE80211_RADIOTAP_MCS_STBC_3	3
#define	IEEE80211_RADIOTAP_MCS_NESS_BIT0	0x80

/* For IEEE80211_RADIOTAP_AMPDU_STATUS */
#define	IEEE80211_RADIOTAP_AMPDU_REPORT_ZEROLEN		0x0001
#define	IEEE80211_RADIOTAP_AMPDU_IS_ZEROLEN		0x0002
#define	IEEE80211_RADIOTAP_AMPDU_LAST_KNOWN		0x0004
#define	IEEE80211_RADIOTAP_AMPDU_IS_LAST		0x0008
#define	IEEE80211_RADIOTAP_AMPDU_DELIM_CRC_ERR		0x0010
#define	IEEE80211_RADIOTAP_AMPDU_DELIM_CRC_KNOWN	0x0020

/* For IEEE80211_RADIOTAP_VHT */
#define	IEEE80211_RADIOTAP_VHT_HAVE_STBC	0x0001
#define	IEEE80211_RADIOTAP_VHT_HAVE_TXOP_PS	0x0002
#define	IEEE80211_RADIOTAP_VHT_HAVE_GI		0x0004
#define	IEEE80211_RADIOTAP_VHT_HAVE_SGI_NSYM_DA	0x0008
#define	IEEE80211_RADIOTAP_VHT_HAVE_LDPC_EXTRA	0x0010
#define	IEEE80211_RADIOTAP_VHT_HAVE_BF		0x0020
#define	IEEE80211_RADIOTAP_VHT_HAVE_BW		0x0040
#define	IEEE80211_RADIOTAP_VHT_HAVE_GID		0x0080
#define	IEEE80211_RADIOTAP_VHT_HAVE_PAID	0x0100
#define	IEEE80211_RADIOTAP_VHT_STBC		0x01
#define	IEEE80211_RADIOTAP_VHT_TXOP_PS		0x02
#define	IEEE80211_RADIOTAP_VHT_SGI		0x04
#define	IEEE80211_RADIOTAP_VHT_SGI_NSYM_DA	0x08
#define	IEEE80211_RADIOTAP_VHT_LDPC_EXTRA	0x10
#define	IEEE80211_RADIOTAP_VHT_BF		0x20
#define	IEEE80211_RADIOTAP_VHT_NSS		0x0f
#define	IEEE80211_RADIOTAP_VHT_MCS		0xf0
#define	IEEE80211_RADIOTAP_VHT_CODING_LDPC	0x01

#define	IEEE80211_RADIOTAP_VHT_BW_MASK		0x1f
#define	IEEE80211_RADIOTAP_VHT_BW_20		IEEE80211_RADIOTAP_MCS_BW_20
#define	IEEE80211_RADIOTAP_VHT_BW_40		IEEE80211_RADIOTAP_MCS_BW_40
#define	IEEE80211_RADIOTAP_VHT_BW_20L		IEEE80211_RADIOTAP_MCS_BW_20L
#define	IEEE80211_RADIOTAP_VHT_BW_20U		IEEE80211_RADIOTAP_MCS_BW_20U
#define	IEEE80211_RADIOTAP_VHT_BW_80		4
#define	IEEE80211_RADIOTAP_VHT_BW_40L		5
#define	IEEE80211_RADIOTAP_VHT_BW_40U		6
#define	IEEE80211_RADIOTAP_VHT_BW_20LL		7
#define	IEEE80211_RADIOTAP_VHT_BW_20LU		8
#define	IEEE80211_RADIOTAP_VHT_BW_20UL		9
#define	IEEE80211_RADIOTAP_VHT_BW_20UU		10
#define	IEEE80211_RADIOTAP_VHT_BW_160		11
#define	IEEE80211_RADIOTAP_VHT_BW_80L		12
#define	IEEE80211_RADIOTAP_VHT_BW_80U		13
#define	IEEE80211_RADIOTAP_VHT_BW_40LL		14
#define	IEEE80211_RADIOTAP_VHT_BW_40LU		15
#define	IEEE80211_RADIOTAP_VHT_BW_40UL		16
#define	IEEE80211_RADIOTAP_VHT_BW_40UU		17
#define	IEEE80211_RADIOTAP_VHT_BW_20LLL		18
#define	IEEE80211_RADIOTAP_VHT_BW_20LLU		19
#define	IEEE80211_RADIOTAP_VHT_BW_20LUL		20
#define	IEEE80211_RADIOTAP_VHT_BW_20LUU		21
#define	IEEE80211_RADIOTAP_VHT_BW_20ULL		22
#define	IEEE80211_RADIOTAP_VHT_BW_20ULU		23
#define	IEEE80211_RADIOTAP_VHT_BW_20UUL		24
#define	IEEE80211_RADIOTAP_VHT_BW_20UUU		25

#endif /* !_NET80211_IEEE80211_RADIOTAP_H_ */
