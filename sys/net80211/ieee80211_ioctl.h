/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Atsushi Onoe
 * Copyright (c) 2002-2009 Sam Leffler, Errno Consulting
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
#ifndef _NET80211_IEEE80211_IOCTL_H_
#define _NET80211_IEEE80211_IOCTL_H_

/*
 * IEEE 802.11 ioctls.
 */
#include <net80211/_ieee80211.h>
#include <net80211/ieee80211.h>
#include <net80211/ieee80211_crypto.h>

/*
 * Per/node (station) statistics.
 */
struct ieee80211_nodestats {
	uint32_t	ns_rx_data;		/* rx data frames */
	uint32_t	ns_rx_mgmt;		/* rx management frames */
	uint32_t	ns_rx_ctrl;		/* rx control frames */
	uint32_t	ns_rx_ucast;		/* rx unicast frames */
	uint32_t	ns_rx_mcast;		/* rx multi/broadcast frames */
	uint64_t	ns_rx_bytes;		/* rx data count (bytes) */
	uint64_t	ns_rx_beacons;		/* rx beacon frames */
	uint32_t	ns_rx_proberesp;	/* rx probe response frames */

	uint32_t	ns_rx_dup;		/* rx discard 'cuz dup */
	uint32_t	ns_rx_noprivacy;	/* rx w/ wep but privacy off */
	uint32_t	ns_rx_wepfail;		/* rx wep processing failed */
	uint32_t	ns_rx_demicfail;	/* rx demic failed */
	uint32_t	ns_rx_decap;		/* rx decapsulation failed */
	uint32_t	ns_rx_defrag;		/* rx defragmentation failed */
	uint32_t	ns_rx_disassoc;		/* rx disassociation */
	uint32_t	ns_rx_deauth;		/* rx deauthentication */
	uint32_t	ns_rx_action;		/* rx action */
	uint32_t	ns_rx_decryptcrc;	/* rx decrypt failed on crc */
	uint32_t	ns_rx_unauth;		/* rx on unauthorized port */
	uint32_t	ns_rx_unencrypted;	/* rx unecrypted w/ privacy */
	uint32_t	ns_rx_drop;		/* rx discard other reason */

	uint32_t	ns_tx_data;		/* tx data frames */
	uint32_t	ns_tx_mgmt;		/* tx management frames */
	uint32_t	ns_tx_ctrl;		/* tx control frames */
	uint32_t	ns_tx_ucast;		/* tx unicast frames */
	uint32_t	ns_tx_mcast;		/* tx multi/broadcast frames */
	uint64_t	ns_tx_bytes;		/* tx data count (bytes) */
	uint32_t	ns_tx_probereq;		/* tx probe request frames */

	uint32_t	ns_tx_novlantag;	/* tx discard 'cuz no tag */
	uint32_t	ns_tx_vlanmismatch;	/* tx discard 'cuz bad tag */

	uint32_t	ns_ps_discard;		/* ps discard 'cuz of age */

	/* MIB-related state */
	uint32_t	ns_tx_assoc;		/* [re]associations */
	uint32_t	ns_tx_assoc_fail;	/* [re]association failures */
	uint32_t	ns_tx_auth;		/* [re]authentications */
	uint32_t	ns_tx_auth_fail;	/* [re]authentication failures*/
	uint32_t	ns_tx_deauth;		/* deauthentications */
	uint32_t	ns_tx_deauth_code;	/* last deauth reason */
	uint32_t	ns_tx_disassoc;		/* disassociations */
	uint32_t	ns_tx_disassoc_code;	/* last disassociation reason */

	/* Hardware A-MSDU decode */
	uint32_t	ns_rx_amsdu_more;	/* RX decap A-MSDU, more coming from A-MSDU */
	uint32_t	ns_rx_amsdu_more_end;	/* RX decap A-MSDU (or any other frame), no more coming */
	uint32_t	ns_spare[6];
};

/*
 * Summary statistics.
 */
