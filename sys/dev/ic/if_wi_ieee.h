/*	$OpenBSD: if_wi_ieee.h,v 1.31 2022/01/09 05:42:38 jsg Exp $	*/

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
 *	From: if_wavelan_ieee.h,v 1.5.2.1 2001/07/04 00:12:34 brooks Exp $
 */

#ifndef _IF_WI_IEEE_H
#define _IF_WI_IEEE_H

/*
 * This header defines a simple command interface to the FreeBSD
 * WaveLAN/IEEE driver (wi) driver, which is used to set certain
 * device-specific parameters which can't be easily managed through
 * ifconfig(8). No, sysctl(2) is not the answer. I said a _simple_
 * interface, didn't I.
 */

#define SIOCSWAVELAN	_IOW('i', 206, struct ifreq)	/* wavelan set op */
#define SIOCGWAVELAN	_IOWR('i', 207, struct ifreq)	/* wavelan get op */

/*
 * Technically I don't think there's a limit to a record
 * length. The largest record is the one that contains the CIS
 * data, which is 240 words long, so 256 should be a safe
 * value.
 */
#define WI_MAX_DATALEN	512

struct wi_req {
	u_int16_t	wi_len;
	u_int16_t	wi_type;
	u_int16_t	wi_val[WI_MAX_DATALEN];
};

/*
 * Private LTV records (interpreted only by the driver). This is
 * a minor kludge to allow reading the interface statistics from
 * the driver.
 */
#define WI_RID_IFACE_STATS	0x0100
#define WI_RID_MGMT_XMIT	0x0200
#define	WI_RID_MONITOR_MODE	0x0500
#define WI_RID_SCAN_APS		0x0600
#define WI_RID_READ_APS		0x0700

struct wi_80211_hdr {
	u_int16_t		frame_ctl;
	u_int16_t		dur_id;
	u_int8_t		addr1[6];
	u_int8_t		addr2[6];
	u_int8_t		addr3[6];
	u_int16_t		seq_ctl;
	u_int8_t		addr4[6];
};

#define WI_FCTL_VERS		0x0002
#define WI_FCTL_FTYPE		0x000C
#define WI_FCTL_STYPE		0x00F0
#define WI_FCTL_TODS		0x0100
#define WI_FCTL_FROMDS		0x0200
#define WI_FCTL_MOREFRAGS	0x0400
#define WI_FCTL_RETRY		0x0800
#define WI_FCTL_PM		0x1000
#define WI_FCTL_MOREDATA	0x2000
#define WI_FCTL_WEP		0x4000
#define WI_FCTL_ORDER		0x8000

#define WI_FTYPE_MGMT		0x0000
#define WI_FTYPE_CTL		0x0004
#define WI_FTYPE_DATA		0x0008

#define WI_STYPE_MGMT_ASREQ	0x0000	/* association request */
#define WI_STYPE_MGMT_ASRESP	0x0010	/* association response */
#define WI_STYPE_MGMT_REASREQ	0x0020	/* reassociation request */
#define WI_STYPE_MGMT_REASRESP	0x0030	/* reassociation response */
#define WI_STYPE_MGMT_PROBEREQ	0x0040	/* probe request */
#define WI_STYPE_MGMT_PROBERESP	0x0050	/* probe response */
#define WI_STYPE_MGMT_BEACON	0x0080	/* beacon */
#define WI_STYPE_MGMT_ATIM	0x0090	/* announcement traffic ind msg */
#define WI_STYPE_MGMT_DISAS	0x00A0	/* disassociation */
#define WI_STYPE_MGMT_AUTH	0x00B0	/* authentication */
#define WI_STYPE_MGMT_DEAUTH	0x00C0	/* deauthentication */

#define WI_STYPE_CTL_PSPOLL	0x00A0
#define WI_STYPE_CTL_RTS	0x00B0
#define WI_STYPE_CTL_CTS	0x00C0
#define WI_STYPE_CTL_ACK	0x00D0
#define WI_STYPE_CTL_CFEND	0x00E0
#define WI_STYPE_CTL_CFENDACK	0x00F0
#define WI_STYPE_CTL_CFENDCFACK	WI_STYPE_CTL_CFENDACK

#define WI_STYPE_DATA		0x0000
#define WI_STYPE_DATA_CFACK	0x0010
#define WI_STYPE_DATA_CFPOLL	0x0020
#define WI_STYPE_DATA_CFACKPOLL	0x0030
#define WI_STYPE_NULLFUNC	0x0040
#define WI_STYPE_CFACK		0x0050
#define WI_STYPE_CFPOLL		0x0060
#define WI_STYPE_CFACKPOLL	0x0070

struct wi_mgmt_hdr {
	u_int16_t		frame_ctl;
	u_int16_t		duration;
	u_int8_t		dst_addr[6];
	u_int8_t		src_addr[6];
	u_int8_t		bssid[6];
	u_int16_t		seq_ctl;
};

