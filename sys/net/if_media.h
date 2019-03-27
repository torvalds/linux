/*	$NetBSD: if_media.h,v 1.3 1997/03/26 01:19:27 thorpej Exp $	*/
/* $FreeBSD$ */

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1997
 *	Jonathan Stone and Jason R. Thorpe.  All rights reserved.
 *
 * This software is derived from information provided by Matt Thomas.
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
 *	This product includes software developed by Jonathan Stone
 *	and Jason R. Thorpe for the NetBSD Project.
 * 4. The names of the authors may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _NET_IF_MEDIA_H_
#define	_NET_IF_MEDIA_H_

/*
 * Prototypes and definitions for BSD/OS-compatible network interface
 * media selection.
 *
 * Where it is safe to do so, this code strays slightly from the BSD/OS
 * design.  Software which uses the API (device drivers, basically)
 * shouldn't notice any difference.
 *
 * Many thanks to Matt Thomas for providing the information necessary
 * to implement this interface.
 */

#ifdef _KERNEL

#include <sys/queue.h>

struct ifnet;

/*
 * Driver callbacks for media status and change requests.
 */
typedef	int (*ifm_change_cb_t)(struct ifnet *);
typedef	void (*ifm_stat_cb_t)(struct ifnet *, struct ifmediareq *req);

/*
 * In-kernel representation of a single supported media type.
 */
struct ifmedia_entry {
	LIST_ENTRY(ifmedia_entry) ifm_list;
	int	ifm_media;	/* description of this media attachment */
	int	ifm_data;	/* for driver-specific use */
	void	*ifm_aux;	/* for driver-specific use */
};

/*
 * One of these goes into a network interface's softc structure.
 * It is used to keep general media state.
 */
struct ifmedia {
	int	ifm_mask;	/* mask of changes we don't care about */
	int	ifm_media;	/* current user-set media word */
	struct ifmedia_entry *ifm_cur;	/* currently selected media */
	LIST_HEAD(, ifmedia_entry) ifm_list; /* list of all supported media */
	ifm_change_cb_t	ifm_change;	/* media change driver callback */
	ifm_stat_cb_t	ifm_status;	/* media status driver callback */
};

/* Initialize an interface's struct if_media field. */
void	ifmedia_init(struct ifmedia *ifm, int dontcare_mask,
	    ifm_change_cb_t change_callback, ifm_stat_cb_t status_callback);

/* Remove all mediums from a struct ifmedia.  */
void	ifmedia_removeall( struct ifmedia *ifm);

/* Add one supported medium to a struct ifmedia. */
void	ifmedia_add(struct ifmedia *ifm, int mword, int data, void *aux);

/* Add an array (of ifmedia_entry) media to a struct ifmedia. */
void	ifmedia_list_add(struct ifmedia *mp, struct ifmedia_entry *lp,
	    int count);

/* Set default media type on initialization. */
void	ifmedia_set(struct ifmedia *ifm, int mword);

/* Common ioctl function for getting/setting media, called by driver. */
int	ifmedia_ioctl(struct ifnet *ifp, struct ifreq *ifr,
	    struct ifmedia *ifm, u_long cmd);


/* Compute baudrate for a given media. */
uint64_t	ifmedia_baudrate(int);

#endif /*_KERNEL */

/*
 * if_media Options word:
 *	Bits	Use
 *	----	-------
 *	0-4	Media variant
 *	5-7	Media type
 *	8-15	Type specific options (includes added variant bits on Ethernet)
 *	16-18	Mode (for multi-mode devices)
 *	19	RFU
 *	20-27	Shared (global) options
 *	28-31	Instance
 */

/*
 * Ethernet
 * In order to use more than 31 subtypes, Ethernet uses some of the option
 * bits as part of the subtype field.  See the options section below for
 * relevant definitions
 */
#define	IFM_ETHER	0x00000020
#define	IFM_ETHER_SUBTYPE(x) (((x) & IFM_TMASK) | \
	(((x) & (IFM_ETH_XTYPE >> IFM_ETH_XSHIFT)) << IFM_ETH_XSHIFT))
#define	IFM_X(x) IFM_ETHER_SUBTYPE(x)	/* internal shorthand */
#define	IFM_ETHER_SUBTYPE_SET(x) (IFM_ETHER_SUBTYPE(x) | IFM_ETHER)
#define	IFM_ETHER_SUBTYPE_GET(x) ((x) & (IFM_TMASK|IFM_ETH_XTYPE))
#define	IFM_ETHER_IS_EXTENDED(x)	((x) & IFM_ETH_XTYPE)

#define	IFM_10_T	3		/* 10BaseT - RJ45 */
#define	IFM_10_2	4		/* 10Base2 - Thinnet */
#define	IFM_10_5	5		/* 10Base5 - AUI */
#define	IFM_100_TX	6		/* 100BaseTX - RJ45 */
#define	IFM_100_FX	7		/* 100BaseFX - Fiber */
#define	IFM_100_T4	8		/* 100BaseT4 - 4 pair cat 3 */
#define	IFM_100_VG	9		/* 100VG-AnyLAN */
#define	IFM_100_T2	10		/* 100BaseT2 */
#define	IFM_1000_SX	11		/* 1000BaseSX - multi-mode fiber */
#define	IFM_10_STP	12		/* 10BaseT over shielded TP */
#define	IFM_10_FL	13		/* 10BaseFL - Fiber */
#define	IFM_1000_LX	14		/* 1000baseLX - single-mode fiber */
#define	IFM_1000_CX	15		/* 1000baseCX - 150ohm STP */
#define	IFM_1000_T	16		/* 1000baseT - 4 pair cat 5 */
#define	IFM_HPNA_1	17		/* HomePNA 1.0 (1Mb/s) */
#define	IFM_10G_LR	18		/* 10GBase-LR 1310nm Single-mode */
#define	IFM_10G_SR	19		/* 10GBase-SR 850nm Multi-mode */
#define	IFM_10G_CX4	20		/* 10GBase CX4 copper */
#define	IFM_2500_SX	21		/* 2500BaseSX - multi-mode fiber */
#define	IFM_10G_TWINAX	22		/* 10GBase Twinax copper */
#define	IFM_10G_TWINAX_LONG	23	/* 10GBase Twinax Long copper */
#define	IFM_10G_LRM	24		/* 10GBase-LRM 850nm Multi-mode */
#define	IFM_UNKNOWN	25		/* media types not defined yet */
#define	IFM_10G_T	26		/* 10GBase-T - RJ45 */
#define	IFM_40G_CR4	27		/* 40GBase-CR4 */
#define	IFM_40G_SR4	28		/* 40GBase-SR4 */
#define	IFM_40G_LR4	29		/* 40GBase-LR4 */
#define	IFM_1000_KX	30		/* 1000Base-KX backplane */
#define	IFM_OTHER	31		/* Other: one of the following */