struct ieee80211_stats {
	uint32_t	is_rx_badversion;	/* rx frame with bad version */
	uint32_t	is_rx_tooshort;		/* rx frame too short */
	uint32_t	is_rx_wrongbss;		/* rx from wrong bssid */
	uint32_t	is_rx_dup;		/* rx discard 'cuz dup */
	uint32_t	is_rx_wrongdir;		/* rx w/ wrong direction */
	uint32_t	is_rx_mcastecho;	/* rx discard 'cuz mcast echo */
	uint32_t	is_rx_notassoc;		/* rx discard 'cuz sta !assoc */
	uint32_t	is_rx_noprivacy;	/* rx w/ wep but privacy off */
	uint32_t	is_rx_unencrypted;	/* rx w/o wep and privacy on */
	uint32_t	is_rx_wepfail;		/* rx wep processing failed */
	uint32_t	is_rx_decap;		/* rx decapsulation failed */
	uint32_t	is_rx_mgtdiscard;	/* rx discard mgt frames */
	uint32_t	is_rx_ctl;		/* rx ctrl frames */
	uint32_t	is_rx_beacon;		/* rx beacon frames */
	uint32_t	is_rx_rstoobig;		/* rx rate set truncated */
	uint32_t	is_rx_elem_missing;	/* rx required element missing*/
	uint32_t	is_rx_elem_toobig;	/* rx element too big */
	uint32_t	is_rx_elem_toosmall;	/* rx element too small */
	uint32_t	is_rx_elem_unknown;	/* rx element unknown */
	uint32_t	is_rx_badchan;		/* rx frame w/ invalid chan */
	uint32_t	is_rx_chanmismatch;	/* rx frame chan mismatch */
	uint32_t	is_rx_nodealloc;	/* rx frame dropped */
	uint32_t	is_rx_ssidmismatch;	/* rx frame ssid mismatch  */
	uint32_t	is_rx_auth_unsupported;	/* rx w/ unsupported auth alg */
	uint32_t	is_rx_auth_fail;	/* rx sta auth failure */
	uint32_t	is_rx_auth_countermeasures;/* rx auth discard 'cuz CM */
	uint32_t	is_rx_assoc_bss;	/* rx assoc from wrong bssid */
	uint32_t	is_rx_assoc_notauth;	/* rx assoc w/o auth */
	uint32_t	is_rx_assoc_capmismatch;/* rx assoc w/ cap mismatch */
	uint32_t	is_rx_assoc_norate;	/* rx assoc w/ no rate match */
	uint32_t	is_rx_assoc_badwpaie;	/* rx assoc w/ bad WPA IE */
	uint32_t	is_rx_deauth;		/* rx deauthentication */
	uint32_t	is_rx_disassoc;		/* rx disassociation */
	uint32_t	is_rx_badsubtype;	/* rx frame w/ unknown subtype*/
	uint32_t	is_rx_nobuf;		/* rx failed for lack of buf */
	uint32_t	is_rx_decryptcrc;	/* rx decrypt failed on crc */
	uint32_t	is_rx_ahdemo_mgt;	/* rx discard ahdemo mgt frame*/
	uint32_t	is_rx_bad_auth;		/* rx bad auth request */
	uint32_t	is_rx_unauth;		/* rx on unauthorized port */
	uint32_t	is_rx_badkeyid;		/* rx w/ incorrect keyid */
	uint32_t	is_rx_ccmpreplay;	/* rx seq# violation (CCMP) */
	uint32_t	is_rx_ccmpformat;	/* rx format bad (CCMP) */
	uint32_t	is_rx_ccmpmic;		/* rx MIC check failed (CCMP) */
	uint32_t	is_rx_tkipreplay;	/* rx seq# violation (TKIP) */
	uint32_t	is_rx_tkipformat;	/* rx format bad (TKIP) */
	uint32_t	is_rx_tkipmic;		/* rx MIC check failed (TKIP) */
	uint32_t	is_rx_tkipicv;		/* rx ICV check failed (TKIP) */
	uint32_t	is_rx_badcipher;	/* rx failed 'cuz key type */
	uint32_t	is_rx_nocipherctx;	/* rx failed 'cuz key !setup */
	uint32_t	is_rx_acl;		/* rx discard 'cuz acl policy */
	uint32_t	is_tx_nobuf;		/* tx failed for lack of buf */
	uint32_t	is_tx_nonode;		/* tx failed for no node */
	uint32_t	is_tx_unknownmgt;	/* tx of unknown mgt frame */
	uint32_t	is_tx_badcipher;	/* tx failed 'cuz key type */
	uint32_t	is_tx_nodefkey;		/* tx failed 'cuz no defkey */
	uint32_t	is_tx_noheadroom;	/* tx failed 'cuz no space */
	uint32_t	is_tx_fragframes;	/* tx frames fragmented */
	uint32_t	is_tx_frags;		/* tx fragments created */
	uint32_t	is_scan_active;		/* active scans started */
	uint32_t	is_scan_passive;	/* passive scans started */
	uint32_t	is_node_timeout;	/* nodes timed out inactivity */
	uint32_t	is_crypto_nomem;	/* no memory for crypto ctx */
	uint32_t	is_crypto_tkip;		/* tkip crypto done in s/w */
	uint32_t	is_crypto_tkipenmic;	/* tkip en-MIC done in s/w */
	uint32_t	is_crypto_tkipdemic;	/* tkip de-MIC done in s/w */
	uint32_t	is_crypto_tkipcm;	/* tkip counter measures */
	uint32_t	is_crypto_ccmp;		/* ccmp crypto done in s/w */
	uint32_t	is_crypto_wep;		/* wep crypto done in s/w */
	uint32_t	is_crypto_setkey_cipher;/* cipher rejected key */
	uint32_t	is_crypto_setkey_nokey;	/* no key index for setkey */
	uint32_t	is_crypto_delkey;	/* driver key delete failed */
	uint32_t	is_crypto_badcipher;	/* unknown cipher */
	uint32_t	is_crypto_nocipher;	/* cipher not available */
	uint32_t	is_crypto_attachfail;	/* cipher attach failed */
	uint32_t	is_crypto_swfallback;	/* cipher fallback to s/w */
	uint32_t	is_crypto_keyfail;	/* driver key alloc failed */
	uint32_t	is_crypto_enmicfail;	/* en-MIC failed */
	uint32_t	is_ibss_capmismatch;	/* merge failed-cap mismatch */
	uint32_t	is_ibss_norate;		/* merge failed-rate mismatch */
	uint32_t	is_ps_unassoc;		/* ps-poll for unassoc. sta */
	uint32_t	is_ps_badaid;		/* ps-poll w/ incorrect aid */
	uint32_t	is_ps_qempty;		/* ps-poll w/ nothing to send */
	uint32_t	is_ff_badhdr;		/* fast frame rx'd w/ bad hdr */
	uint32_t	is_ff_tooshort;		/* fast frame rx decap error */
	uint32_t	is_ff_split;		/* fast frame rx split error */
	uint32_t	is_ff_decap;		/* fast frames decap'd */
	uint32_t	is_ff_encap;		/* fast frames encap'd for tx */
	uint32_t	is_rx_badbintval;	/* rx frame w/ bogus bintval */
	uint32_t	is_rx_demicfail;	/* rx demic failed */
	uint32_t	is_rx_defrag;		/* rx defragmentation failed */
	uint32_t	is_rx_mgmt;		/* rx management frames */
	uint32_t	is_rx_action;		/* rx action mgt frames */
	uint32_t	is_amsdu_tooshort;	/* A-MSDU rx decap error */
	uint32_t	is_amsdu_split;		/* A-MSDU rx split error */
	uint32_t	is_amsdu_decap;		/* A-MSDU decap'd */
	uint32_t	is_amsdu_encap;		/* A-MSDU encap'd for tx */
	uint32_t	is_ampdu_bar_bad;	/* A-MPDU BAR out of window */
	uint32_t	is_ampdu_bar_oow;	/* A-MPDU BAR before ADDBA */
	uint32_t	is_ampdu_bar_move;	/* A-MPDU BAR moved window */
	uint32_t	is_ampdu_bar_rx;	/* A-MPDU BAR frames handled */
	uint32_t	is_ampdu_rx_flush;	/* A-MPDU frames flushed */
	uint32_t	is_ampdu_rx_oor;	/* A-MPDU frames out-of-order */
	uint32_t	is_ampdu_rx_copy;	/* A-MPDU frames copied down */
	uint32_t	is_ampdu_rx_drop;	/* A-MPDU frames dropped */
	uint32_t	is_tx_badstate;		/* tx discard state != RUN */
	uint32_t	is_tx_notassoc;		/* tx failed, sta not assoc */
	uint32_t	is_tx_classify;		/* tx classification failed */
	uint32_t	is_dwds_mcast;		/* discard mcast over dwds */
	uint32_t	is_dwds_qdrop;		/* dwds pending frame q full */
	uint32_t	is_ht_assoc_nohtcap;	/* non-HT sta rejected */
	uint32_t	is_ht_assoc_downgrade;	/* HT sta forced to legacy */
	uint32_t	is_ht_assoc_norate;	/* HT assoc w/ rate mismatch */
	uint32_t	is_ampdu_rx_age;	/* A-MPDU sent up 'cuz of age */
	uint32_t	is_ampdu_rx_move;	/* A-MPDU MSDU moved window */
	uint32_t	is_addba_reject;	/* ADDBA reject 'cuz disabled */
	uint32_t	is_addba_norequest;	/* ADDBA response w/o ADDBA */
	uint32_t	is_addba_badtoken;	/* ADDBA response w/ wrong
						   dialogtoken */
	uint32_t	is_addba_badpolicy;	/* ADDBA resp w/ wrong policy */
	uint32_t	is_ampdu_stop;		/* A-MPDU stream stopped */
	uint32_t	is_ampdu_stop_failed;	/* A-MPDU stream not running */
	uint32_t	is_ampdu_rx_reorder;	/* A-MPDU held for rx reorder */
	uint32_t	is_scan_bg;		/* background scans started */
	uint8_t		is_rx_deauth_code;	/* last rx'd deauth reason */
	uint8_t		is_rx_disassoc_code;	/* last rx'd disassoc reason */
	uint8_t		is_rx_authfail_code;	/* last rx'd auth fail reason */
	uint32_t	is_beacon_miss;		/* beacon miss notification */
	uint32_t	is_rx_badstate;		/* rx discard state != RUN */
	uint32_t	is_ff_flush;		/* ff's flush'd from stageq */
	uint32_t	is_tx_ctl;		/* tx ctrl frames */
	uint32_t	is_ampdu_rexmt;		/* A-MPDU frames rexmt ok */
	uint32_t	is_ampdu_rexmt_fail;	/* A-MPDU frames rexmt fail */