struct wi_counters {
	u_int32_t		wi_tx_unicast_frames;
	u_int32_t		wi_tx_multicast_frames;
	u_int32_t		wi_tx_fragments;
	u_int32_t		wi_tx_unicast_octets;
	u_int32_t		wi_tx_multicast_octets;
	u_int32_t		wi_tx_deferred_xmits;
	u_int32_t		wi_tx_single_retries;
	u_int32_t		wi_tx_multi_retries;
	u_int32_t		wi_tx_retry_limit;
	u_int32_t		wi_tx_discards;
	u_int32_t		wi_rx_unicast_frames;
	u_int32_t		wi_rx_multicast_frames;
	u_int32_t		wi_rx_fragments;
	u_int32_t		wi_rx_unicast_octets;
	u_int32_t		wi_rx_multicast_octets;
	u_int32_t		wi_rx_fcs_errors;
	u_int32_t		wi_rx_discards_nobuf;
	u_int32_t		wi_tx_discards_wrong_sa;
	u_int32_t		wi_rx_WEP_cant_decrypt;
	u_int32_t		wi_rx_msg_in_msg_frags;
	u_int32_t		wi_rx_msg_in_bad_msg_frags;
};

/*
 * These are all the LTV record types that we can read or write
 * from the WaveLAN. Not all of them are tremendously useful, but I
 * list as many as I know about here for completeness.
 */

#define WI_RID_DNLD_BUF		0xFD01
#define WI_RID_MEMSZ		0xFD02 /* memory size info (Lucent) */
#define WI_RID_PRI_IDENTITY	0xFD02 /* primary firmware ident (PRISM2) */
#define WI_RID_DOMAINS		0xFD11 /* List of intended regulatory domains */
#define WI_RID_CIS		0xFD13 /* CIS info */
#define WI_RID_COMMQUAL		0xFD43 /* Communications quality */
#define WI_RID_SCALETHRESH	0xFD46 /* Actual system scale thresholds */
#define WI_RID_PCF		0xFD87 /* PCF info */

/*
 * Network parameters, static configuration entities.
 */
#define	WI_RID_PORTTYPE		0xFC00 /* Connection control characteristics */
#define	WI_RID_MAC_NODE		0xFC01 /* MAC address of this station */
#define	WI_RID_DESIRED_SSID	0xFC02 /* Service Set ID for connection */
#define	WI_RID_OWN_CHNL		0xFC03 /* Comm channel for BSS creation */
#define	WI_RID_OWN_SSID		0xFC04 /* IBSS creation ID */
#define	WI_RID_OWN_ATIM_WIN	0xFC05 /* ATIM window time for IBSS creation */
#define	WI_RID_SYSTEM_SCALE	0xFC06 /* scale that specifies AP density */
#define	WI_RID_MAX_DATALEN	0xFC07 /* Max len of MAC frame body data */
#define	WI_RID_MAC_WDS		0xFC08 /* MAC addr of corresponding WDS node */
#define	WI_RID_PM_ENABLED	0xFC09 /* ESS power management enable */
#define	WI_RID_PM_EPS		0xFC0A /* PM EPS/PS mode */
#define	WI_RID_MCAST_RX		0xFC0B /* ESS PM mcast reception */
#define	WI_RID_MAX_SLEEP	0xFC0C /* max sleep time for ESS PM */
#define	WI_RID_HOLDOVER		0xFC0D /* holdover time for ESS PM */
#define	WI_RID_NODENAME		0xFC0E /* ID name of this node for diag */
#define	WI_RID_DTIM_PERIOD	0xFC10 /* beacon interval between DTIMs */
#define	WI_RID_WDS_ADDR1	0xFC11 /* port 1 MAC of WDS link node */
#define	WI_RID_WDS_ADDR2	0xFC12 /* port 1 MAC of WDS link node */
#define	WI_RID_WDS_ADDR3	0xFC13 /* port 1 MAC of WDS link node */
#define	WI_RID_WDS_ADDR4	0xFC14 /* port 1 MAC of WDS link node */
#define	WI_RID_WDS_ADDR5	0xFC15 /* port 1 MAC of WDS link node */
#define	WI_RID_WDS_ADDR6	0xFC16 /* port 1 MAC of WDS link node */
#define	WI_RID_MCAST_PM_BUF	0xFC17 /* PM buffering of mcast */
#define	WI_RID_ENCRYPTION	0xFC20 /* enable/disable WEP */
#define	WI_RID_AUTHTYPE		0xFC21 /* specify authentication type */
#define	WI_RID_SYMBOL_MANDATORYBSSID 0xFC21
#define	WI_RID_P2_TX_CRYPT_KEY	0xFC23
#define	WI_RID_P2_CRYPT_KEY0	0xFC24
#define	WI_RID_P2_CRYPT_KEY1	0xFC25
#define	WI_RID_MICROWAVE_OVEN	0xFC25 /* Microwave oven robustness */
#define	WI_RID_P2_CRYPT_KEY2	0xFC26
#define	WI_RID_P2_CRYPT_KEY3	0xFC27
#define	WI_RID_P2_ENCRYPTION	0xFC28
#define PRIVACY_INVOKED		0x01
#define EXCLUDE_UNENCRYPTED	0x02
#define HOST_ENCRYPT		0x10
#define IV_EVERY_FRAME		0x00
#define IV_EVERY10_FRAME	0x20
#define IV_EVERY50_FRAME	0x40
#define IV_EVERY100_FRAME	0x60
#define HOST_DECRYPT		0x80
#define	WI_RID_WEP_MAPTABLE	0xFC29
#define	WI_RID_CNFAUTHMODE	0xFC2A
#define	WI_RID_SYMBOL_KEYLENGTH	0xFC2B
#define	WI_RID_ROAMING_MODE	0xFC2D /* Roaming mode (1:firm,3:disable) */
#define WI_RID_CUR_BEACON_INT	0xFC33 /* beacon xmit time for BSS creation */
#define	WI_RID_ENH_SECURITY	0xFC43 /* hide SSID name (prism fw >= 1.6.3) */
#define WI_HIDESSID		0x01
#define WI_IGNPROBES		0x02
#define WI_HIDESSID_IGNPROBES	0x03
#define	WI_RID_DBM_ADJUST	0xFC46 /* Get DBM adjustment factor */
#define	WI_RID_SYMBOL_PREAMBLE	0xFC8C /* Enable/disable short preamble */
#define	WI_RID_P2_SHORT_PREAMBLE	0xFCB0 /* Short preamble support */
#define	WI_RID_P2_EXCLUDE_LONG_PREAMBLE	0xFCB1 /* Don't send long preamble */
#define	WI_RID_BASIC_RATE	0xFCB3
#define	WI_RID_SUPPORT_RATE	0xFCB4
#define WI_RID_SYMBOL_DIVERSITY	0xFC87 /* Symbol antenna diversity */
#define WI_RID_SYMBOL_BASIC_RATE 0xFC90