/* following types are not visible to old binaries using only IFM_TMASK */
#define	IFM_10G_KX4	IFM_X(32)	/* 10GBase-KX4 backplane */
#define	IFM_10G_KR	IFM_X(33)	/* 10GBase-KR backplane */
#define	IFM_10G_CR1	IFM_X(34)	/* 10GBase-CR1 Twinax splitter */
#define	IFM_20G_KR2	IFM_X(35)	/* 20GBase-KR2 backplane */
#define	IFM_2500_KX	IFM_X(36)	/* 2500Base-KX backplane */
#define	IFM_2500_T	IFM_X(37)	/* 2500Base-T - RJ45 (NBaseT) */
#define	IFM_5000_T	IFM_X(38)	/* 5000Base-T - RJ45 (NBaseT) */
#define	IFM_50G_PCIE	IFM_X(39)	/* 50G Ethernet over PCIE */
#define	IFM_25G_PCIE	IFM_X(40)	/* 25G Ethernet over PCIE */
#define	IFM_1000_SGMII	IFM_X(41)	/* 1G media interface */
#define	IFM_10G_SFI	IFM_X(42)	/* 10G media interface */
#define	IFM_40G_XLPPI	IFM_X(43)	/* 40G media interface */
#define	IFM_1000_CX_SGMII IFM_X(44)	/* 1000Base-CX-SGMII */
#define	IFM_40G_KR4	IFM_X(45)	/* 40GBase-KR4 */
#define	IFM_10G_ER	IFM_X(46)	/* 10GBase-ER */
#define	IFM_100G_CR4	IFM_X(47)	/* 100GBase-CR4 */
#define	IFM_100G_SR4	IFM_X(48)	/* 100GBase-SR4 */
#define	IFM_100G_KR4	IFM_X(49)	/* 100GBase-KR4 */
#define	IFM_100G_LR4	IFM_X(50)	/* 100GBase-LR4 */
#define	IFM_56G_R4	IFM_X(51)	/* 56GBase-R4 */
#define	IFM_100_T	IFM_X(52)	/* 100BaseT - RJ45 */
#define	IFM_25G_CR	IFM_X(53)	/* 25GBase-CR */
#define	IFM_25G_KR	IFM_X(54)	/* 25GBase-KR */
#define	IFM_25G_SR	IFM_X(55)	/* 25GBase-SR */
#define	IFM_50G_CR2	IFM_X(56)	/* 50GBase-CR2 */
#define	IFM_50G_KR2	IFM_X(57)	/* 50GBase-KR2 */
#define	IFM_25G_LR	IFM_X(58)	/* 25GBase-LR */
#define	IFM_10G_AOC	IFM_X(59)	/* 10G active optical cable */
#define	IFM_25G_ACC	IFM_X(60)	/* 25G active copper cable */
#define	IFM_25G_AOC	IFM_X(61)	/* 25G active optical cable */
#define	IFM_100_SGMII	IFM_X(62)	/* 100M media interface */
#define	IFM_2500_X	IFM_X(63)	/* 2500BaseX */
#define	IFM_5000_KR	IFM_X(64)	/* 5GBase-KR backplane */
#define	IFM_25G_T	IFM_X(65)	/* 25GBase-T - RJ45 */
#define	IFM_25G_CR_S	IFM_X(66)	/* 25GBase-CR (short) */
#define	IFM_25G_CR1	IFM_X(67)	/* 25GBase-CR1 DA cable */
#define	IFM_25G_KR_S	IFM_X(68)	/* 25GBase-KR (short) */
#define	IFM_5000_KR_S	IFM_X(69)	/* 5GBase-KR backplane (short) */
#define	IFM_5000_KR1	IFM_X(70)	/* 5GBase-KR backplane */
#define	IFM_25G_AUI	IFM_X(71)	/* 25G-AUI-C2C (chip to chip) */
#define	IFM_40G_XLAUI	IFM_X(72)	/* 40G-XLAUI */
#define	IFM_40G_XLAUI_AC IFM_X(73)	/* 40G active copper/optical */
#define	IFM_40G_ER4	IFM_X(74)	/* 40GBase-ER4 */
#define	IFM_50G_SR2	IFM_X(75)	/* 50GBase-SR2 */
#define	IFM_50G_LR2	IFM_X(76)	/* 50GBase-LR2 */
#define	IFM_50G_LAUI2_AC IFM_X(77)	/* 50G active copper/optical */
#define	IFM_50G_LAUI2	IFM_X(78)	/* 50G-LAUI2 */
#define	IFM_50G_AUI2_AC	IFM_X(79)	/* 50G active copper/optical */
#define	IFM_50G_AUI2	IFM_X(80)	/* 50G-AUI2 */
#define	IFM_50G_CP	IFM_X(81)	/* 50GBase-CP */
#define	IFM_50G_SR	IFM_X(82)	/* 50GBase-SR */
#define	IFM_50G_LR	IFM_X(83)	/* 50GBase-LR */
#define	IFM_50G_FR	IFM_X(84)	/* 50GBase-FR */
#define	IFM_50G_KR_PAM4	IFM_X(85)	/* 50GBase-KR PAM4 */
#define	IFM_25G_KR1	IFM_X(86)	/* 25GBase-KR1 */
#define	IFM_50G_AUI1_AC	IFM_X(87)	/* 50G active copper/optical */
#define	IFM_50G_AUI1	IFM_X(88)	/* 50G-AUI1 */
#define	IFM_100G_CAUI4_AC IFM_X(89)	/* 100G-CAUI4 active copper/optical */
#define	IFM_100G_CAUI4 IFM_X(90)	/* 100G-CAUI4 */
#define	IFM_100G_AUI4_AC IFM_X(91)	/* 100G-AUI4 active copper/optical */
#define	IFM_100G_AUI4	IFM_X(92)	/* 100G-AUI4 */
#define	IFM_100G_CR_PAM4 IFM_X(93)	/* 100GBase-CR PAM4 */
#define	IFM_100G_KR_PAM4 IFM_X(94)	/* 100GBase-CR PAM4 */
#define	IFM_100G_CP2	IFM_X(95)	/* 100GBase-CP2 */
#define	IFM_100G_SR2	IFM_X(96)	/* 100GBase-SR2 */
#define	IFM_100G_DR	IFM_X(97)	/* 100GBase-DR */
#define	IFM_100G_KR2_PAM4 IFM_X(98)	/* 100GBase-KR2 PAM4 */
#define	IFM_100G_CAUI2_AC IFM_X(99)	/* 100G-CAUI2 active copper/optical */
#define	IFM_100G_CAUI2	IFM_X(100)	/* 100G-CAUI2 */
#define	IFM_100G_AUI2_AC IFM_X(101)	/* 100G-AUI2 active copper/optical */
#define	IFM_100G_AUI2	IFM_X(102)	/* 100G-AUI2 */
#define	IFM_200G_CR4_PAM4 IFM_X(103)	/* 200GBase-CR4 PAM4 */
#define	IFM_200G_SR4	IFM_X(104)	/* 200GBase-SR4 */
#define	IFM_200G_FR4	IFM_X(105)	/* 200GBase-FR4 */
#define	IFM_200G_LR4	IFM_X(106)	/* 200GBase-LR4 */
#define	IFM_200G_DR4	IFM_X(107)	/* 200GBase-DR4 */
#define	IFM_200G_KR4_PAM4 IFM_X(108)	/* 200GBase-KR4 PAM4 */
#define	IFM_200G_AUI4_AC IFM_X(109)	/* 200G-AUI4 active copper/optical */
#define	IFM_200G_AUI4	IFM_X(110)	/* 200G-AUI4 */
#define	IFM_200G_AUI8_AC IFM_X(111)	/* 200G-AUI8 active copper/optical */
#define	IFM_200G_AUI8	IFM_X(112)	/* 200G-AUI8 */
#define	IFM_400G_FR8	IFM_X(113)	/* 400GBase-FR8 */
#define	IFM_400G_LR8	IFM_X(114)	/* 400GBase-LR8 */
#define	IFM_400G_DR4	IFM_X(115)	/* 400GBase-DR4 */
#define	IFM_400G_AUI8_AC IFM_X(116)	/* 400G-AUI8 active copper/optical */
#define	IFM_400G_AUI8	IFM_X(117)	/* 400G-AUI8 */