	uint32_t	is_mesh_wrongmesh;	/* dropped 'cuz not mesh sta*/
	uint32_t	is_mesh_nolink;		/* dropped 'cuz link not estab*/
	uint32_t	is_mesh_fwd_ttl;	/* mesh not fwd'd 'cuz ttl 0 */
	uint32_t	is_mesh_fwd_nobuf;	/* mesh not fwd'd 'cuz no mbuf*/
	uint32_t	is_mesh_fwd_tooshort;	/* mesh not fwd'd 'cuz no hdr */
	uint32_t	is_mesh_fwd_disabled;	/* mesh not fwd'd 'cuz disabled */
	uint32_t	is_mesh_fwd_nopath;	/* mesh not fwd'd 'cuz path unknown */

	uint32_t	is_hwmp_wrongseq;	/* wrong hwmp seq no. */
	uint32_t	is_hwmp_rootreqs;	/* root PREQs sent */
	uint32_t	is_hwmp_rootrann;	/* root RANNs sent */

	uint32_t	is_mesh_badae;		/* dropped 'cuz invalid AE */
	uint32_t	is_mesh_rtaddfailed;	/* route add failed */
	uint32_t	is_mesh_notproxy;	/* dropped 'cuz not proxying */
	uint32_t	is_rx_badalign;		/* dropped 'cuz misaligned */
	uint32_t	is_hwmp_proxy;		/* PREP for proxy route */
	uint32_t	is_beacon_bad;		/* Number of bad beacons */
	uint32_t	is_ampdu_bar_tx;	/* A-MPDU BAR frames TXed */
	uint32_t	is_ampdu_bar_tx_retry;	/* A-MPDU BAR frames TX rtry */
	uint32_t	is_ampdu_bar_tx_fail;	/* A-MPDU BAR frames TX fail */

	uint32_t	is_ff_encapfail;	/* failed FF encap */
	uint32_t	is_amsdu_encapfail;	/* failed A-MSDU encap */

	uint32_t	is_spare[5];
};

/*
 * Max size of optional information elements.  We artificially
 * constrain this; it's limited only by the max frame size (and
 * the max parameter size of the wireless extensions).
 */
#define	IEEE80211_MAX_OPT_IE	256

/*
 * WPA/RSN get/set key request.  Specify the key/cipher
 * type and whether the key is to be used for sending and/or
 * receiving.  The key index should be set only when working
 * with global keys (use IEEE80211_KEYIX_NONE for ``no index'').
 * Otherwise a unicast/pairwise key is specified by the bssid
 * (on a station) or mac address (on an ap).  They key length
 * must include any MIC key data; otherwise it should be no
 * more than IEEE80211_KEYBUF_SIZE.
 */
struct ieee80211req_key {
	uint8_t		ik_type;	/* key/cipher type */
	uint8_t		ik_pad;
	uint16_t	ik_keyix;	/* key index */
	uint8_t		ik_keylen;	/* key length in bytes */
	uint8_t		ik_flags;
/* NB: IEEE80211_KEY_XMIT and IEEE80211_KEY_RECV defined elsewhere */
#define	IEEE80211_KEY_DEFAULT	0x80	/* default xmit key */
	uint8_t		ik_macaddr[IEEE80211_ADDR_LEN];
	uint64_t	ik_keyrsc;	/* key receive sequence counter */
	uint64_t	ik_keytsc;	/* key transmit sequence counter */
	uint8_t		ik_keydata[IEEE80211_KEYBUF_SIZE+IEEE80211_MICBUF_SIZE];
};

/*
 * Delete a key either by index or address.  Set the index
 * to IEEE80211_KEYIX_NONE when deleting a unicast key.
 */
struct ieee80211req_del_key {
	uint8_t		idk_keyix;	/* key index */
	uint8_t		idk_macaddr[IEEE80211_ADDR_LEN];
};

/*
 * MLME state manipulation request.  IEEE80211_MLME_ASSOC
 * only makes sense when operating as a station.  The other
 * requests can be used when operating as a station or an
 * ap (to effect a station).
 */