/*
 * Network parameters, dynamic configuration entities
 */
#define WI_RID_MCAST_LIST	0xFC80 /* list of multicast addrs (up to 16) */
#define WI_RID_CREATE_IBSS	0xFC81 /* create IBSS */
#define WI_RID_FRAG_THRESH	0xFC82 /* frag len, unicast msg xmit */
#define WI_RID_RTS_THRESH	0xFC83 /* frame len for RTS/CTS handshake */
#define WI_RID_TX_RATE		0xFC84 /* data rate for message xmit */
#define WI_RID_PROMISC		0xFC85 /* enable promisc mode */
#define WI_RID_FRAG_THRESH0	0xFC90
#define WI_RID_FRAG_THRESH1	0xFC91
#define WI_RID_FRAG_THRESH2	0xFC92
#define WI_RID_FRAG_THRESH3	0xFC93
#define WI_RID_FRAG_THRESH4	0xFC94
#define WI_RID_FRAG_THRESH5	0xFC95
#define WI_RID_FRAG_THRESH6	0xFC96
#define WI_RID_RTS_THRESH0	0xFC97
#define WI_RID_RTS_THRESH1	0xFC98
#define WI_RID_RTS_THRESH2	0xFC99
#define WI_RID_RTS_THRESH3	0xFC9A
#define WI_RID_RTS_THRESH4	0xFC9B
#define WI_RID_RTS_THRESH5	0xFC9C
#define WI_RID_RTS_THRESH6	0xFC9D
#define WI_RID_TX_RATE0		0xFC9E
#define WI_RID_TX_RATE1		0xFC9F
#define WI_RID_TX_RATE2		0xFCA0
#define WI_RID_TX_RATE3		0xFCA1
#define WI_RID_TX_RATE4		0xFCA2
#define WI_RID_TX_RATE5		0xFCA3
#define WI_RID_TX_RATE6		0xFCA4
#define WI_RID_DEFLT_CRYPT_KEYS	0xFCB0
#define WI_RID_TX_CRYPT_KEY	0xFCB1
#define WI_RID_TICK_TIME	0xFCE0 /* Auxiliary Timer tick interval */

struct wi_key {
	u_int16_t		wi_keylen;
	u_int8_t		wi_keydat[13];
};

#define	WI_NLTV_KEYS	4
struct wi_ltv_keys {
	u_int16_t		wi_len;
	u_int16_t		wi_type;
	struct wi_key		wi_keys[WI_NLTV_KEYS];
};