/*
 * Please update ieee8023ad_lacp.c:lacp_compose_key()
 * after adding new Ethernet media types.
 */
/* Note IFM_X(511) is the max! */

/* Ethernet option values; includes bits used for extended variant field */
#define	IFM_ETH_MASTER	0x00000100	/* master mode (1000baseT) */
#define	IFM_ETH_RXPAUSE	0x00000200	/* receive PAUSE frames */
#define	IFM_ETH_TXPAUSE	0x00000400	/* transmit PAUSE frames */
#define	IFM_ETH_XTYPE	0x00007800	/* extended media variants */
#define	IFM_ETH_XSHIFT	6		/* shift XTYPE next to TMASK */

/*
 * IEEE 802.11 Wireless
 */
#define	IFM_IEEE80211	0x00000080
/* NB: 0,1,2 are auto, manual, none defined below */
#define	IFM_IEEE80211_FH1	3	/* Frequency Hopping 1Mbps */
#define	IFM_IEEE80211_FH2	4	/* Frequency Hopping 2Mbps */
#define	IFM_IEEE80211_DS1	5	/* Direct Sequence 1Mbps */
#define	IFM_IEEE80211_DS2	6	/* Direct Sequence 2Mbps */
#define	IFM_IEEE80211_DS5	7	/* Direct Sequence 5.5Mbps */
#define	IFM_IEEE80211_DS11	8	/* Direct Sequence 11Mbps */
#define	IFM_IEEE80211_DS22	9	/* Direct Sequence 22Mbps */
#define	IFM_IEEE80211_OFDM6	10	/* OFDM 6Mbps */
#define	IFM_IEEE80211_OFDM9	11	/* OFDM 9Mbps */
#define	IFM_IEEE80211_OFDM12	12	/* OFDM 12Mbps */
#define	IFM_IEEE80211_OFDM18	13	/* OFDM 18Mbps */
#define	IFM_IEEE80211_OFDM24	14	/* OFDM 24Mbps */
#define	IFM_IEEE80211_OFDM36	15	/* OFDM 36Mbps */
#define	IFM_IEEE80211_OFDM48	16	/* OFDM 48Mbps */
#define	IFM_IEEE80211_OFDM54	17	/* OFDM 54Mbps */
#define	IFM_IEEE80211_OFDM72	18	/* OFDM 72Mbps */
#define	IFM_IEEE80211_DS354k	19	/* Direct Sequence 354Kbps */
#define	IFM_IEEE80211_DS512k	20	/* Direct Sequence 512Kbps */
#define	IFM_IEEE80211_OFDM3	21	/* OFDM 3Mbps */
#define	IFM_IEEE80211_OFDM4	22	/* OFDM 4.5Mbps */
#define	IFM_IEEE80211_OFDM27	23	/* OFDM 27Mbps */
/* NB: not enough bits to express MCS fully */
#define	IFM_IEEE80211_MCS	24	/* HT MCS rate */
#define	IFM_IEEE80211_VHT	25	/* VHT MCS rate */

#define	IFM_IEEE80211_ADHOC	0x00000100	/* Operate in Adhoc mode */
#define	IFM_IEEE80211_HOSTAP	0x00000200	/* Operate in Host AP mode */
#define	IFM_IEEE80211_IBSS	0x00000400	/* Operate in IBSS mode */
#define	IFM_IEEE80211_WDS	0x00000800	/* Operate in WDS mode */
#define	IFM_IEEE80211_TURBO	0x00001000	/* Operate in turbo mode */
#define	IFM_IEEE80211_MONITOR	0x00002000	/* Operate in monitor mode */
#define	IFM_IEEE80211_MBSS	0x00004000	/* Operate in MBSS mode */

/* operating mode for multi-mode devices */
#define	IFM_IEEE80211_11A	0x00010000	/* 5Ghz, OFDM mode */
#define	IFM_IEEE80211_11B	0x00020000	/* Direct Sequence mode */
#define	IFM_IEEE80211_11G	0x00030000	/* 2Ghz, CCK mode */
#define	IFM_IEEE80211_FH	0x00040000	/* 2Ghz, GFSK mode */
#define	IFM_IEEE80211_11NA	0x00050000	/* 5Ghz, HT mode */
#define	IFM_IEEE80211_11NG	0x00060000	/* 2Ghz, HT mode */
#define	IFM_IEEE80211_VHT5G	0x00070000	/* 5Ghz, VHT mode */
#define	IFM_IEEE80211_VHT2G	0x00080000	/* 2Ghz, VHT mode */

/*
 * ATM
 */
#define	IFM_ATM	0x000000a0
#define	IFM_ATM_UNKNOWN		3
#define	IFM_ATM_UTP_25		4
#define	IFM_ATM_TAXI_100	5
#define	IFM_ATM_TAXI_140	6
#define	IFM_ATM_MM_155		7
#define	IFM_ATM_SM_155		8
#define	IFM_ATM_UTP_155		9
#define	IFM_ATM_MM_622		10
#define	IFM_ATM_SM_622		11
#define	IFM_ATM_VIRTUAL		12
#define	IFM_ATM_SDH		0x00000100	/* SDH instead of SONET */
#define	IFM_ATM_NOSCRAMB	0x00000200	/* no scrambling */
#define	IFM_ATM_UNASSIGNED	0x00000400	/* unassigned cells */

/*
 * Shared media sub-types
 */
#define	IFM_AUTO	0		/* Autoselect best media */
#define	IFM_MANUAL	1		/* Jumper/dipswitch selects media */
#define	IFM_NONE	2		/* Deselect all media */

/*
 * Shared options
 */
#define	IFM_FDX		0x00100000	/* Force full duplex */
#define	IFM_HDX		0x00200000	/* Force half duplex */
#define	IFM_FLOW	0x00400000	/* enable hardware flow control */
#define	IFM_FLAG0	0x01000000	/* Driver defined flag */
#define	IFM_FLAG1	0x02000000	/* Driver defined flag */
#define	IFM_FLAG2	0x04000000	/* Driver defined flag */
#define	IFM_LOOP	0x08000000	/* Put hardware in loopback */

/*
 * Masks
 */
#define	IFM_NMASK	0x000000e0	/* Network type */
#define	IFM_TMASK	0x0000001f	/* Media sub-type */
#define	IFM_IMASK	0xf0000000	/* Instance */
#define	IFM_ISHIFT	28		/* Instance shift */
#define	IFM_OMASK	0x0000ff00	/* Type specific options */
#define	IFM_MMASK	0x00070000	/* Mode */
#define	IFM_MSHIFT	16		/* Mode shift */
#define	IFM_GMASK	0x0ff00000	/* Global options */

/* Ethernet flow control mask */
#define	IFM_ETH_FMASK	(IFM_FLOW | IFM_ETH_RXPAUSE | IFM_ETH_TXPAUSE)

/*
 * Status bits
 */
#define	IFM_AVALID	0x00000001	/* Active bit valid */
#define	IFM_ACTIVE	0x00000002	/* Interface attached to working net */

/* Mask of "status valid" bits, for ifconfig(8). */
#define	IFM_STATUS_VALID	IFM_AVALID

/* List of "status valid" bits, for ifconfig(8). */
#define	IFM_STATUS_VALID_LIST {						\
	IFM_AVALID,							\
	0								\
}

/*
 * Macros to extract various bits of information from the media word.
 */
#define	IFM_TYPE(x)		((x) & IFM_NMASK)
#define	IFM_SUBTYPE(x)	\
  (IFM_TYPE(x) == IFM_ETHER ? IFM_ETHER_SUBTYPE_GET(x) : ((x) & IFM_TMASK))