struct ieee80211req_mlme {
	uint8_t		im_op;		/* operation to perform */
#define	IEEE80211_MLME_ASSOC		1	/* associate station */
#define	IEEE80211_MLME_DISASSOC		2	/* disassociate station */
#define	IEEE80211_MLME_DEAUTH		3	/* deauthenticate station */
#define	IEEE80211_MLME_AUTHORIZE	4	/* authorize station */
#define	IEEE80211_MLME_UNAUTHORIZE	5	/* unauthorize station */
#define	IEEE80211_MLME_AUTH		6	/* authenticate station */
	uint8_t		im_ssid_len;	/* length of optional ssid */
	uint16_t	im_reason;	/* 802.11 reason code */
	uint8_t		im_macaddr[IEEE80211_ADDR_LEN];
	uint8_t		im_ssid[IEEE80211_NWID_LEN];
};

/* 
 * MAC ACL operations.
 */
enum {
	IEEE80211_MACCMD_POLICY_OPEN	= 0,	/* set policy: no ACL's */
	IEEE80211_MACCMD_POLICY_ALLOW	= 1,	/* set policy: allow traffic */
	IEEE80211_MACCMD_POLICY_DENY	= 2,	/* set policy: deny traffic */
	IEEE80211_MACCMD_FLUSH		= 3,	/* flush ACL database */
	IEEE80211_MACCMD_DETACH		= 4,	/* detach ACL policy */
	IEEE80211_MACCMD_POLICY		= 5,	/* get ACL policy */
	IEEE80211_MACCMD_LIST		= 6,	/* get ACL database */
	IEEE80211_MACCMD_POLICY_RADIUS	= 7,	/* set policy: RADIUS managed */
};

struct ieee80211req_maclist {
	uint8_t		ml_macaddr[IEEE80211_ADDR_LEN];
} __packed;

/*
 * Mesh Routing Table Operations.
 */
enum {
	IEEE80211_MESH_RTCMD_LIST   = 0, /* list HWMP routing table */
	IEEE80211_MESH_RTCMD_FLUSH  = 1, /* flush HWMP routing table */
	IEEE80211_MESH_RTCMD_ADD    = 2, /* add entry to the table */
	IEEE80211_MESH_RTCMD_DELETE = 3, /* delete an entry from the table */
};

struct ieee80211req_mesh_route {
	uint8_t		imr_flags;
#define	IEEE80211_MESHRT_FLAGS_DISCOVER	0x01
#define	IEEE80211_MESHRT_FLAGS_VALID	0x02
#define	IEEE80211_MESHRT_FLAGS_PROXY	0x04
#define	IEEE80211_MESHRT_FLAGS_GATE	0x08
	uint8_t		imr_dest[IEEE80211_ADDR_LEN];
	uint8_t		imr_nexthop[IEEE80211_ADDR_LEN];
	uint16_t	imr_nhops;
	uint8_t		imr_pad;
	uint32_t	imr_metric;
	uint32_t	imr_lifetime;
	uint32_t	imr_lastmseq;
};

/*
 * HWMP root modes
 */
enum {
	IEEE80211_HWMP_ROOTMODE_DISABLED	= 0, 	/* disabled */
	IEEE80211_HWMP_ROOTMODE_NORMAL		= 1,	/* normal PREPs */
	IEEE80211_HWMP_ROOTMODE_PROACTIVE	= 2,	/* proactive PREPS */
	IEEE80211_HWMP_ROOTMODE_RANN		= 3,	/* use RANN elemid */
};


/*
 * Set the active channel list by IEEE channel #: each channel
 * to be marked active is set in a bit vector.  Note this list is
 * intersected with the available channel list in calculating
 * the set of channels actually used in scanning.
 */
struct ieee80211req_chanlist {
	uint8_t		ic_channels[32];	/* NB: can be variable length */
};

/*
 * Get the active channel list info.
 */
struct ieee80211req_chaninfo {
	u_int	ic_nchans;
	struct ieee80211_channel ic_chans[1];	/* NB: variable length */
};
#define	IEEE80211_CHANINFO_SIZE(_nchan) \
	(sizeof(struct ieee80211req_chaninfo) + \
	 (((_nchan)-1) * sizeof(struct ieee80211_channel)))
#define	IEEE80211_CHANINFO_SPACE(_ci) \
	IEEE80211_CHANINFO_SIZE((_ci)->ic_nchans)

/*
 * Retrieve the WPA/RSN information element for an associated station.
 */
struct ieee80211req_wpaie {	/* old version w/ only one ie */
	uint8_t		wpa_macaddr[IEEE80211_ADDR_LEN];
	uint8_t		wpa_ie[IEEE80211_MAX_OPT_IE];
};
struct ieee80211req_wpaie2 {
	uint8_t		wpa_macaddr[IEEE80211_ADDR_LEN];
	uint8_t		wpa_ie[IEEE80211_MAX_OPT_IE];
	uint8_t		rsn_ie[IEEE80211_MAX_OPT_IE];
};

/*
 * Retrieve per-node statistics.
 */
struct ieee80211req_sta_stats {
	union {
		/* NB: explicitly force 64-bit alignment */
		uint8_t		macaddr[IEEE80211_ADDR_LEN];
		uint64_t	pad;
	} is_u;
	struct ieee80211_nodestats is_stats;
};

/*
 * Station information block; the mac address is used
 * to retrieve other data like stats, unicast key, etc.
 */
struct ieee80211req_sta_info {
	uint16_t	isi_len;		/* total length (mult of 4) */
	uint16_t	isi_ie_off;		/* offset to IE data */
	uint16_t	isi_ie_len;		/* IE length */
	uint16_t	isi_freq;		/* MHz */
	uint32_t	isi_flags;		/* channel flags */
	uint32_t	isi_state;		/* state flags */
	uint8_t		isi_authmode;		/* authentication algorithm */
	int8_t		isi_rssi;		/* receive signal strength */
	int8_t		isi_noise;		/* noise floor */
	uint8_t		isi_capinfo;		/* capabilities */
	uint8_t		isi_erp;		/* ERP element */
	uint8_t		isi_macaddr[IEEE80211_ADDR_LEN];
	uint8_t		isi_nrates;
						/* negotiated rates */
	uint8_t		isi_rates[IEEE80211_RATE_MAXSIZE];
	uint8_t		isi_txrate;		/* legacy/IEEE rate or MCS */
	uint16_t	isi_associd;		/* assoc response */
	uint16_t	isi_txpower;		/* current tx power */
	uint16_t	isi_vlan;		/* vlan tag */
	/* NB: [IEEE80211_NONQOS_TID] holds seq#'s for non-QoS stations */
	uint16_t	isi_txseqs[IEEE80211_TID_SIZE];/* tx seq #/TID */
	uint16_t	isi_rxseqs[IEEE80211_TID_SIZE];/* rx seq#/TID */
	uint16_t	isi_inact;		/* inactivity timer */
	uint16_t	isi_txmbps;		/* current tx rate in .5 Mb/s */
	uint16_t	isi_pad;
	uint32_t	isi_jointime;		/* time of assoc/join */
	struct ieee80211_mimo_info isi_mimo;	/* MIMO info for 11n sta's */
	/* 11s info */
	uint16_t	isi_peerid;
	uint16_t	isi_localid;
	uint8_t		isi_peerstate;
	/* XXX frag state? */
	/* variable length IE data */
};