/*
 * NIC information
 */
#define WI_RID_FIRM_ID		0xFD02 /* Primary func firmware ID. */
#define WI_RID_PRI_SUP_RANGE	0xFD03 /* primary supplier compatibility */
#define WI_RID_CIF_ACT_RANGE	0xFD04 /* controller sup. compatibility */
#define WI_RID_SERIALNO		0xFD0A /* card serial number */
#define WI_RID_CARD_ID		0xFD0B /* card identification */
#define WI_RID_MFI_SUP_RANGE	0xFD0C /* modem supplier compatibility */
#define WI_RID_CFI_SUP_RANGE	0xFD0D /* controller sup. compatibility */
#define WI_RID_CHANNEL_LIST	0xFD10 /* allowed comm. frequencies. */
#define WI_RID_REG_DOMAINS	0xFD11 /* list of intended regulatory doms */
#define WI_RID_TEMP_TYPE	0xFD12 /* hw temp range code */
#define WI_RID_CIS		0xFD13 /* PC card info struct */
#define WI_RID_STA_IDENTITY	0xFD20 /* station funcs firmware ident */
#define WI_RID_STA_SUP_RANGE	0xFD21 /* station supplier compat */
#define WI_RID_MFI_ACT_RANGE	0xFD22
#define WI_RID_SYMBOL_IDENTITY	0xFD24 /* Symbol station firmware ident */
#define WI_RID_CFI_ACT_RANGE	0xFD33

/*
 * MAC information
 */
#define WI_RID_PORT_STAT	0xFD40 /* actual MAC port con control stat */
#define WI_RID_CURRENT_SSID	0xFD41 /* ID of actually connected SS */
#define WI_RID_CURRENT_BSSID	0xFD42 /* ID of actually connected BSS */
#define WI_RID_COMMS_QUALITY	0xFD43 /* quality of BSS connection */
#define WI_RID_CUR_TX_RATE	0xFD44 /* current TX rate */
#define WI_RID_OWN_BEACON_INT	0xFD45 /* beacon xmit time for BSS creation */
#define WI_RID_CUR_SCALE_THRESH	0xFD46 /* actual system scale thresh setting */
#define WI_RID_PROT_RESP_TIME	0xFD47 /* time to wait for resp to req msg */
#define WI_RID_SHORT_RTR_LIM	0xFD48 /* max tx attempts for short frames */
#define WI_RID_LONG_RTS_LIM	0xFD49 /* max tx attempts for long frames */
#define WI_RID_MAX_TX_LIFE	0xFD4A /* max tx frame handling duration */
#define WI_RID_MAX_RX_LIFE	0xFD4B /* max rx frame handling duration */
#define WI_RID_CF_POLL		0xFD4C /* contention free pollable ind */
#define WI_RID_AUTH_ALGS	0xFD4D /* auth algorithms available */
#define WI_RID_AUTH_TYPE	0xFD4E /* available auth types */
#define WI_RID_WEP_AVAIL	0xFD4F /* WEP privacy option available */
#define WI_RID_CUR_TX_RATE1	0xFD80
#define WI_RID_CUR_TX_RATE2	0xFD81
#define WI_RID_CUR_TX_RATE3	0xFD82
#define WI_RID_CUR_TX_RATE4	0xFD83
#define WI_RID_CUR_TX_RATE5	0xFD84
#define WI_RID_CUR_TX_RATE6	0xFD85
#define WI_RID_OWN_MAC		0xFD86 /* unique local MAC addr */
#define WI_RID_PCI_INFO		0xFD87 /* point coordination func cap */

/*
 * Scan Information
 */
#define WI_RID_BCAST_SCAN_REQ	0xFCAB /* Broadcast Scan request (Symbol) */
#define BSCAN_5SEC		0x01
#define BSCAN_ONETIME		0x02
#define BSCAN_PASSIVE		0x40
#define BSCAN_BCAST		0x80
#define WI_RID_SCAN_REQ		0xFCE1 /* Scan request (STA only) */
#define WI_RID_JOIN_REQ		0xFCE2 /* Join request (STA only) */
#define WI_RID_AUTH_STATION	0xFCE3 /* Authenticates Station (AP) */
#define WI_RID_CHANNEL_REQ	0xFCE4 /* Channel Information Request (AP) */
#define WI_RID_SCAN_RES		0xFD88 /* Scan Results Table */

struct wi_apinfo {
	int			scanreason;	/* ScanReason */
	char			bssid[6];	/* BSSID (mac address) */
	int			channel;	/* Channel */
	int			signal;		/* Signal level */
	int			noise;		/* Average Noise Level*/
	int			quality;	/* Quality */
	int			namelen;	/* Length of SSID string */
	char			name[32];	/* SSID string */
	int			capinfo;	/* Capability info. */ 
	int			interval;	/* BSS Beacon Interval */
	int			rate;		/* Data Rate */
};