#define	IFM_TYPE_MATCH(x,y) \
  (IFM_TYPE(x) == IFM_TYPE(y) && IFM_SUBTYPE(x) == IFM_SUBTYPE(y))
#define	IFM_TYPE_OPTIONS(x)	((x) & IFM_OMASK)
#define	IFM_INST(x)		(((x) & IFM_IMASK) >> IFM_ISHIFT)
#define	IFM_OPTIONS(x)		((x) & (IFM_OMASK | IFM_GMASK))
#define	IFM_MODE(x)		((x) & IFM_MMASK)

#define	IFM_INST_MAX		IFM_INST(IFM_IMASK)

/*
 * Macro to create a media word.
 */
#define	IFM_MAKEWORD(type, subtype, options, instance)			\
	((type) | (subtype) | (options) | ((instance) << IFM_ISHIFT))
#define	IFM_MAKEMODE(mode) \
	(((mode) << IFM_MSHIFT) & IFM_MMASK)

/*
 * NetBSD extension not defined in the BSDI API.  This is used in various
 * places to get the canonical description for a given type/subtype.
 *
 * NOTE: all but the top-level type descriptions must contain NO whitespace!
 * Otherwise, parsing these in ifconfig(8) would be a nightmare.
 */
struct ifmedia_description {
	int	ifmt_word;		/* word value; may be masked */
	const char *ifmt_string;	/* description */
};

#define	IFM_TYPE_DESCRIPTIONS {						\
	{ IFM_ETHER,		"Ethernet" },				\
	{ IFM_IEEE80211,	"IEEE 802.11 Wireless Ethernet" },	\
	{ IFM_ATM,		"ATM" },				\
	{ 0, NULL },							\
}

#define	IFM_SUBTYPE_ETHERNET_DESCRIPTIONS {				\
	{ IFM_10_T,	"10baseT/UTP" },				\
	{ IFM_10_2,	"10base2/BNC" },				\
	{ IFM_10_5,	"10base5/AUI" },				\
	{ IFM_100_TX,	"100baseTX" },					\
	{ IFM_100_FX,	"100baseFX" },					\
	{ IFM_100_T4,	"100baseT4" },					\
	{ IFM_100_VG,	"100baseVG" },					\
	{ IFM_100_T2,	"100baseT2" },					\
	{ IFM_10_STP,	"10baseSTP" },					\
	{ IFM_10_FL,	"10baseFL" },					\
	{ IFM_1000_SX,	"1000baseSX" },					\
	{ IFM_1000_LX,	"1000baseLX" },					\
	{ IFM_1000_CX,	"1000baseCX" },					\
	{ IFM_1000_T,	"1000baseT" },					\
	{ IFM_HPNA_1,	"homePNA" },					\
	{ IFM_10G_LR,	"10Gbase-LR" },					\
	{ IFM_10G_SR,	"10Gbase-SR" },					\
	{ IFM_10G_CX4,	"10Gbase-CX4" },				\
	{ IFM_2500_SX,	"2500BaseSX" },					\
	{ IFM_10G_LRM,	"10Gbase-LRM" },				\
	{ IFM_10G_TWINAX,	"10Gbase-Twinax" },			\
	{ IFM_10G_TWINAX_LONG,	"10Gbase-Twinax-Long" },		\
	{ IFM_UNKNOWN,	"Unknown" },					\
	{ IFM_10G_T,	"10Gbase-T" },					\
	{ IFM_40G_CR4,	"40Gbase-CR4" },				\
	{ IFM_40G_SR4,	"40Gbase-SR4" },				\
	{ IFM_40G_LR4,	"40Gbase-LR4" },				\
	{ IFM_1000_KX,	"1000Base-KX" },				\
	{ IFM_OTHER,	"Other" },					\
	{ IFM_10G_KX4,	"10GBase-KX4" },				\
	{ IFM_10G_KR,	"10GBase-KR" },					\
	{ IFM_10G_CR1,	"10GBase-CR1" },				\
	{ IFM_20G_KR2,	"20GBase-KR2" },				\
	{ IFM_2500_KX,	"2500Base-KX" },				\
	{ IFM_2500_T,	"2500Base-T" },					\
	{ IFM_5000_T,	"5000Base-T" },					\
	{ IFM_50G_PCIE,	"PCIExpress-50G" },				\
	{ IFM_25G_PCIE,	"PCIExpress-25G" },				\
	{ IFM_1000_SGMII,	"1000Base-SGMII" },			\
	{ IFM_10G_SFI,	"10GBase-SFI" },				\
	{ IFM_40G_XLPPI,	"40GBase-XLPPI" },			\
	{ IFM_1000_CX_SGMII,	"1000Base-CX-SGMII" },			\
	{ IFM_40G_KR4,	"40GBase-KR4" },				\
	{ IFM_10G_ER,	"10GBase-ER" },					\
	{ IFM_100G_CR4,	"100GBase-CR4" },				\
	{ IFM_100G_SR4,	"100GBase-SR4" },				\
	{ IFM_100G_KR4,	"100GBase-KR4" },				\
	{ IFM_100G_LR4, "100GBase-LR4" },				\
	{ IFM_56G_R4,	"56GBase-R4" },					\
	{ IFM_100_T,	"100BaseT" },					\
	{ IFM_25G_CR,	"25GBase-CR" },					\
	{ IFM_25G_KR,	"25GBase-KR" },					\
	{ IFM_25G_SR,	"25GBase-SR" },					\
	{ IFM_50G_CR2,	"50GBase-CR2" },				\
	{ IFM_50G_KR2,	"50GBase-KR2" },				\
	{ IFM_25G_LR,	"25GBase-LR" },					\
	{ IFM_10G_AOC,	"10GBase-AOC" },				\
	{ IFM_25G_ACC,	"25GBase-ACC" },				\
	{ IFM_25G_AOC,	"25GBase-AOC" },				\
	{ IFM_100_SGMII,	"100M-SGMII" },				\
	{ IFM_2500_X,	"2500Base-X" },					\
	{ IFM_5000_KR,	"5000Base-KR" },				\
	{ IFM_25G_T,	"25GBase-T" },					\
	{ IFM_25G_CR_S,	"25GBase-CR-S" },				\
	{ IFM_25G_CR1,	"25GBase-CR1" },				\
	{ IFM_25G_KR_S,	"25GBase-KR-S" },				\
	{ IFM_5000_KR_S,	"5000Base-KR-S" },			\
	{ IFM_5000_KR1,	"5000Base-KR1" },				\
	{ IFM_25G_AUI,	"25G-AUI" },					\
	{ IFM_40G_XLAUI,	"40G-XLAUI" },				\
	{ IFM_40G_XLAUI_AC,	"40G-XLAUI-AC" },			\
	{ IFM_40G_ER4,	"40GBase-ER4" },				\
	{ IFM_50G_SR2,	"50GBase-SR2" },				\
	{ IFM_50G_LR2,	"50GBase-LR2" },				\
	{ IFM_50G_LAUI2_AC,	"50G-LAUI2-AC" },			\
	{ IFM_50G_LAUI2,	"50G-LAUI2" },				\
	{ IFM_50G_AUI2_AC,	"50G-AUI2-AC" },			\
	{ IFM_50G_AUI2,	"50G-AUI2" },					\
	{ IFM_50G_CP,	"50GBase-CP" },					\
	{ IFM_50G_SR,	"50GBase-SR" },					\
	{ IFM_50G_LR,	"50GBase-LR" },					\
	{ IFM_50G_FR,	"50GBase-FR" },					\
	{ IFM_50G_KR_PAM4,	"50GBase-KR-PAM4" },			\
	{ IFM_25G_KR1,	"25GBase-KR1" },				\
	{ IFM_50G_AUI1_AC,	"50G-AUI1-AC" },			\
	{ IFM_50G_AUI1,	"50G-AUI1" },					\
	{ IFM_100G_CAUI4_AC,	"100G-CAUI4-AC" },			\
	{ IFM_100G_CAUI4,	"100G-CAUI4" },				\
	{ IFM_100G_AUI4_AC,	"100G-AUI4-AC" },			\
	{ IFM_100G_AUI4,	"100G-AUI4" },				\
	{ IFM_100G_CR_PAM4,	"100GBase-CR-PAM4" },			\
	{ IFM_100G_KR_PAM4,	"100GBase-KR-PAM4" },			\
	{ IFM_100G_CP2,	"100GBase-CP2" },				\
	{ IFM_100G_SR2,	"100GBase-SR2" },				\
	{ IFM_100G_DR,	"100GBase-DR" },				\
	{ IFM_100G_KR2_PAM4,	"100GBase-KR2-PAM4" },			\
	{ IFM_100G_CAUI2_AC,	"100G-CAUI2-AC" },			\
	{ IFM_100G_CAUI2,	"100G-CAUI2" },				\
	{ IFM_100G_AUI2_AC,	"100G-AUI2-AC" },			\
	{ IFM_100G_AUI2,	"100G-AUI2" },				\
	{ IFM_200G_CR4_PAM4,	"200GBase-CR4-PAM4" },			\
	{ IFM_200G_SR4,	"200GBase-SR4" },				\
	{ IFM_200G_FR4,	"200GBase-FR4" },				\
	{ IFM_200G_LR4,	"200GBase-LR4" },				\
	{ IFM_200G_DR4,	"200GBase-DR4" },				\
	{ IFM_200G_KR4_PAM4,	"200GBase-KR4-PAM4" },			\
	{ IFM_200G_AUI4_AC,	"200G-AUI4-AC" },			\
	{ IFM_200G_AUI4,	"200G-AUI4" },				\
	{ IFM_200G_AUI8_AC,	"200G-AUI8-AC" },			\
	{ IFM_200G_AUI8,	"200G-AUI8" },				\
	{ IFM_400G_FR8,	"400GBase-FR8" },				\
	{ IFM_400G_LR8,	"400GBase-LR8" },				\
	{ IFM_400G_DR4,	"400GBase-DR4" },				\
	{ IFM_400G_AUI8_AC,	"400G-AUI8-AC" },			\
	{ IFM_400G_AUI8,	"400G-AUI8" },				\
	{ 0, NULL },							\
}