/*
 * Retrieve per-station information; to retrieve all
 * specify a mac address of ff:ff:ff:ff:ff:ff.
 */
struct ieee80211req_sta_req {
	union {
		/* NB: explicitly force 64-bit alignment */
		uint8_t		macaddr[IEEE80211_ADDR_LEN];
		uint64_t	pad;
	} is_u;
	struct ieee80211req_sta_info info[1];	/* variable length */
};

/*
 * Get/set per-station tx power cap.
 */
struct ieee80211req_sta_txpow {
	uint8_t		it_macaddr[IEEE80211_ADDR_LEN];
	uint8_t		it_txpow;
};

/*
 * WME parameters manipulated with IEEE80211_IOC_WME_CWMIN
 * through IEEE80211_IOC_WME_ACKPOLICY are set and return
 * using i_val and i_len.  i_val holds the value itself.
 * i_len specifies the AC and, as appropriate, then high bit
 * specifies whether the operation is to be applied to the
 * BSS or ourself.
 */
#define	IEEE80211_WMEPARAM_SELF	0x0000		/* parameter applies to self */
#define	IEEE80211_WMEPARAM_BSS	0x8000		/* parameter applies to BSS */
#define	IEEE80211_WMEPARAM_VAL	0x7fff		/* parameter value */

/*
 * Application Information Elements can be appended to a variety
 * of frames with the IEE80211_IOC_APPIE request.  This request
 * piggybacks on a normal ieee80211req; the frame type is passed
 * in i_val as the 802.11 FC0 bytes and the length of the IE data
 * is passed in i_len.  The data is referenced in i_data.  If i_len
 * is zero then any previously configured IE data is removed.  At
 * most IEEE80211_MAX_APPIE data be appened.  Note that multiple
 * IE's can be supplied; the data is treated opaquely.
 */
#define	IEEE80211_MAX_APPIE	1024		/* max app IE data */
/*
 * Hack: the WPA authenticator uses this mechanism to specify WPA
 * ie's that are used instead of the ones normally constructed using
 * the cipher state setup with separate ioctls.  This avoids issues
 * like the authenticator ordering ie data differently than the
 * net80211 layer and needing to keep separate state for WPA and RSN.
 */
#define	IEEE80211_APPIE_WPA \
	(IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_BEACON | \
	 IEEE80211_FC0_SUBTYPE_PROBE_RESP)

/*
 * Station mode roaming parameters.  These are maintained
 * per band/mode and control the roaming algorithm.
 */
struct ieee80211_roamparams_req {
	struct ieee80211_roamparam params[IEEE80211_MODE_MAX];
};

/*
 * Transmit parameters.  These can be used to set fixed transmit
 * rate for each operating mode when operating as client or on a
 * per-client basis according to the capabilities of the client
 * (e.g. an 11b client associated to an 11g ap) when operating as
 * an ap.
 *
 * MCS are distinguished from legacy rates by or'ing in 0x80.
 */
struct ieee80211_txparams_req {
	struct ieee80211_txparam params[IEEE80211_MODE_MAX];
};

/*
 * Set regulatory domain state with IEEE80211_IOC_REGDOMAIN.
 * Note this is both the regulatory description and the channel
 * list.  The get request for IEEE80211_IOC_REGDOMAIN returns
 * only the regdomain info; the channel list is obtained
 * separately with IEEE80211_IOC_CHANINFO.
 */
struct ieee80211_regdomain_req {
	struct ieee80211_regdomain	rd;
	struct ieee80211req_chaninfo	chaninfo;
};
#define	IEEE80211_REGDOMAIN_SIZE(_nchan) \
	(sizeof(struct ieee80211_regdomain_req) + \
	 (((_nchan)-1) * sizeof(struct ieee80211_channel)))
#define	IEEE80211_REGDOMAIN_SPACE(_req) \
	IEEE80211_REGDOMAIN_SIZE((_req)->chaninfo.ic_nchans)

/*
 * Get driver capabilities.  Driver, hardware crypto, and
 * HT/802.11n capabilities, and a table that describes what
 * the radio can do.
 */
struct ieee80211_devcaps_req {
	uint32_t	dc_drivercaps;		/* general driver caps */
	uint32_t	dc_cryptocaps;		/* hardware crypto support */
	uint32_t	dc_htcaps;		/* HT/802.11n support */
	uint32_t	dc_vhtcaps;		/* VHT/802.11ac capabilities */
	struct ieee80211req_chaninfo dc_chaninfo;
};
#define	IEEE80211_DEVCAPS_SIZE(_nchan) \
	(sizeof(struct ieee80211_devcaps_req) + \
	 (((_nchan)-1) * sizeof(struct ieee80211_channel)))
#define	IEEE80211_DEVCAPS_SPACE(_dc) \
	IEEE80211_DEVCAPS_SIZE((_dc)->dc_chaninfo.ic_nchans)

struct ieee80211_chanswitch_req {
	struct ieee80211_channel csa_chan;	/* new channel */
	int		csa_mode;		/* CSA mode */
	int		csa_count;		/* beacon count to switch */
};

/*
 * Get/set per-station vlan tag.
 */
struct ieee80211req_sta_vlan {
	uint8_t		sv_macaddr[IEEE80211_ADDR_LEN];
	uint16_t	sv_vlan;
};

#ifdef __FreeBSD__
/*
 * FreeBSD-style ioctls.
 */