/*
 * The following do not get passed down to the card, they are used
 * by wicontrol to modify the behavior of the driver (use WEP in software or
 * firmware, use alternate cryptographic algorithms, etc.)  I'm calling them
 * "fake record IDs."
 */
#define	WI_FRID_CRYPTO_ALG	0xFCE3
#define	WI_FRID_DEBUGGING	0xFCE4

/*
 * bsd-airtools v0.2 - source-mods v0.2 [common.h]
 * by h1kari - (c) Dachb0den Labs 2001
 */

/*
 * Copyright (c) 2001 Dachb0den Labs.
 *      David Hulton <h1kari@dachb0den.com>.  All rights reserved.
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
 *      This product includes software developed by David Hulton.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY David Hulton AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL David Hulton OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * standard hermes receive frame used by wavelan/prism2 cards
 */
struct wi_rx_frame {
	/*
	 * hermes prefix header. supplies information on the current status of
	 * the network and various other statistics gathered from the
	 * management/control frames as used internally.
	 */
	u_int16_t	wi_status;
	u_int16_t	wi_ts0;
	u_int16_t	wi_ts1;
	u_int8_t	wi_silence;
	u_int8_t	wi_signal;
	u_int8_t	wi_rate;
	u_int8_t	wi_rx_flow;
	u_int16_t	wi_rsvd0;
	u_int16_t	wi_rsvd1;
	/*
	 * standard 80211 frame header. all packets have to use this header as
	 * per the AN9900 from intersil, even management/control. for
	 * management packets, they just threw the header into the data field,
	 * but for control packets the headers are lost in translation and
	 * therefore not all control packet info can be displayed.
	 */
	u_int16_t	wi_frame_ctl;
	u_int16_t	wi_id;
	u_int8_t	wi_addr1[6];
	u_int8_t	wi_addr2[6];
	u_int8_t	wi_addr3[6];
	u_int16_t	wi_seq_ctl;
	u_int8_t	wi_addr4[6];
	u_int16_t	wi_dat_len;
	/*
	 * another oddity with the drivers. they append a 802.3 header which
	 * is somewhat redundant, since all the same data is provided in the
	 * 802.11 header.
	 */
	u_int8_t	wi_dst_addr[6];
	u_int8_t	wi_src_addr[6];
	u_int16_t	wi_len;
};
#define WI_DATA_HDRLEN		WI_802_11_OFFSET
#define WI_MGMT_HDRLEN		WI_802_11_OFFSET_RAW
#define WI_CTL_HDRLEN		WI_802_11_OFFSET_RAW


/*
 * all data packets have a snap (sub-network access protocol) header that
 * isn't entirely defined, but added for ethernet compatibility.
 */
struct wi_snap_frame {
	u_int16_t	wi_dat[3];
	u_int16_t	wi_type;
};


/*
 * management frame headers
 * note: all management frames consist of a static header and variable length
 * fields.
 */

/*
 * variable length field structure
 */
struct wi_mgmt_var_hdr {
	u_int8_t	wi_code;
	u_int8_t	wi_len;
	u_int8_t	wi_data[256];
};

/*
 * management beacon frame prefix
 */
struct wi_mgmt_beacon_hdr {
	u_int32_t	wi_ts0;
	u_int32_t	wi_ts1;
	u_int16_t	wi_interval;
	u_int16_t	wi_capinfo;
};

/*
 * ibss announcement traffic indication message (atim) frame
 * note: no parameters
 */

/*
 * management disassociation frame
 */
struct wi_mgmt_disas_hdr {
	u_int16_t	wi_reason;
};

/*
 * management association request frame prefix
 */
struct wi_mgmt_asreq_hdr {
	u_int16_t	wi_capinfo;
	u_int16_t	wi_interval;
};

/*
 * management association response frame prefix
 */
struct wi_mgmt_asresp_hdr {
	u_int16_t	wi_capinfo;
	u_int16_t	wi_status;
	u_int16_t	wi_aid;
};

/*
 * management reassociation request frame prefix
 */
struct wi_mgmt_reasreq_hdr {
	u_int16_t	wi_capinfo;
	u_int16_t	wi_interval;
	u_int8_t	wi_currap[6];
};

/*
 * management reassociation response frame prefix
 */
struct wi_mgmt_reasresp_hdr {
	u_int16_t	wi_capinfo;
	u_int16_t	wi_status;
	u_int16_t	wi_aid;
};

/*
 * management probe request frame prefix
 * note: no static parameters, only variable length
 */

/*
 * management probe response frame prefix
 */
struct wi_mgmt_proberesp_hdr {
	u_int32_t	wi_ts0;
	u_int32_t	wi_ts1;
	u_int16_t	wi_interval;
	u_int16_t	wi_capinfo;
};