#define	IFM_SUBTYPE_ETHERNET_ALIASES {					\
	{ IFM_10_T,	"10baseT" },					\
	{ IFM_10_T,	"UTP" },					\
	{ IFM_10_T,	"10UTP" },					\
	{ IFM_10_2,	"BNC" },					\
	{ IFM_10_2,	"10BNC" },					\
	{ IFM_10_5,	"AUI" },					\
	{ IFM_10_5,	"10AUI" },					\
	{ IFM_100_TX,	"100TX" },					\
	{ IFM_100_T4,	"100T4" },					\
	{ IFM_100_VG,	"100VG" },					\
	{ IFM_100_T2,	"100T2" },					\
	{ IFM_10_STP,	"10STP" },					\
	{ IFM_10_FL,	"10FL" },					\
	{ IFM_1000_SX,	"1000SX" },					\
	{ IFM_1000_LX,	"1000LX" },					\
	{ IFM_1000_CX,	"1000CX" },					\
	{ IFM_1000_T,	"1000baseTX" },					\
	{ IFM_1000_T,	"1000TX" },					\
	{ IFM_1000_T,	"1000T" },					\
	{ IFM_2500_SX,	"2500SX" },					\
									\
	/*								\
	 * Shorthands for common media+option combinations as announced	\
	 * by miibus(4)							\
	 */								\
	{ IFM_10_T | IFM_FDX,			"10baseT-FDX" },	\
	{ IFM_10_T | IFM_FDX | IFM_FLOW,	"10baseT-FDX-flow" },	\
	{ IFM_100_TX | IFM_FDX,			"100baseTX-FDX" },	\
	{ IFM_100_TX | IFM_FDX | IFM_FLOW,	"100baseTX-FDX-flow" },	\
	{ IFM_1000_T | IFM_FDX,			"1000baseT-FDX" },	\
	{ IFM_1000_T | IFM_FDX | IFM_FLOW,	"1000baseT-FDX-flow" },	\
	{ IFM_1000_T | IFM_FDX | IFM_FLOW | IFM_ETH_MASTER,		\
	    "1000baseT-FDX-flow-master" },				\
	{ IFM_1000_T | IFM_FDX | IFM_ETH_MASTER,			\
	    "1000baseT-FDX-master" },					\
	{ IFM_1000_T | IFM_ETH_MASTER,		"1000baseT-master" },	\
									\
	{ 0, NULL },							\
}

#define	IFM_SUBTYPE_ETHERNET_OPTION_DESCRIPTIONS {			\
	{ IFM_ETH_MASTER,	"master" },				\
	{ IFM_ETH_RXPAUSE,	"rxpause" },				\
	{ IFM_ETH_TXPAUSE,	"txpause" },				\
	{ 0, NULL },							\
}

#define	IFM_SUBTYPE_IEEE80211_DESCRIPTIONS {				\
	{ IFM_IEEE80211_FH1, "FH/1Mbps" },				\
	{ IFM_IEEE80211_FH2, "FH/2Mbps" },				\
	{ IFM_IEEE80211_DS1, "DS/1Mbps" },				\
	{ IFM_IEEE80211_DS2, "DS/2Mbps" },				\
	{ IFM_IEEE80211_DS5, "DS/5.5Mbps" },				\
	{ IFM_IEEE80211_DS11, "DS/11Mbps" },				\
	{ IFM_IEEE80211_DS22, "DS/22Mbps" },				\
	{ IFM_IEEE80211_OFDM6, "OFDM/6Mbps" },				\
	{ IFM_IEEE80211_OFDM9, "OFDM/9Mbps" },				\
	{ IFM_IEEE80211_OFDM12, "OFDM/12Mbps" },			\
	{ IFM_IEEE80211_OFDM18, "OFDM/18Mbps" },			\
	{ IFM_IEEE80211_OFDM24, "OFDM/24Mbps" },			\
	{ IFM_IEEE80211_OFDM36, "OFDM/36Mbps" },			\
	{ IFM_IEEE80211_OFDM48, "OFDM/48Mbps" },			\
	{ IFM_IEEE80211_OFDM54, "OFDM/54Mbps" },			\
	{ IFM_IEEE80211_OFDM72, "OFDM/72Mbps" },			\
	{ IFM_IEEE80211_DS354k, "DS/354Kbps" },				\
	{ IFM_IEEE80211_DS512k, "DS/512Kbps" },				\
	{ IFM_IEEE80211_OFDM3, "OFDM/3Mbps" },				\
	{ IFM_IEEE80211_OFDM4, "OFDM/4.5Mbps" },			\
	{ IFM_IEEE80211_OFDM27, "OFDM/27Mbps" },			\
	{ IFM_IEEE80211_MCS, "MCS" },					\
	{ IFM_IEEE80211_VHT, "VHT" },					\
	{ 0, NULL },							\
}