/* the first member must be matched with struct ifreq */
struct ieee80211req {
	char		i_name[IFNAMSIZ];	/* if_name, e.g. "wi0" */
	uint16_t	i_type;			/* req type */
	int16_t		i_val;			/* Index or simple value */
	uint16_t	i_len;			/* Index or simple value */
	void		*i_data;		/* Extra data */
};
#define	SIOCS80211		 _IOW('i', 234, struct ieee80211req)
#define	SIOCG80211		_IOWR('i', 235, struct ieee80211req)
#define	SIOCG80211STATS		_IOWR('i', 236, struct ifreq)

#define IEEE80211_IOC_SSID		1
#define IEEE80211_IOC_NUMSSIDS		2
#define IEEE80211_IOC_WEP		3
#define 	IEEE80211_WEP_NOSUP	-1
#define 	IEEE80211_WEP_OFF	0
#define 	IEEE80211_WEP_ON	1
#define 	IEEE80211_WEP_MIXED	2
#define IEEE80211_IOC_WEPKEY		4
#define IEEE80211_IOC_NUMWEPKEYS	5
#define IEEE80211_IOC_WEPTXKEY		6
#define IEEE80211_IOC_AUTHMODE		7
#define IEEE80211_IOC_STATIONNAME	8
#define IEEE80211_IOC_CHANNEL		9
#define IEEE80211_IOC_POWERSAVE		10
#define 	IEEE80211_POWERSAVE_NOSUP	-1
#define 	IEEE80211_POWERSAVE_OFF		0
#define 	IEEE80211_POWERSAVE_CAM		1
#define 	IEEE80211_POWERSAVE_PSP		2
#define 	IEEE80211_POWERSAVE_PSP_CAM	3
#define 	IEEE80211_POWERSAVE_ON		IEEE80211_POWERSAVE_CAM
#define IEEE80211_IOC_POWERSAVESLEEP	11
#define	IEEE80211_IOC_RTSTHRESHOLD	12
#define IEEE80211_IOC_PROTMODE		13
#define 	IEEE80211_PROTMODE_OFF		0
#define 	IEEE80211_PROTMODE_CTS		1
#define 	IEEE80211_PROTMODE_RTSCTS	2
#define	IEEE80211_IOC_TXPOWER		14	/* global tx power limit */
#define	IEEE80211_IOC_BSSID		15
#define	IEEE80211_IOC_ROAMING		16	/* roaming mode */
#define	IEEE80211_IOC_PRIVACY		17	/* privacy invoked */
#define	IEEE80211_IOC_DROPUNENCRYPTED	18	/* discard unencrypted frames */
#define	IEEE80211_IOC_WPAKEY		19
#define	IEEE80211_IOC_DELKEY		20
#define	IEEE80211_IOC_MLME		21
/* 22 was IEEE80211_IOC_OPTIE, replaced by IEEE80211_IOC_APPIE */
/* 23 was IEEE80211_IOC_SCAN_REQ */
/* 24 was IEEE80211_IOC_SCAN_RESULTS */
#define	IEEE80211_IOC_COUNTERMEASURES	25	/* WPA/TKIP countermeasures */
#define	IEEE80211_IOC_WPA		26	/* WPA mode (0,1,2) */
#define	IEEE80211_IOC_CHANLIST		27	/* channel list */
#define	IEEE80211_IOC_WME		28	/* WME mode (on, off) */
#define	IEEE80211_IOC_HIDESSID		29	/* hide SSID mode (on, off) */
#define	IEEE80211_IOC_APBRIDGE		30	/* AP inter-sta bridging */
/* 31-35,37-38 were for WPA authenticator settings */
/* 36 was IEEE80211_IOC_DRIVER_CAPS */
#define	IEEE80211_IOC_WPAIE		39	/* WPA information element */
#define	IEEE80211_IOC_STA_STATS		40	/* per-station statistics */
#define	IEEE80211_IOC_MACCMD		41	/* MAC ACL operation */
#define	IEEE80211_IOC_CHANINFO		42	/* channel info list */
#define	IEEE80211_IOC_TXPOWMAX		43	/* max tx power for channel */
#define	IEEE80211_IOC_STA_TXPOW		44	/* per-station tx power limit */
/* 45 was IEEE80211_IOC_STA_INFO */
#define	IEEE80211_IOC_WME_CWMIN		46	/* WME: ECWmin */
#define	IEEE80211_IOC_WME_CWMAX		47	/* WME: ECWmax */
#define	IEEE80211_IOC_WME_AIFS		48	/* WME: AIFSN */
#define	IEEE80211_IOC_WME_TXOPLIMIT	49	/* WME: txops limit */
#define	IEEE80211_IOC_WME_ACM		50	/* WME: ACM (bss only) */
#define	IEEE80211_IOC_WME_ACKPOLICY	51	/* WME: ACK policy (!bss only)*/
#define	IEEE80211_IOC_DTIM_PERIOD	52	/* DTIM period (beacons) */
#define	IEEE80211_IOC_BEACON_INTERVAL	53	/* beacon interval (ms) */
#define	IEEE80211_IOC_ADDMAC		54	/* add sta to MAC ACL table */
#define	IEEE80211_IOC_DELMAC		55	/* del sta from MAC ACL table */
#define	IEEE80211_IOC_PUREG		56	/* pure 11g (no 11b stations) */
#define	IEEE80211_IOC_FF		57	/* ATH fast frames (on, off) */
#define	IEEE80211_IOC_TURBOP		58	/* ATH turbo' (on, off) */
#define	IEEE80211_IOC_BGSCAN		59	/* bg scanning (on, off) */
#define	IEEE80211_IOC_BGSCAN_IDLE	60	/* bg scan idle threshold */
#define	IEEE80211_IOC_BGSCAN_INTERVAL	61	/* bg scan interval */
#define	IEEE80211_IOC_SCANVALID		65	/* scan cache valid threshold */
/* 66-72 were IEEE80211_IOC_ROAM_* and IEEE80211_IOC_MCAST_RATE */
#define	IEEE80211_IOC_FRAGTHRESHOLD	73	/* tx fragmentation threshold */
#define	IEEE80211_IOC_BURST		75	/* packet bursting */
#define	IEEE80211_IOC_SCAN_RESULTS	76	/* get scan results */
#define	IEEE80211_IOC_BMISSTHRESHOLD	77	/* beacon miss threshold */
#define	IEEE80211_IOC_STA_INFO		78	/* station/neighbor info */
#define	IEEE80211_IOC_WPAIE2		79	/* WPA+RSN info elements */
#define	IEEE80211_IOC_CURCHAN		80	/* current channel */
#define	IEEE80211_IOC_SHORTGI		81	/* 802.11n half GI */
#define	IEEE80211_IOC_AMPDU		82	/* 802.11n A-MPDU (on, off) */
#define	IEEE80211_IOC_AMPDU_LIMIT	83	/* A-MPDU length limit */
#define	IEEE80211_IOC_AMPDU_DENSITY	84	/* A-MPDU density */
#define	IEEE80211_IOC_AMSDU		85	/* 802.11n A-MSDU (on, off) */
#define	IEEE80211_IOC_AMSDU_LIMIT	86	/* A-MSDU length limit */
#define	IEEE80211_IOC_PUREN		87	/* pure 11n (no legacy sta's) */
#define	IEEE80211_IOC_DOTH		88	/* 802.11h (on, off) */
/* 89-91 were regulatory items */
#define	IEEE80211_IOC_HTCOMPAT		92	/* support pre-D1.10 HT ie's */
#define	IEEE80211_IOC_DWDS		93	/* DWDS/4-address handling */
#define	IEEE80211_IOC_INACTIVITY	94	/* sta inactivity handling */
#define	IEEE80211_IOC_APPIE		95	/* application IE's */
#define	IEEE80211_IOC_WPS		96	/* WPS operation */
#define	IEEE80211_IOC_TSN		97	/* TSN operation */
#define	IEEE80211_IOC_DEVCAPS		98	/* driver+device capabilities */
#define	IEEE80211_IOC_CHANSWITCH	99	/* start 11h channel switch */
#define	IEEE80211_IOC_DFS		100	/* DFS (on, off) */
#define	IEEE80211_IOC_DOTD		101	/* 802.11d (on, off) */
#define IEEE80211_IOC_HTPROTMODE	102	/* HT protection (off, rts) */
#define	IEEE80211_IOC_SCAN_REQ		103	/* scan w/ specified params */
#define	IEEE80211_IOC_SCAN_CANCEL	104	/* cancel ongoing scan */
#define	IEEE80211_IOC_HTCONF		105	/* HT config (off, HT20, HT40)*/
#define	IEEE80211_IOC_REGDOMAIN		106	/* regulatory domain info */
#define	IEEE80211_IOC_ROAM		107	/* roaming params en masse */
#define	IEEE80211_IOC_TXPARAMS		108	/* tx parameters */
#define	IEEE80211_IOC_STA_VLAN		109	/* per-station vlan tag */
#define	IEEE80211_IOC_SMPS		110	/* MIMO power save */
#define	IEEE80211_IOC_RIFS		111	/* RIFS config (on, off) */
#define	IEEE80211_IOC_GREENFIELD	112	/* Greenfield (on, off) */
#define	IEEE80211_IOC_STBC		113	/* STBC Tx/RX (on, off) */
#define	IEEE80211_IOC_LDPC		114	/* LDPC Tx/RX (on, off) */