/*
 * management authentication frame prefix
 */
struct wi_mgmt_auth_hdr {
	u_int16_t	wi_algo;
	u_int16_t	wi_seq;
	u_int16_t	wi_status;
};

/*
 * management deauthentication frame
 */
struct wi_mgmt_deauth_hdr {
	u_int16_t	wi_reason;
};


/*
 * rid configuration register definitions
 */
#define WI_RID_PROCFRAME	0x3137 /* Return full frame information */
#define WI_RID_PRISM2		0x3138 /* tell if we're a prism2 card or not */


/*
 * 802.11 definitions
 */
#define WI_STAT_BADCRC		0x0001
#define WI_STAT_UNDECRYPTABLE	0x0002
#define WI_STAT_ERRSTAT		0x0003
#define WI_STAT_MAC_PORT	0x0700
#define WI_STAT_1042		0x2000
#define WI_STAT_TUNNEL		0x4000
#define WI_STAT_WMP_MSG		0x6000
#define WI_RXSTAT_MSG_TYPE	0xE000

#define WI_FCTL_OPT_MASK	0xFF00
#define WI_AID_SET		0xC000
#define WI_AID_MASK		0x3FFF
#define WI_SCTL_FRAGNUM_MASK	0x000F
#define WI_SCTL_SEQNUM_MASK	0xFFF0

#define WI_STAT_UNSPEC_FAIL	1
#define WI_STAT_CAPINFO_FAIL	10
#define WI_STAT_REAS_DENY	11
#define WI_STAT_ASSOC_DENY	12
#define WI_STAT_ALGO_FAIL	13
#define WI_STAT_SEQ_FAIL	14
#define WI_STAT_CHAL_FAIL	15
#define WI_STAT_TOUT_FAIL	16
#define WI_STAT_OVERL_DENY	17
#define WI_STAT_RATE_DENY	18

#define WI_FTYPE_MGMT		0x0000
#define WI_FTYPE_CTL		0x0004
#define WI_FTYPE_DATA		0x0008

#define WI_FCTL_VERS		0x0002
#define WI_FCTL_FTYPE		0x000C
#define WI_FCTL_STYPE		0x00F0
#define WI_FCTL_TODS		0x0100
#define WI_FCTL_FROMDS		0x0200
#define WI_FCTL_MOREFRAGS	0x0400
#define WI_FCTL_RETRY		0x0800
#define WI_FCTL_PM		0x1000
#define WI_FCTL_MOREDATA	0x2000
#define WI_FCTL_WEP		0x4000
#define WI_FCTL_ORDER		0x8000

#define WI_FCS_LEN		0x4 /* checksum length */


/*
 * management definitions
 */
#define WI_CAPINFO_ESS		0x01
#define WI_CAPINFO_IBSS		0x02
#define WI_CAPINFO_CFPOLL	0x04
#define WI_CAPINFO_CFPOLLREQ	0x08
#define WI_CAPINFO_PRIV		0x10

#define WI_REASON_UNSPEC	1
#define WI_REASON_AUTH_INVALID	2
#define WI_REASON_DEAUTH_LEAVE	3
#define WI_REASON_DISAS_INACT	4
#define WI_REASON_DISAS_OVERL	5
#define WI_REASON_CLASS2	6
#define WI_REASON_CLASS3	7
#define WI_REASON_DISAS_LEAVE	8
#define WI_REASON_NOAUTH	9

#define WI_VAR_SSID		0
#define WI_VAR_SRATES		1
#define WI_VAR_FH		2
#define WI_VAR_DS		3
#define WI_VAR_CF		4
#define WI_VAR_TIM		5
#define WI_VAR_IBSS		6
#define WI_VAR_CHAL		16

#define WI_VAR_SRATES_MASK	0x7F

/*
 * ap scanning structures
 */
struct wi_scan_res {
	u_int16_t	wi_chan;
	u_int16_t	wi_noise;
	u_int16_t	wi_signal;
	u_int8_t	wi_bssid[6];
	u_int16_t	wi_interval;
	u_int16_t	wi_capinfo;
	u_int16_t	wi_ssid_len;
	u_int8_t	wi_ssid[32];
	u_int8_t	wi_srates[10];
	u_int8_t	wi_rate;
	u_int8_t	wi_rsvd;
};
#define WI_WAVELAN_RES_1M	0x0a
#define WI_WAVELAN_RES_2M	0x14
#define WI_WAVELAN_RES_5M	0x37
#define WI_WAVELAN_RES_11M	0x6e

#define WI_WAVELAN_RES_SIZE	50
#define WI_WAVELAN_RES_TIMEOUT	((hz / 10) * 2)		/* 200ms */
#define WI_WAVELAN_RES_TRIES	100