#define	IFM_SUBTYPE_IEEE80211_ALIASES {					\
	{ IFM_IEEE80211_FH1, "FH1" },					\
	{ IFM_IEEE80211_FH2, "FH2" },					\
	{ IFM_IEEE80211_FH1, "FrequencyHopping/1Mbps" },		\
	{ IFM_IEEE80211_FH2, "FrequencyHopping/2Mbps" },		\
	{ IFM_IEEE80211_DS1, "DS1" },					\
	{ IFM_IEEE80211_DS2, "DS2" },					\
	{ IFM_IEEE80211_DS5, "DS5.5" },					\
	{ IFM_IEEE80211_DS11, "DS11" },					\
	{ IFM_IEEE80211_DS22, "DS22" },					\
	{ IFM_IEEE80211_DS1, "DirectSequence/1Mbps" },			\
	{ IFM_IEEE80211_DS2, "DirectSequence/2Mbps" },			\
	{ IFM_IEEE80211_DS5, "DirectSequence/5.5Mbps" },		\
	{ IFM_IEEE80211_DS11, "DirectSequence/11Mbps" },		\
	{ IFM_IEEE80211_DS22, "DirectSequence/22Mbps" },		\
	{ IFM_IEEE80211_OFDM6, "OFDM6" },				\
	{ IFM_IEEE80211_OFDM9, "OFDM9" },				\
	{ IFM_IEEE80211_OFDM12, "OFDM12" },				\
	{ IFM_IEEE80211_OFDM18, "OFDM18" },				\
	{ IFM_IEEE80211_OFDM24, "OFDM24" },				\
	{ IFM_IEEE80211_OFDM36, "OFDM36" },				\
	{ IFM_IEEE80211_OFDM48, "OFDM48" },				\
	{ IFM_IEEE80211_OFDM54, "OFDM54" },				\
	{ IFM_IEEE80211_OFDM72, "OFDM72" },				\
	{ IFM_IEEE80211_DS1, "CCK1" },					\
	{ IFM_IEEE80211_DS2, "CCK2" },					\
	{ IFM_IEEE80211_DS5, "CCK5.5" },				\
	{ IFM_IEEE80211_DS11, "CCK11" },				\
	{ IFM_IEEE80211_DS354k, "DS354K" },				\
	{ IFM_IEEE80211_DS354k, "DirectSequence/354Kbps" },		\
	{ IFM_IEEE80211_DS512k, "DS512K" },				\
	{ IFM_IEEE80211_DS512k, "DirectSequence/512Kbps" },		\
	{ IFM_IEEE80211_OFDM3, "OFDM3" },				\
	{ IFM_IEEE80211_OFDM4, "OFDM4.5" },				\
	{ IFM_IEEE80211_OFDM27, "OFDM27" },				\
	{ IFM_IEEE80211_MCS, "MCS" },					\
	{ IFM_IEEE80211_VHT, "VHT" },					\
	{ 0, NULL },							\
}

#define	IFM_SUBTYPE_IEEE80211_OPTION_DESCRIPTIONS {			\
	{ IFM_IEEE80211_ADHOC, "adhoc" },				\
	{ IFM_IEEE80211_HOSTAP, "hostap" },				\
	{ IFM_IEEE80211_IBSS, "ibss" },					\
	{ IFM_IEEE80211_WDS, "wds" },					\
	{ IFM_IEEE80211_TURBO, "turbo" },				\
	{ IFM_IEEE80211_MONITOR, "monitor" },				\
	{ IFM_IEEE80211_MBSS, "mesh" },					\
	{ 0, NULL },							\
}

#define	IFM_SUBTYPE_IEEE80211_MODE_DESCRIPTIONS {			\
	{ IFM_AUTO, "autoselect" },					\
	{ IFM_IEEE80211_11A, "11a" },					\
	{ IFM_IEEE80211_11B, "11b" },					\
	{ IFM_IEEE80211_11G, "11g" },					\
	{ IFM_IEEE80211_FH, "fh" },					\
	{ IFM_IEEE80211_11NA, "11na" },					\
	{ IFM_IEEE80211_11NG, "11ng" },					\
	{ IFM_IEEE80211_VHT5G, "11ac" },				\
	{ IFM_IEEE80211_VHT2G, "11ac2" },				\
	{ 0, NULL },							\
}

#define	IFM_SUBTYPE_IEEE80211_MODE_ALIASES {				\
	{ IFM_AUTO, "auto" },						\
	{ 0, NULL },							\
}

#define	IFM_SUBTYPE_ATM_DESCRIPTIONS {					\
	{ IFM_ATM_UNKNOWN,	"Unknown" },				\
	{ IFM_ATM_UTP_25,	"UTP/25.6MBit" },			\
	{ IFM_ATM_TAXI_100,	"Taxi/100MBit" },			\
	{ IFM_ATM_TAXI_140,	"Taxi/140MBit" },			\
	{ IFM_ATM_MM_155,	"Multi-mode/155MBit" },			\
	{ IFM_ATM_SM_155,	"Single-mode/155MBit" },		\
	{ IFM_ATM_UTP_155,	"UTP/155MBit" },			\
	{ IFM_ATM_MM_622,	"Multi-mode/622MBit" },			\
	{ IFM_ATM_SM_622,	"Single-mode/622MBit" },		\
	{ IFM_ATM_VIRTUAL,	"Virtual" },				\
	{ 0, NULL },							\
}

#define	IFM_SUBTYPE_ATM_ALIASES {					\
	{ IFM_ATM_UNKNOWN,	"UNKNOWN" },				\
	{ IFM_ATM_UTP_25,	"UTP-25" },				\
	{ IFM_ATM_TAXI_100,	"TAXI-100" },				\
	{ IFM_ATM_TAXI_140,	"TAXI-140" },				\
	{ IFM_ATM_MM_155,	"MM-155" },				\
	{ IFM_ATM_SM_155,	"SM-155" },				\
	{ IFM_ATM_UTP_155,	"UTP-155" },				\
	{ IFM_ATM_MM_622,	"MM-622" },				\
	{ IFM_ATM_SM_622,	"SM-622" },				\
	{ IFM_ATM_VIRTUAL,	"VIRTUAL" },				\
	{ 0, NULL },							\
}

#define	IFM_SUBTYPE_ATM_OPTION_DESCRIPTIONS {				\
	{ IFM_ATM_SDH, "SDH" },						\
	{ IFM_ATM_NOSCRAMB, "Noscramb" },				\
	{ IFM_ATM_UNASSIGNED, "Unassigned" },				\
	{ 0, NULL },							\
}

#define	IFM_SUBTYPE_SHARED_DESCRIPTIONS {				\
	{ IFM_AUTO,	"autoselect" },					\
	{ IFM_MANUAL,	"manual" },					\
	{ IFM_NONE,	"none" },					\
	{ 0, NULL },							\
}

#define	IFM_SUBTYPE_SHARED_ALIASES {					\
	{ IFM_AUTO,	"auto" },					\
									\
	/*								\
	 * Shorthands for common media+option combinations as announced	\
	 * by miibus(4)							\
	 */								\
	{ IFM_AUTO | IFM_FLOW,	"auto-flow" },				\
									\
	{ 0, NULL },							\
}

#define	IFM_SHARED_OPTION_DESCRIPTIONS {				\
	{ IFM_FDX,	"full-duplex" },				\
	{ IFM_HDX,	"half-duplex" },				\
	{ IFM_FLOW,	"flowcontrol" },				\
	{ IFM_FLAG0,	"flag0" },					\
	{ IFM_FLAG1,	"flag1" },					\
	{ IFM_FLAG2,	"flag2" },					\
	{ IFM_LOOP,	"hw-loopback" },				\
	{ 0, NULL },							\
}