/* VHT */
#define	IEEE80211_IOC_VHTCONF		130	/* VHT config (off, on; widths) */

#define	IEEE80211_IOC_MESH_ID		170	/* mesh identifier */
#define	IEEE80211_IOC_MESH_AP		171	/* accepting peerings */
#define	IEEE80211_IOC_MESH_FWRD		172	/* forward frames */
#define	IEEE80211_IOC_MESH_PROTO	173	/* mesh protocols */
#define	IEEE80211_IOC_MESH_TTL		174	/* mesh TTL */
#define	IEEE80211_IOC_MESH_RTCMD	175	/* mesh routing table commands*/
#define	IEEE80211_IOC_MESH_PR_METRIC	176	/* mesh metric protocol */
#define	IEEE80211_IOC_MESH_PR_PATH	177	/* mesh path protocol */
#define	IEEE80211_IOC_MESH_PR_SIG	178	/* mesh sig protocol */
#define	IEEE80211_IOC_MESH_PR_CC	179	/* mesh congestion protocol */
#define	IEEE80211_IOC_MESH_PR_AUTH	180	/* mesh auth protocol */
#define	IEEE80211_IOC_MESH_GATE		181	/* mesh gate XXX: 173? */

#define	IEEE80211_IOC_HWMP_ROOTMODE	190	/* HWMP root mode */
#define	IEEE80211_IOC_HWMP_MAXHOPS	191	/* number of hops before drop */
#define	IEEE80211_IOC_HWMP_TTL		192	/* HWMP TTL */

#define	IEEE80211_IOC_TDMA_SLOT		201	/* TDMA: assigned slot */
#define	IEEE80211_IOC_TDMA_SLOTCNT	202	/* TDMA: slots in bss */
#define	IEEE80211_IOC_TDMA_SLOTLEN	203	/* TDMA: slot length (usecs) */
#define	IEEE80211_IOC_TDMA_BINTERVAL	204	/* TDMA: beacon intvl (slots) */

#define	IEEE80211_IOC_QUIET		205	/* Quiet Enable/Disable */
#define	IEEE80211_IOC_QUIET_PERIOD	206	/* Quiet Period */
#define	IEEE80211_IOC_QUIET_OFFSET	207	/* Quiet Offset */
#define	IEEE80211_IOC_QUIET_DUR		208	/* Quiet Duration */
#define	IEEE80211_IOC_QUIET_COUNT	209	/* Quiet Count */
/*
 * Parameters for controlling a scan requested with
 * IEEE80211_IOC_SCAN_REQ.
 *
 * Active scans cause ProbeRequest frames to be issued for each
 * specified ssid and, by default, a broadcast ProbeRequest frame.
 * The set of ssid's is specified in the request.
 *
 * By default the scan will cause a BSS to be joined (in station/adhoc
 * mode) or a channel to be selected for operation (hostap mode).
 * To disable that specify IEEE80211_IOC_SCAN_NOPICK and if the
 *
 * If the station is currently associated to an AP then a scan request
 * will cause the station to leave the current channel and potentially
 * miss frames from the AP.  Alternatively the station may notify the
 * AP that it is going into power save mode before it leaves the channel.
 * This ensures frames for the station are buffered by the AP.  This is
 * termed a ``bg scan'' and is requested with the IEEE80211_IOC_SCAN_BGSCAN
 * flag.  Background scans may take longer than foreground scans and may
 * be preempted by traffic.  If a station is not associated to an AP
 * then a request for a background scan is automatically done in the
 * foreground.
 *
 * The results of the scan request are cached by the system.  This
 * information is aged out and/or invalidated based on events like not
 * being able to associated to an AP.  To flush the current cache
 * contents before doing a scan the IEEE80211_IOC_SCAN_FLUSH flag may
 * be specified.
 *
 * By default the scan will be done until a suitable AP is located
 * or a channel is found for use.  A scan can also be constrained
 * to be done once (IEEE80211_IOC_SCAN_ONCE) or to last for no more
 * than a specified duration.
 */