struct wi_scan_p2_hdr {
	u_int16_t	wi_rsvd;
	u_int16_t	wi_reason;
};
#define WI_PRISM2_RES_SIZE	62

/*
 * prism2 debug mode definitions
 */
#define SIOCSPRISM2DEBUG	_IOW('i', 137, struct ifreq)
#define SIOCGPRISM2DEBUG	_IOWR('i', 138, struct ifreq)

#define WI_CMD_DEBUG		0x0038 /* prism2 debug */

#define WI_DEBUG_RESET		0x00
#define WI_DEBUG_INIT		0x01
#define WI_DEBUG_SLEEP		0x02
#define WI_DEBUG_WAKE		0x03
#define WI_DEBUG_CHAN		0x08
#define WI_DEBUG_DELAYSUPP	0x09
#define WI_DEBUG_TXSUPP		0x0A
#define WI_DEBUG_MONITOR	0x0B
#define WI_DEBUG_LEDTEST	0x0C
#define WI_DEBUG_CONTTX		0x0E
#define WI_DEBUG_STOPTEST	0x0F
#define WI_DEBUG_CONTRX		0x10
#define WI_DEBUG_SIGSTATE	0x11
#define WI_DEBUG_CALENABLE	0x13
#define WI_DEBUG_CONFBITS	0x15

/*
 * Modem information
 */
#define WI_RID_PHY_TYPE		0xFDC0 /* phys layer type indication */
#define WI_RID_CURRENT_CHAN	0xFDC1 /* current frequency */
#define WI_RID_PWR_STATE	0xFDC2 /* pwr consumption status */
#define WI_RID_CCA_MODE		0xFDC3 /* clear chan assess mode indication */
#define WI_RID_CCA_TIME		0xFDC4 /* clear chan assess time */
#define WI_RID_MAC_PROC_DELAY	0xFDC5 /* MAC processing delay time */
#define WI_RID_DATA_RATES	0xFDC6 /* supported data rates */

/*
 * Values for supported crypto algorithms:
 */
#define	WI_CRYPTO_FIRMWARE_WEP		0x00 /* default */
#define	WI_CRYPTO_SOFTWARE_WEP		0x01

/* Firmware types */
#define	WI_NOTYPE	0
#define	WI_LUCENT	1
#define	WI_INTERSIL	2
#define	WI_SYMBOL	3

/* Card identities */
#define	WI_NIC_LUCENT		0x0001

#define	WI_NIC_SONY		0x0002

#define	WI_NIC_LUCENT_EMB	0x0005

#define	WI_NIC_EVB2		0x8000

#define	WI_NIC_HWB3763		0x8001

#define	WI_NIC_HWB3163		0x8002

#define	WI_NIC_HWB3163B		0x8003

#define	WI_NIC_EVB3		0x8004

#define	WI_NIC_HWB1153		0x8007

#define	WI_NIC_P2_SST		0x8008	/* Prism2 with SST flush */

#define	WI_NIC_EVB2_SST		0x8009

#define	WI_NIC_3842_EVA		0x800A	/* 3842 Evaluation Board */

#define	WI_NIC_3842_PCMCIA_AMD	0x800B	/* Prism2.5 PCMCIA */
#define	WI_NIC_3842_PCMCIA_SST	0x800C
#define	WI_NIC_3842_PCMCIA_ATL	0x800D
#define	WI_NIC_3842_PCMCIA_ATS	0x800E

#define WI_NIC_3842_USB_AMD	0x800f	/* Prism2.5 USB */
#define WI_NIC_3842_USB_SST	0x8010
#define WI_NIC_3842_USB_ATL	0x8011

#define	WI_NIC_3842_MINI_AMD	0x8012	/* Prism2.5 Mini-PCI */
#define	WI_NIC_3842_MINI_SST	0x8013
#define	WI_NIC_3842_MINI_ATL	0x8014
#define	WI_NIC_3842_MINI_ATS	0x8015

#define	WI_NIC_3842_PCI_AMD	0x8016	/* Prism2.5 PCI-bridge */
#define	WI_NIC_3842_PCI_SST	0x8017
#define	WI_NIC_3842_PCI_ATL	0x8018
#define	WI_NIC_3842_PCI_ATS	0x8019

#define	WI_NIC_P3_PCMCIA_AMD	0x801A	/* Prism3 PCMCIA */
#define	WI_NIC_P3_PCMCIA_SST	0x801B
#define	WI_NIC_P3_PCMCIA_ATL	0x801C
#define	WI_NIC_P3_PCMCIA_ATS	0x801D

#define WI_NIC_3842_USB_AMD_2	0x801E	/* Prism2.5 USB */
#define WI_NIC_3842_USB_SST_2	0x801F
#define WI_NIC_3842_USB_ATL_2	0x8020