#define	IFM_SHARED_OPTION_ALIASES {					\
	{ IFM_FDX,	"fdx" },					\
	{ IFM_HDX,	"hdx" },					\
	{ IFM_FLOW,	"flow" },					\
	{ IFM_LOOP,	"loop" },					\
	{ IFM_LOOP,	"loopback" },					\
	{ 0, NULL },							\
}

/*
 * Baudrate descriptions for the various media types.
 */
struct ifmedia_baudrate {
	int		ifmb_word;		/* media word */
	uint64_t	ifmb_baudrate;		/* corresponding baudrate */
};

#define	IFM_BAUDRATE_DESCRIPTIONS {					\
	{ IFM_ETHER | IFM_10_T,		IF_Mbps(10) },			\
	{ IFM_ETHER | IFM_10_2,		IF_Mbps(10) },			\
	{ IFM_ETHER | IFM_10_5,		IF_Mbps(10) },			\
	{ IFM_ETHER | IFM_100_TX,	IF_Mbps(100) },			\
	{ IFM_ETHER | IFM_100_FX,	IF_Mbps(100) },			\
	{ IFM_ETHER | IFM_100_T4,	IF_Mbps(100) },			\
	{ IFM_ETHER | IFM_100_VG,	IF_Mbps(100) },			\
	{ IFM_ETHER | IFM_100_T2,	IF_Mbps(100) },			\
	{ IFM_ETHER | IFM_1000_SX,	IF_Mbps(1000) },		\
	{ IFM_ETHER | IFM_10_STP,	IF_Mbps(10) },			\
	{ IFM_ETHER | IFM_10_FL,	IF_Mbps(10) },			\
	{ IFM_ETHER | IFM_1000_LX,	IF_Mbps(1000) },		\
	{ IFM_ETHER | IFM_1000_CX,	IF_Mbps(1000) },		\
	{ IFM_ETHER | IFM_1000_T,	IF_Mbps(1000) },		\
	{ IFM_ETHER | IFM_HPNA_1,	IF_Mbps(1) },			\
	{ IFM_ETHER | IFM_10G_LR,	IF_Gbps(10ULL) },		\
	{ IFM_ETHER | IFM_10G_SR,	IF_Gbps(10ULL) },		\
	{ IFM_ETHER | IFM_10G_CX4,	IF_Gbps(10ULL) },		\
	{ IFM_ETHER | IFM_2500_SX,	IF_Mbps(2500ULL) },		\
	{ IFM_ETHER | IFM_10G_TWINAX,	IF_Gbps(10ULL) },		\
	{ IFM_ETHER | IFM_10G_TWINAX_LONG,	IF_Gbps(10ULL) },	\
	{ IFM_ETHER | IFM_10G_LRM,	IF_Gbps(10ULL) },		\
	{ IFM_ETHER | IFM_10G_T,	IF_Gbps(10ULL) },		\
	{ IFM_ETHER | IFM_40G_CR4,	IF_Gbps(40ULL) },		\
	{ IFM_ETHER | IFM_40G_SR4,	IF_Gbps(40ULL) },		\
	{ IFM_ETHER | IFM_40G_LR4,	IF_Gbps(40ULL) },		\
	{ IFM_ETHER | IFM_1000_KX,	IF_Mbps(1000) },		\
	{ IFM_ETHER | IFM_10G_KX4,	IF_Gbps(10ULL) },		\
	{ IFM_ETHER | IFM_10G_KR,	IF_Gbps(10ULL) },		\
	{ IFM_ETHER | IFM_10G_CR1,	IF_Gbps(10ULL) },		\
	{ IFM_ETHER | IFM_20G_KR2,	IF_Gbps(20ULL) },		\
	{ IFM_ETHER | IFM_2500_KX,	IF_Mbps(2500) },		\
	{ IFM_ETHER | IFM_2500_T,	IF_Mbps(2500) },		\
	{ IFM_ETHER | IFM_5000_T,	IF_Mbps(5000) },		\
	{ IFM_ETHER | IFM_50G_PCIE,	IF_Gbps(50ULL) },		\
	{ IFM_ETHER | IFM_25G_PCIE,	IF_Gbps(25ULL) },		\
	{ IFM_ETHER | IFM_1000_SGMII,	IF_Mbps(1000) },		\
	{ IFM_ETHER | IFM_10G_SFI,	IF_Gbps(10ULL) },		\
	{ IFM_ETHER | IFM_40G_XLPPI,	IF_Gbps(40ULL) },		\
	{ IFM_ETHER | IFM_1000_CX_SGMII, IF_Mbps(1000) },		\
	{ IFM_ETHER | IFM_40G_KR4,	IF_Gbps(40ULL) },		\
	{ IFM_ETHER | IFM_10G_ER,	IF_Gbps(10ULL) },		\
	{ IFM_ETHER | IFM_100G_CR4,	IF_Gbps(100ULL) },		\
	{ IFM_ETHER | IFM_100G_SR4,	IF_Gbps(100ULL) },		\
	{ IFM_ETHER | IFM_100G_KR4,	IF_Gbps(100ULL) },		\
	{ IFM_ETHER | IFM_100G_LR4,	IF_Gbps(100ULL) },		\
	{ IFM_ETHER | IFM_56G_R4,	IF_Gbps(56ULL) },		\
	{ IFM_ETHER | IFM_100_T,	IF_Mbps(100ULL) },		\
	{ IFM_ETHER | IFM_25G_CR,	IF_Gbps(25ULL) },		\
	{ IFM_ETHER | IFM_25G_KR,	IF_Gbps(25ULL) },		\
	{ IFM_ETHER | IFM_25G_SR,	IF_Gbps(25ULL) },		\
	{ IFM_ETHER | IFM_50G_CR2,	IF_Gbps(50ULL) },		\
	{ IFM_ETHER | IFM_50G_KR2,	IF_Gbps(50ULL) },		\
	{ IFM_ETHER | IFM_25G_LR,	IF_Gbps(25ULL) },		\
	{ IFM_ETHER | IFM_10G_AOC,	IF_Gbps(10ULL) },		\
	{ IFM_ETHER | IFM_25G_ACC,	IF_Gbps(25ULL) },		\
	{ IFM_ETHER | IFM_25G_AOC,	IF_Gbps(25ULL) },		\
	{ IFM_ETHER | IFM_100_SGMII,	IF_Mbps(100) },			\
	{ IFM_ETHER | IFM_2500_X,	IF_Mbps(2500ULL) },		\
	{ IFM_ETHER | IFM_5000_KR,	IF_Mbps(5000ULL) },		\
	{ IFM_ETHER | IFM_25G_T,	IF_Gbps(25ULL) },		\
	{ IFM_ETHER | IFM_25G_CR_S,	IF_Gbps(25ULL) },		\
	{ IFM_ETHER | IFM_25G_CR1,	IF_Gbps(25ULL) },		\
	{ IFM_ETHER | IFM_25G_KR_S,	IF_Gbps(25ULL) },		\
	{ IFM_ETHER | IFM_5000_KR_S,	IF_Mbps(5000ULL) },		\
	{ IFM_ETHER | IFM_5000_KR1,	IF_Mbps(5000ULL) },		\
	{ IFM_ETHER | IFM_25G_AUI,	IF_Gbps(25ULL) },		\
	{ IFM_ETHER | IFM_40G_XLAUI,	IF_Gbps(40ULL) },		\
	{ IFM_ETHER | IFM_40G_XLAUI_AC,	IF_Gbps(40ULL) },		\
	{ IFM_ETHER | IFM_40G_ER4,	IF_Gbps(40ULL) },		\
	{ IFM_ETHER | IFM_50G_SR2,	IF_Gbps(50ULL) },		\
	{ IFM_ETHER | IFM_50G_LR2,	IF_Gbps(50ULL) },		\
	{ IFM_ETHER | IFM_50G_LAUI2_AC,	IF_Gbps(50ULL) },		\
	{ IFM_ETHER | IFM_50G_LAUI2,	IF_Gbps(50ULL) },		\
	{ IFM_ETHER | IFM_50G_AUI2_AC,	IF_Gbps(50ULL) },		\
	{ IFM_ETHER | IFM_50G_AUI2,	IF_Gbps(50ULL) },		\
	{ IFM_ETHER | IFM_50G_CP,	IF_Gbps(50ULL) },		\
	{ IFM_ETHER | IFM_50G_SR,	IF_Gbps(50ULL) },		\
	{ IFM_ETHER | IFM_50G_LR,	IF_Gbps(50ULL) },		\
	{ IFM_ETHER | IFM_50G_FR,	IF_Gbps(50ULL) },		\
	{ IFM_ETHER | IFM_50G_KR_PAM4,	IF_Gbps(50ULL) },		\
	{ IFM_ETHER | IFM_25G_KR1,	IF_Gbps(25ULL) },		\
	{ IFM_ETHER | IFM_50G_AUI1_AC,	IF_Gbps(50ULL) },		\
	{ IFM_ETHER | IFM_50G_AUI1,	IF_Gbps(50ULL) },		\
	{ IFM_ETHER | IFM_100G_CAUI4_AC, IF_Gbps(100ULL) },		\
	{ IFM_ETHER | IFM_100G_CAUI4,	IF_Gbps(100ULL) },		\
	{ IFM_ETHER | IFM_100G_AUI4_AC,	IF_Gbps(100ULL) },		\
	{ IFM_ETHER | IFM_100G_AUI4,	IF_Gbps(100ULL) },		\
	{ IFM_ETHER | IFM_100G_CR_PAM4,	IF_Gbps(100ULL) },		\
	{ IFM_ETHER | IFM_100G_KR_PAM4,	IF_Gbps(100ULL) },		\
	{ IFM_ETHER | IFM_100G_CP2,	IF_Gbps(100ULL) },		\
	{ IFM_ETHER | IFM_100G_SR2,	IF_Gbps(100ULL) },		\
	{ IFM_ETHER | IFM_100G_DR,	IF_Gbps(100ULL) },		\
	{ IFM_ETHER | IFM_100G_KR2_PAM4, IF_Gbps(100ULL) },		\
	{ IFM_ETHER | IFM_100G_CAUI2_AC, IF_Gbps(100ULL) },		\
	{ IFM_ETHER | IFM_100G_CAUI2,	IF_Gbps(100ULL) },		\
	{ IFM_ETHER | IFM_100G_AUI2_AC,	IF_Gbps(100ULL) },		\
	{ IFM_ETHER | IFM_100G_AUI2,	IF_Gbps(100ULL) },		\
	{ IFM_ETHER | IFM_200G_CR4_PAM4, IF_Gbps(200ULL) },		\
	{ IFM_ETHER | IFM_200G_SR4,	IF_Gbps(200ULL) },		\
	{ IFM_ETHER | IFM_200G_FR4,	IF_Gbps(200ULL) },		\
	{ IFM_ETHER | IFM_200G_LR4,	IF_Gbps(200ULL) },		\
	{ IFM_ETHER | IFM_200G_DR4,	IF_Gbps(200ULL) },		\
	{ IFM_ETHER | IFM_200G_KR4_PAM4, IF_Gbps(200ULL) },		\
	{ IFM_ETHER | IFM_200G_AUI4_AC,	IF_Gbps(200ULL) },		\
	{ IFM_ETHER | IFM_200G_AUI4,	IF_Gbps(200ULL) },		\
	{ IFM_ETHER | IFM_200G_AUI8_AC,	IF_Gbps(200ULL) },		\
	{ IFM_ETHER | IFM_200G_AUI8,	IF_Gbps(200ULL) },		\
	{ IFM_ETHER | IFM_400G_FR8,	IF_Gbps(400ULL) },		\
	{ IFM_ETHER | IFM_400G_LR8,	IF_Gbps(400ULL) },		\
	{ IFM_ETHER | IFM_400G_DR4,	IF_Gbps(400ULL) },		\
	{ IFM_ETHER | IFM_400G_AUI8_AC,	IF_Gbps(400ULL) },		\
	{ IFM_ETHER | IFM_400G_AUI8,	IF_Gbps(400ULL) },		\
									\
	{ IFM_IEEE80211 | IFM_IEEE80211_FH1,	IF_Mbps(1) },		\
	{ IFM_IEEE80211 | IFM_IEEE80211_FH2,	IF_Mbps(2) },		\
	{ IFM_IEEE80211 | IFM_IEEE80211_DS2,	IF_Mbps(2) },		\
	{ IFM_IEEE80211 | IFM_IEEE80211_DS5,	IF_Kbps(5500) },	\
	{ IFM_IEEE80211 | IFM_IEEE80211_DS11,	IF_Mbps(11) },		\
	{ IFM_IEEE80211 | IFM_IEEE80211_DS1,	IF_Mbps(1) },		\
	{ IFM_IEEE80211 | IFM_IEEE80211_DS22,	IF_Mbps(22) },		\
	{ IFM_IEEE80211 | IFM_IEEE80211_OFDM6,	IF_Mbps(6) },		\
	{ IFM_IEEE80211 | IFM_IEEE80211_OFDM9,	IF_Mbps(9) },		\
	{ IFM_IEEE80211 | IFM_IEEE80211_OFDM12,	IF_Mbps(12) },		\
	{ IFM_IEEE80211 | IFM_IEEE80211_OFDM18,	IF_Mbps(18) },		\
	{ IFM_IEEE80211 | IFM_IEEE80211_OFDM24,	IF_Mbps(24) },		\
	{ IFM_IEEE80211 | IFM_IEEE80211_OFDM36,	IF_Mbps(36) },		\
	{ IFM_IEEE80211 | IFM_IEEE80211_OFDM48,	IF_Mbps(48) },		\
	{ IFM_IEEE80211 | IFM_IEEE80211_OFDM54,	IF_Mbps(54) },		\
	{ IFM_IEEE80211 | IFM_IEEE80211_OFDM72,	IF_Mbps(72) },		\
									\
	{ 0, 0 },							\
}

/*
 * Status descriptions for the various media types.
 */
struct ifmedia_status_description {
	int	   ifms_type;
	int	   ifms_valid;
	int	   ifms_bit;
	const char *ifms_string[2];
};

#define	IFM_STATUS_DESC(ifms, bit)					\
	(ifms)->ifms_string[((ifms)->ifms_bit & (bit)) ? 1 : 0]

#define	IFM_STATUS_DESCRIPTIONS {					\
	{ IFM_ETHER,		IFM_AVALID,	IFM_ACTIVE,		\
	    { "no carrier", "active" } },				\
	{ IFM_IEEE80211,	IFM_AVALID,	IFM_ACTIVE,		\
	    { "no network", "active" } },				\
	{ IFM_ATM,		IFM_AVALID,	IFM_ACTIVE,		\
	    { "no network", "active" } },				\
	{ 0,			0,		0,			\
	    { NULL, NULL } }						\
}
#endif	/* _NET_IF_MEDIA_H_ */