struct ieee80211_scan_req {
	int		sr_flags;
#define	IEEE80211_IOC_SCAN_NOPICK	0x00001	/* scan only, no selection */
#define	IEEE80211_IOC_SCAN_ACTIVE	0x00002	/* active scan (probe req) */
#define	IEEE80211_IOC_SCAN_PICK1ST	0x00004	/* ``hey sailor'' mode */
#define	IEEE80211_IOC_SCAN_BGSCAN	0x00008	/* bg scan, exit ps at end */
#define	IEEE80211_IOC_SCAN_ONCE		0x00010	/* do one complete pass */
#define	IEEE80211_IOC_SCAN_NOBCAST	0x00020	/* don't send bcast probe req */
#define	IEEE80211_IOC_SCAN_NOJOIN	0x00040	/* no auto-sequencing */
#define	IEEE80211_IOC_SCAN_FLUSH	0x10000	/* flush scan cache first */
#define	IEEE80211_IOC_SCAN_CHECK	0x20000	/* check scan cache first */
	u_int		sr_duration;		/* duration (ms) */
#define	IEEE80211_IOC_SCAN_DURATION_MIN	1
#define	IEEE80211_IOC_SCAN_DURATION_MAX	0x7fffffff
#define	IEEE80211_IOC_SCAN_FOREVER	IEEE80211_IOC_SCAN_DURATION_MAX
	u_int		sr_mindwell;		/* min channel dwelltime (ms) */
	u_int		sr_maxdwell;		/* max channel dwelltime (ms) */
	int		sr_nssid;
#define	IEEE80211_IOC_SCAN_MAX_SSID	3
	struct {
		int	 len;				/* length in bytes */
		uint8_t ssid[IEEE80211_NWID_LEN];	/* ssid contents */
	} sr_ssid[IEEE80211_IOC_SCAN_MAX_SSID];
};

/*
 * Scan result data returned for IEEE80211_IOC_SCAN_RESULTS.
 * Each result is a fixed size structure followed by a variable
 * length SSID and one or more variable length information elements.
 * The size of each variable length item is found in the fixed
 * size structure and the entire length of the record is specified
 * in isr_len.  Result records are rounded to a multiple of 4 bytes.
 */
struct ieee80211req_scan_result {
	uint16_t	isr_len;		/* total length (mult of 4) */
	uint16_t	isr_ie_off;		/* offset to SSID+IE data */
	uint16_t	isr_ie_len;		/* IE length */
	uint16_t	isr_freq;		/* MHz */
	uint16_t	isr_flags;		/* channel flags */
	int8_t		isr_noise;
	int8_t		isr_rssi;
	uint16_t	isr_intval;		/* beacon interval */
	uint8_t		isr_capinfo;		/* capabilities */
	uint8_t		isr_erp;		/* ERP element */
	uint8_t		isr_bssid[IEEE80211_ADDR_LEN];
	uint8_t		isr_nrates;
	uint8_t		isr_rates[IEEE80211_RATE_MAXSIZE];
	uint8_t		isr_ssid_len;		/* SSID length */
	uint8_t		isr_meshid_len;		/* MESH ID length */
	/* variable length SSID, followed by variable length MESH ID,
	  followed by IE data */
};

/*
 * Virtual AP cloning parameters.  The parent device must
 * be a vap-capable device.  All parameters specified with
 * the clone request are fixed for the lifetime of the vap.
 *
 * There are two flavors of WDS vaps: legacy and dynamic.
 * Legacy WDS operation implements a static binding between
 * two stations encapsulating traffic in 4-address frames.
 * Dynamic WDS vaps are created when a station associates to
 * an AP and sends a 4-address frame.  If the AP vap is
 * configured to support WDS then this will generate an
 * event to user programs listening on the routing socket
 * and a Dynamic WDS vap will be created to handle traffic
 * to/from that station.  In both cases the bssid of the
 * peer must be specified when creating the vap.
 *
 * By default a vap will inherit the mac address/bssid of
 * the underlying device.  To request a unique address the
 * IEEE80211_CLONE_BSSID flag should be supplied.  This is
 * meaningless for WDS vaps as they share the bssid of an
 * AP vap that must otherwise exist.  Note that some devices
 * may not be able to support multiple addresses.
 *
 * Station mode vap's normally depend on the device to notice
 * when the AP stops sending beacon frames.  If IEEE80211_CLONE_NOBEACONS
 * is specified the net80211 layer will do this in s/w.  This
 * is mostly useful when setting up a WDS repeater/extender where
 * an AP vap is combined with a sta vap and the device isn't able
 * to track beacon frames in hardware.
 */
struct ieee80211_clone_params {
	char	icp_parent[IFNAMSIZ];		/* parent device */
	uint16_t icp_opmode;			/* operating mode */
	uint16_t icp_flags;			/* see below */
	uint8_t	icp_bssid[IEEE80211_ADDR_LEN];	/* for WDS links */
	uint8_t	icp_macaddr[IEEE80211_ADDR_LEN];/* local address */
};
#define	IEEE80211_CLONE_BSSID		0x0001	/* allocate unique mac/bssid */
#define	IEEE80211_CLONE_NOBEACONS	0x0002	/* don't setup beacon timers */
#define	IEEE80211_CLONE_WDSLEGACY	0x0004	/* legacy WDS processing */
#define	IEEE80211_CLONE_MACADDR		0x0008	/* use specified mac addr */
#define	IEEE80211_CLONE_TDMA		0x0010	/* operate in TDMA mode */
#endif /* __FreeBSD__ */

#endif /* _NET80211_IEEE80211_IOCTL_H_ */