#define	WI_NIC_P3_MINI_AMD	0x8021	/* Prism3 Mini-PCI */
#define	WI_NIC_P3_MINI_SST	0x8022
#define	WI_NIC_P3_MINI_ATL	0x8023
#define	WI_NIC_P3_MINI_ATS	0x8024

#define WI_NIC_P3_USB		0x8025 	/* Prism3 USB */
#define	WI_NIC_P3_USB_NETGEAR	0x8026
#define WI_NIC_P3_USB_2		0x8027

struct wi_card_ident {
	const u_int16_t	card_id;
	const char	*card_name;
	const u_int8_t	firm_type;
};

#define WI_CARD_IDS							\
	{								\
		WI_NIC_LUCENT,						\
		"Lucent WaveLAN/IEEE",					\
		WI_LUCENT						\
	}, {								\
		WI_NIC_SONY,						\
		"Sony WaveLAN/IEEE",					\
		WI_LUCENT						\
	}, {								\
		WI_NIC_LUCENT_EMB,					\
		"Lucent Embedded WaveLAN/IEEE",				\
		WI_LUCENT						\
	}, {								\
		WI_NIC_EVB2,						\
		"PRISM2 HFA3841(EVB2)",					\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_HWB3763,						\
		"PRISM2 HWB3763 rev.B",					\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_HWB3163,						\
		"PRISM2 HWB3163 rev.A",					\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_HWB3163B,					\
		"PRISM2 HWB3163 rev.B",					\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_EVB3,						\
		"PRISM2 HFA3842(EVB3)",					\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_HWB1153,						\
		"PRISM1 HWB1153",					\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_P2_SST,						\
		"PRISM2 HWB3163 SST-flash",				\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_EVB2_SST,					\
		"PRISM2 HWB3163(EVB2) SST-flash",			\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_3842_EVA,					\
		"PRISM2 HFA3842(EVAL)",					\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_3842_PCMCIA_AMD,					\
		"PRISM2.5 ISL3873",					\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_3842_PCMCIA_SST,					\
		"PRISM2.5 ISL3873",					\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_3842_PCMCIA_ATL,					\
		"PRISM2.5 ISL3873",					\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_3842_PCMCIA_ATS,					\
		"PRISM2.5 ISL3873",					\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_3842_USB_AMD,					\
		"PRISM2.5 USB",						\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_3842_USB_SST,					\
		"PRISM2.5 USB",						\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_3842_USB_ATL,					\
		"PRISM2.5 USB",						\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_3842_MINI_AMD,					\
		"PRISM2.5 ISL3874A(Mini-PCI)",				\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_3842_MINI_SST,					\
		"PRISM2.5 ISL3874A(Mini-PCI)",				\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_3842_MINI_ATL,					\
		"PRISM2.5 ISL3874A(Mini-PCI)",				\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_3842_MINI_ATS,					\
		"PRISM2.5 ISL3874A(Mini-PCI)",				\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_3842_PCI_AMD,					\
		"PRISM2.5 ISL3874A(PCI-bridge)",			\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_3842_PCI_SST,					\
		"PRISM2.5 ISL3874A(PCI-bridge)",			\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_3842_PCI_ATS,					\
		"PRISM2.5 ISL3874A(PCI-bridge)",			\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_3842_PCI_ATL,					\
		"PRISM2.5 ISL3874A(PCI-bridge)",			\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_P3_PCMCIA_AMD,					\
		"PRISM3 ISL37300P",					\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_P3_PCMCIA_SST,					\
		"PRISM3 ISL37300P",					\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_P3_PCMCIA_ATL,					\
		"PRISM3 ISL37300P",					\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_P3_PCMCIA_ATS,					\
		"PRISM3 ISL37300P",					\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_3842_USB_AMD_2,					\
		"PRISM2.5 USB",						\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_3842_USB_SST_2,					\
		"PRISM2.5 USB",						\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_3842_USB_ATL_2,					\
		"PRISM2.5 USB",						\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_P3_MINI_AMD,					\
		"PRISM3 ISL37300P(PCI)",				\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_P3_MINI_SST,					\
		"PRISM3 ISL37300P(PCI)",				\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_P3_MINI_ATL,					\
		"PRISM3 ISL37300P(PCI)",				\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_P3_MINI_ATS,					\
		"PRISM3 ISL37300P(PCI)",				\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_P3_USB,						\
		"PRISM3 (USB)",						\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_P3_USB_NETGEAR,					\
		"PRISM3 (USB)",						\
		WI_INTERSIL						\
	}, {								\
		WI_NIC_P3_USB_2,						\
		"PRISM3 (USB)",						\
		WI_INTERSIL						\
	}, {								\
		0,							\
		NULL,							\
		WI_NOTYPE						\
	}

#endif /* _IF_WI_IEEE_H */
