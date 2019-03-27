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
#ifndef _NET80211_IEEE80211_H_
#define _NET80211_IEEE80211_H_

/*
 * 802.11 protocol definitions.
 */

#define	IEEE80211_ADDR_LEN	6		/* size of 802.11 address */
/* is 802.11 address multicast/broadcast? */
#define	IEEE80211_IS_MULTICAST(_a)	(*(_a) & 0x01)

#ifdef _KERNEL
extern const uint8_t ieee80211broadcastaddr[];
#endif

typedef uint16_t ieee80211_seq;

/* IEEE 802.11 PLCP header */
struct ieee80211_plcp_hdr {
	uint16_t	i_sfd;
	uint8_t		i_signal;
	uint8_t		i_service;
	uint16_t	i_length;
	uint16_t	i_crc;
} __packed;

#define IEEE80211_PLCP_SFD      0xF3A0 
#define IEEE80211_PLCP_SERVICE  0x00
#define IEEE80211_PLCP_SERVICE_LOCKED	0x04
#define IEEE80211_PLCL_SERVICE_PBCC	0x08
#define IEEE80211_PLCP_SERVICE_LENEXT5	0x20
#define IEEE80211_PLCP_SERVICE_LENEXT6	0x40
#define IEEE80211_PLCP_SERVICE_LENEXT7	0x80

/*
 * generic definitions for IEEE 802.11 frames
 */
struct ieee80211_frame {
	uint8_t		i_fc[2];
	uint8_t		i_dur[2];
	uint8_t		i_addr1[IEEE80211_ADDR_LEN];
	uint8_t		i_addr2[IEEE80211_ADDR_LEN];
	uint8_t		i_addr3[IEEE80211_ADDR_LEN];
	uint8_t		i_seq[2];
	/* possibly followed by addr4[IEEE80211_ADDR_LEN]; */
	/* see below */
} __packed;

struct ieee80211_qosframe {
	uint8_t		i_fc[2];
	uint8_t		i_dur[2];
	uint8_t		i_addr1[IEEE80211_ADDR_LEN];
	uint8_t		i_addr2[IEEE80211_ADDR_LEN];
	uint8_t		i_addr3[IEEE80211_ADDR_LEN];
	uint8_t		i_seq[2];
	uint8_t		i_qos[2];
	/* possibly followed by addr4[IEEE80211_ADDR_LEN]; */
	/* see below */
} __packed;

struct ieee80211_qoscntl {
	uint8_t		i_qos[2];
};

struct ieee80211_frame_addr4 {
	uint8_t		i_fc[2];
	uint8_t		i_dur[2];
	uint8_t		i_addr1[IEEE80211_ADDR_LEN];
	uint8_t		i_addr2[IEEE80211_ADDR_LEN];
	uint8_t		i_addr3[IEEE80211_ADDR_LEN];
	uint8_t		i_seq[2];
	uint8_t		i_addr4[IEEE80211_ADDR_LEN];
} __packed;


struct ieee80211_qosframe_addr4 {
	uint8_t		i_fc[2];
	uint8_t		i_dur[2];
	uint8_t		i_addr1[IEEE80211_ADDR_LEN];
	uint8_t		i_addr2[IEEE80211_ADDR_LEN];
	uint8_t		i_addr3[IEEE80211_ADDR_LEN];
	uint8_t		i_seq[2];
	uint8_t		i_addr4[IEEE80211_ADDR_LEN];
	uint8_t		i_qos[2];
} __packed;

#define	IEEE80211_FC0_VERSION_MASK		0x03
#define	IEEE80211_FC0_VERSION_SHIFT		0
#define	IEEE80211_FC0_VERSION_0			0x00
#define	IEEE80211_FC0_TYPE_MASK			0x0c
#define	IEEE80211_FC0_TYPE_SHIFT		2
#define	IEEE80211_FC0_TYPE_MGT			0x00
#define	IEEE80211_FC0_TYPE_CTL			0x04
#define	IEEE80211_FC0_TYPE_DATA			0x08

#define	IEEE80211_FC0_SUBTYPE_MASK		0xf0
#define	IEEE80211_FC0_SUBTYPE_SHIFT		4
/* for TYPE_MGT */
#define	IEEE80211_FC0_SUBTYPE_ASSOC_REQ		0x00
#define	IEEE80211_FC0_SUBTYPE_ASSOC_RESP	0x10
#define	IEEE80211_FC0_SUBTYPE_REASSOC_REQ	0x20
#define	IEEE80211_FC0_SUBTYPE_REASSOC_RESP	0x30
#define	IEEE80211_FC0_SUBTYPE_PROBE_REQ		0x40
#define	IEEE80211_FC0_SUBTYPE_PROBE_RESP	0x50
#define	IEEE80211_FC0_SUBTYPE_TIMING_ADV	0x60
#define	IEEE80211_FC0_SUBTYPE_BEACON		0x80
#define	IEEE80211_FC0_SUBTYPE_ATIM		0x90
#define	IEEE80211_FC0_SUBTYPE_DISASSOC		0xa0
#define	IEEE80211_FC0_SUBTYPE_AUTH		0xb0
#define	IEEE80211_FC0_SUBTYPE_DEAUTH		0xc0
#define	IEEE80211_FC0_SUBTYPE_ACTION		0xd0
#define	IEEE80211_FC0_SUBTYPE_ACTION_NOACK	0xe0
/* for TYPE_CTL */
#define	IEEE80211_FC0_SUBTYPE_CONTROL_WRAP	0x70
#define	IEEE80211_FC0_SUBTYPE_BAR		0x80
#define	IEEE80211_FC0_SUBTYPE_BA		0x90
#define	IEEE80211_FC0_SUBTYPE_PS_POLL		0xa0
#define	IEEE80211_FC0_SUBTYPE_RTS		0xb0
#define	IEEE80211_FC0_SUBTYPE_CTS		0xc0
#define	IEEE80211_FC0_SUBTYPE_ACK		0xd0
#define	IEEE80211_FC0_SUBTYPE_CF_END		0xe0
#define	IEEE80211_FC0_SUBTYPE_CF_END_ACK	0xf0
/* for TYPE_DATA (bit combination) */
#define	IEEE80211_FC0_SUBTYPE_DATA		0x00
#define	IEEE80211_FC0_SUBTYPE_CF_ACK		0x10
#define	IEEE80211_FC0_SUBTYPE_CF_POLL		0x20
#define	IEEE80211_FC0_SUBTYPE_CF_ACPL		0x30
#define	IEEE80211_FC0_SUBTYPE_NODATA		0x40
#define	IEEE80211_FC0_SUBTYPE_CFACK		0x50
#define	IEEE80211_FC0_SUBTYPE_CFPOLL		0x60
#define	IEEE80211_FC0_SUBTYPE_CF_ACK_CF_ACK	0x70
#define	IEEE80211_FC0_SUBTYPE_QOS		0x80
#define	IEEE80211_FC0_SUBTYPE_QOS_CFACK		0x90
#define	IEEE80211_FC0_SUBTYPE_QOS_CFPOLL	0xa0
#define	IEEE80211_FC0_SUBTYPE_QOS_CFACKPOLL	0xb0
#define	IEEE80211_FC0_SUBTYPE_QOS_NULL		0xc0

#define	IEEE80211_IS_MGMT(wh)					\
	(!! (((wh)->i_fc[0] & IEEE80211_FC0_TYPE_MASK)		\
	    == IEEE80211_FC0_TYPE_MGT))
#define	IEEE80211_IS_CTL(wh)					\
	(!! (((wh)->i_fc[0] & IEEE80211_FC0_TYPE_MASK)		\
	    == IEEE80211_FC0_TYPE_CTL))
#define	IEEE80211_IS_DATA(wh)					\
	(!! (((wh)->i_fc[0] & IEEE80211_FC0_TYPE_MASK)		\
	    == IEEE80211_FC0_TYPE_DATA))

#define	IEEE80211_FC0_QOSDATA \
	(IEEE80211_FC0_TYPE_DATA|IEEE80211_FC0_SUBTYPE_QOS|IEEE80211_FC0_VERSION_0)

#define	IEEE80211_IS_QOSDATA(wh) \
	((wh)->i_fc[0] == IEEE80211_FC0_QOSDATA)

#define	IEEE80211_FC1_DIR_MASK			0x03
#define	IEEE80211_FC1_DIR_NODS			0x00	/* STA->STA */
#define	IEEE80211_FC1_DIR_TODS			0x01	/* STA->AP  */
#define	IEEE80211_FC1_DIR_FROMDS		0x02	/* AP ->STA */
#define	IEEE80211_FC1_DIR_DSTODS		0x03	/* AP ->AP  */

#define	IEEE80211_IS_DSTODS(wh) \
	(((wh)->i_fc[1] & IEEE80211_FC1_DIR_MASK) == IEEE80211_FC1_DIR_DSTODS)

#define	IEEE80211_FC1_MORE_FRAG			0x04
#define	IEEE80211_FC1_RETRY			0x08
#define	IEEE80211_FC1_PWR_MGT			0x10
#define	IEEE80211_FC1_MORE_DATA			0x20
#define	IEEE80211_FC1_PROTECTED			0x40
#define	IEEE80211_FC1_ORDER			0x80

#define IEEE80211_HAS_SEQ(type, subtype) \
	((type) != IEEE80211_FC0_TYPE_CTL && \
	!((type) == IEEE80211_FC0_TYPE_DATA && \
	 ((subtype) & IEEE80211_FC0_SUBTYPE_QOS_NULL) == \
		      IEEE80211_FC0_SUBTYPE_QOS_NULL))
#define	IEEE80211_SEQ_FRAG_MASK			0x000f
#define	IEEE80211_SEQ_FRAG_SHIFT		0
#define	IEEE80211_SEQ_SEQ_MASK			0xfff0
#define	IEEE80211_SEQ_SEQ_SHIFT			4
#define	IEEE80211_SEQ_RANGE			4096

#define	IEEE80211_SEQ_ADD(seq, incr) \
	(((seq) + (incr)) & (IEEE80211_SEQ_RANGE-1))
#define	IEEE80211_SEQ_INC(seq)	IEEE80211_SEQ_ADD(seq,1)
#define	IEEE80211_SEQ_SUB(a, b) \
	(((a) + IEEE80211_SEQ_RANGE - (b)) & (IEEE80211_SEQ_RANGE-1))

#define	IEEE80211_SEQ_BA_RANGE			2048	/* 2^11 */
#define	IEEE80211_SEQ_BA_BEFORE(a, b) \
	(IEEE80211_SEQ_SUB(b, a+1) < IEEE80211_SEQ_BA_RANGE-1)

#define	IEEE80211_NWID_LEN			32
#define	IEEE80211_MESHID_LEN			32

#define	IEEE80211_QOS_CTL_LEN			2

#define	IEEE80211_QOS_TXOP			0x00ff
/* bit 8 is reserved */
#define	IEEE80211_QOS_AMSDU			0x80
#define	IEEE80211_QOS_AMSDU_S			7
#define	IEEE80211_QOS_ACKPOLICY			0x60
#define	IEEE80211_QOS_ACKPOLICY_S		5
#define	IEEE80211_QOS_ACKPOLICY_NOACK		0x20	/* No ACK required */
#define	IEEE80211_QOS_ACKPOLICY_BA		0x60	/* Block ACK */
#define	IEEE80211_QOS_EOSP			0x10	/* EndOfService Period*/
#define	IEEE80211_QOS_EOSP_S			4
#define	IEEE80211_QOS_TID			0x0f
/* qos[1] byte used for all frames sent by mesh STAs in a mesh BSS */
#define IEEE80211_QOS_MC			0x01	/* Mesh control */
/* Mesh power save level*/
#define IEEE80211_QOS_MESH_PSL			0x02
/* Mesh Receiver Service Period Initiated */
#define IEEE80211_QOS_RSPI			0x04
/* bits 11 to 15 reserved */

/* does frame have QoS sequence control data */
#define	IEEE80211_QOS_HAS_SEQ(wh) \
	(((wh)->i_fc[0] & \
	  (IEEE80211_FC0_TYPE_MASK | IEEE80211_FC0_SUBTYPE_QOS)) == \
	  (IEEE80211_FC0_TYPE_DATA | IEEE80211_FC0_SUBTYPE_QOS))

/*
 * WME/802.11e information element.
 */
struct ieee80211_wme_info {
	uint8_t		wme_id;		/* IEEE80211_ELEMID_VENDOR */
	uint8_t		wme_len;	/* length in bytes */
	uint8_t		wme_oui[3];	/* 0x00, 0x50, 0xf2 */
	uint8_t		wme_type;	/* OUI type */
	uint8_t		wme_subtype;	/* OUI subtype */
	uint8_t		wme_version;	/* spec revision */
	uint8_t		wme_info;	/* QoS info */
} __packed;

/*
 * WME/802.11e Tspec Element
 */
struct ieee80211_wme_tspec {
	uint8_t		ts_id;
	uint8_t		ts_len;
	uint8_t		ts_oui[3];
	uint8_t		ts_oui_type;
	uint8_t		ts_oui_subtype;
	uint8_t		ts_version;
	uint8_t		ts_tsinfo[3];
	uint8_t		ts_nom_msdu[2];
	uint8_t		ts_max_msdu[2];
	uint8_t		ts_min_svc[4];
	uint8_t		ts_max_svc[4];
	uint8_t		ts_inactv_intv[4];
	uint8_t		ts_susp_intv[4];
	uint8_t		ts_start_svc[4];
	uint8_t		ts_min_rate[4];
	uint8_t		ts_mean_rate[4];
	uint8_t		ts_max_burst[4];
	uint8_t		ts_min_phy[4];
	uint8_t		ts_peak_rate[4];
	uint8_t		ts_delay[4];
	uint8_t		ts_surplus[2];
	uint8_t		ts_medium_time[2];
} __packed;

/*
 * WME AC parameter field
 */
struct ieee80211_wme_acparams {
	uint8_t		acp_aci_aifsn;
	uint8_t		acp_logcwminmax;
	uint16_t	acp_txop;
} __packed;

#define WME_NUM_AC		4	/* 4 AC categories */
#define	WME_NUM_TID		16	/* 16 tids */

#define WME_PARAM_ACI		0x60	/* Mask for ACI field */
#define WME_PARAM_ACI_S		5	/* Shift for ACI field */
#define WME_PARAM_ACM		0x10	/* Mask for ACM bit */
#define WME_PARAM_ACM_S		4	/* Shift for ACM bit */
#define WME_PARAM_AIFSN		0x0f	/* Mask for aifsn field */
#define WME_PARAM_AIFSN_S	0	/* Shift for aifsn field */
#define WME_PARAM_LOGCWMIN	0x0f	/* Mask for CwMin field (in log) */
#define WME_PARAM_LOGCWMIN_S	0	/* Shift for CwMin field */
#define WME_PARAM_LOGCWMAX	0xf0	/* Mask for CwMax field (in log) */
#define WME_PARAM_LOGCWMAX_S	4	/* Shift for CwMax field */

#define WME_AC_TO_TID(_ac) (       \
	((_ac) == WME_AC_VO) ? 6 : \
	((_ac) == WME_AC_VI) ? 5 : \
	((_ac) == WME_AC_BK) ? 1 : \
	0)

#define TID_TO_WME_AC(_tid) (      \
	((_tid) == 0 || (_tid) == 3) ? WME_AC_BE : \
	((_tid) < 3) ? WME_AC_BK : \
	((_tid) < 6) ? WME_AC_VI : \
	WME_AC_VO)

/*
 * WME Parameter Element
 */
struct ieee80211_wme_param {
	uint8_t		param_id;
	uint8_t		param_len;
	uint8_t		param_oui[3];
	uint8_t		param_oui_type;
	uint8_t		param_oui_subtype;
	uint8_t		param_version;
	uint8_t		param_qosInfo;
#define	WME_QOSINFO_COUNT	0x0f	/* Mask for param count field */
	uint8_t		param_reserved;
	struct ieee80211_wme_acparams	params_acParams[WME_NUM_AC];
} __packed;

/*
 * WME U-APSD qos info field defines
 */
#define	WME_CAPINFO_UAPSD_EN                    0x00000080
#define	WME_CAPINFO_UAPSD_VO                    0x00000001
#define	WME_CAPINFO_UAPSD_VI                    0x00000002
#define	WME_CAPINFO_UAPSD_BK                    0x00000004
#define	WME_CAPINFO_UAPSD_BE                    0x00000008
#define	WME_CAPINFO_UAPSD_ACFLAGS_SHIFT         0
#define	WME_CAPINFO_UAPSD_ACFLAGS_MASK          0xF
#define	WME_CAPINFO_UAPSD_MAXSP_SHIFT           5
#define	WME_CAPINFO_UAPSD_MAXSP_MASK            0x3
#define	WME_CAPINFO_IE_OFFSET                   8
#define	WME_UAPSD_MAXSP(_qosinfo)				\
	    (((_qosinfo) >> WME_CAPINFO_UAPSD_MAXSP_SHIFT) &	\
	    WME_CAPINFO_UAPSD_MAXSP_MASK)
#define	WME_UAPSD_AC_ENABLED(_ac, _qosinfo)			\
	    ((1 << (3 - (_ac))) & (				\
	    ((_qosinfo) >> WME_CAPINFO_UAPSD_ACFLAGS_SHIFT) &	\
	    WME_CAPINFO_UAPSD_ACFLAGS_MASK))

/*
 * Management Notification Frame
 */
struct ieee80211_mnf {
	uint8_t		mnf_category;
	uint8_t		mnf_action;
	uint8_t		mnf_dialog;
	uint8_t		mnf_status;
} __packed;
#define	MNF_SETUP_REQ	0
#define	MNF_SETUP_RESP	1
#define	MNF_TEARDOWN	2

/* 
 * 802.11n Management Action Frames 
 */
/* generic frame format */
struct ieee80211_action {
	uint8_t		ia_category;
	uint8_t		ia_action;
} __packed;

#define	IEEE80211_ACTION_CAT_SM		0	/* Spectrum Management */
#define	IEEE80211_ACTION_CAT_QOS	1	/* QoS */
#define	IEEE80211_ACTION_CAT_DLS	2	/* DLS */
#define	IEEE80211_ACTION_CAT_BA		3	/* BA */
#define	IEEE80211_ACTION_CAT_HT		7	/* HT */
#define	IEEE80211_ACTION_CAT_MESH	13	/* Mesh */
#define	IEEE80211_ACTION_CAT_SELF_PROT	15	/* Self-protected */
/* 16 - 125 reserved */
#define	IEEE80211_ACTION_CAT_VHT	21
#define	IEEE80211_ACTION_CAT_VENDOR	127	/* Vendor Specific */

#define	IEEE80211_ACTION_HT_TXCHWIDTH	0	/* recommended xmit chan width*/
#define	IEEE80211_ACTION_HT_MIMOPWRSAVE	1	/* MIMO power save */

/* HT - recommended transmission channel width */
struct ieee80211_action_ht_txchwidth {
	struct ieee80211_action	at_header;
	uint8_t		at_chwidth;	
} __packed;

#define	IEEE80211_A_HT_TXCHWIDTH_20	0
#define	IEEE80211_A_HT_TXCHWIDTH_2040	1

/* HT - MIMO Power Save (NB: D2.04) */
struct ieee80211_action_ht_mimopowersave {
	struct ieee80211_action am_header;
	uint8_t		am_control;
} __packed;

#define	IEEE80211_A_HT_MIMOPWRSAVE_ENA		0x01	/* PS enabled */
#define	IEEE80211_A_HT_MIMOPWRSAVE_MODE		0x02
#define	IEEE80211_A_HT_MIMOPWRSAVE_MODE_S	1
#define	IEEE80211_A_HT_MIMOPWRSAVE_DYNAMIC	0x02	/* Dynamic Mode */
#define	IEEE80211_A_HT_MIMOPWRSAVE_STATIC	0x00	/* no SM packets */
/* bits 2-7 reserved */

/* Block Ack actions */
#define IEEE80211_ACTION_BA_ADDBA_REQUEST       0   /* ADDBA request */
#define IEEE80211_ACTION_BA_ADDBA_RESPONSE      1   /* ADDBA response */
#define IEEE80211_ACTION_BA_DELBA	        2   /* DELBA */

/* Block Ack Parameter Set */
#define	IEEE80211_BAPS_BUFSIZ	0xffc0		/* buffer size */
#define	IEEE80211_BAPS_BUFSIZ_S	6
#define	IEEE80211_BAPS_TID	0x003c		/* TID */
#define	IEEE80211_BAPS_TID_S	2
#define	IEEE80211_BAPS_POLICY	0x0002		/* block ack policy */
#define	IEEE80211_BAPS_POLICY_S	1

#define	IEEE80211_BAPS_POLICY_DELAYED	(0<<IEEE80211_BAPS_POLICY_S)
#define	IEEE80211_BAPS_POLICY_IMMEDIATE	(1<<IEEE80211_BAPS_POLICY_S)

/* Block Ack Sequence Control */
#define	IEEE80211_BASEQ_START	0xfff0		/* starting seqnum */
#define	IEEE80211_BASEQ_START_S	4
#define	IEEE80211_BASEQ_FRAG	0x000f		/* fragment number */
#define	IEEE80211_BASEQ_FRAG_S	0

/* Delayed Block Ack Parameter Set */
#define	IEEE80211_DELBAPS_TID	0xf000		/* TID */
#define	IEEE80211_DELBAPS_TID_S	12
#define	IEEE80211_DELBAPS_INIT	0x0800		/* initiator */
#define	IEEE80211_DELBAPS_INIT_S 11

/* BA - ADDBA request */
struct ieee80211_action_ba_addbarequest {
	struct ieee80211_action rq_header;
	uint8_t		rq_dialogtoken;
	uint16_t	rq_baparamset;
	uint16_t	rq_batimeout;		/* in TUs */
	uint16_t	rq_baseqctl;
} __packed;

/* BA - ADDBA response */
struct ieee80211_action_ba_addbaresponse {
	struct ieee80211_action rs_header;
	uint8_t		rs_dialogtoken;
	uint16_t	rs_statuscode;
	uint16_t	rs_baparamset; 
	uint16_t	rs_batimeout;		/* in TUs */
} __packed;

/* BA - DELBA */
struct ieee80211_action_ba_delba {
	struct ieee80211_action dl_header;
	uint16_t	dl_baparamset;
	uint16_t	dl_reasoncode;
} __packed;

/* BAR Control */
#define	IEEE80211_BAR_TID	0xf000		/* TID */
#define	IEEE80211_BAR_TID_S	12
#define	IEEE80211_BAR_COMP	0x0004		/* Compressed Bitmap */
#define	IEEE80211_BAR_MTID	0x0002		/* Multi-TID */
#define	IEEE80211_BAR_NOACK	0x0001		/* No-Ack policy */

/* BAR Starting Sequence Control */
#define	IEEE80211_BAR_SEQ_START	0xfff0		/* starting seqnum */
#define	IEEE80211_BAR_SEQ_START_S	4

struct ieee80211_ba_request {
	uint16_t	rq_barctl;
	uint16_t	rq_barseqctl;
} __packed;

/*
 * Control frames.
 */
struct ieee80211_frame_min {
	uint8_t		i_fc[2];
	uint8_t		i_dur[2];
	uint8_t		i_addr1[IEEE80211_ADDR_LEN];
	uint8_t		i_addr2[IEEE80211_ADDR_LEN];
	/* FCS */
} __packed;

struct ieee80211_frame_rts {
	uint8_t		i_fc[2];
	uint8_t		i_dur[2];
	uint8_t		i_ra[IEEE80211_ADDR_LEN];
	uint8_t		i_ta[IEEE80211_ADDR_LEN];
	/* FCS */
} __packed;

struct ieee80211_frame_cts {
	uint8_t		i_fc[2];
	uint8_t		i_dur[2];
	uint8_t		i_ra[IEEE80211_ADDR_LEN];
	/* FCS */
} __packed;

struct ieee80211_frame_ack {
	uint8_t		i_fc[2];
	uint8_t		i_dur[2];
	uint8_t		i_ra[IEEE80211_ADDR_LEN];
	/* FCS */
} __packed;

struct ieee80211_frame_pspoll {
	uint8_t		i_fc[2];
	uint8_t		i_aid[2];
	uint8_t		i_bssid[IEEE80211_ADDR_LEN];
	uint8_t		i_ta[IEEE80211_ADDR_LEN];
	/* FCS */
} __packed;

struct ieee80211_frame_cfend {		/* NB: also CF-End+CF-Ack */
	uint8_t		i_fc[2];
	uint8_t		i_dur[2];	/* should be zero */
	uint8_t		i_ra[IEEE80211_ADDR_LEN];
	uint8_t		i_bssid[IEEE80211_ADDR_LEN];
	/* FCS */
} __packed;

struct ieee80211_frame_bar {
	uint8_t		i_fc[2];
	uint8_t		i_dur[2];
	uint8_t		i_ra[IEEE80211_ADDR_LEN];
	uint8_t		i_ta[IEEE80211_ADDR_LEN];
	uint16_t	i_ctl;
	uint16_t	i_seq;
	/* FCS */
} __packed;

/*
 * BEACON management packets
 *
 *	octet timestamp[8]
 *	octet beacon interval[2]
 *	octet capability information[2]
 *	information element
 *		octet elemid
 *		octet length
 *		octet information[length]
 */

#define	IEEE80211_BEACON_INTERVAL(beacon) \
	((beacon)[8] | ((beacon)[9] << 8))
#define	IEEE80211_BEACON_CAPABILITY(beacon) \
	((beacon)[10] | ((beacon)[11] << 8))

#define	IEEE80211_CAPINFO_ESS			0x0001
#define	IEEE80211_CAPINFO_IBSS			0x0002
#define	IEEE80211_CAPINFO_CF_POLLABLE		0x0004
#define	IEEE80211_CAPINFO_CF_POLLREQ		0x0008
#define	IEEE80211_CAPINFO_PRIVACY		0x0010
#define	IEEE80211_CAPINFO_SHORT_PREAMBLE	0x0020
#define	IEEE80211_CAPINFO_PBCC			0x0040
#define	IEEE80211_CAPINFO_CHNL_AGILITY		0x0080
#define	IEEE80211_CAPINFO_SPECTRUM_MGMT		0x0100
/* bit 9 is reserved */
#define	IEEE80211_CAPINFO_SHORT_SLOTTIME	0x0400
#define	IEEE80211_CAPINFO_RSN			0x0800
/* bit 12 is reserved */
#define	IEEE80211_CAPINFO_DSSSOFDM		0x2000
/* bits 14-15 are reserved */

#define	IEEE80211_CAPINFO_BITS \
	"\20\1ESS\2IBSS\3CF_POLLABLE\4CF_POLLREQ\5PRIVACY\6SHORT_PREAMBLE" \
	"\7PBCC\10CHNL_AGILITY\11SPECTRUM_MGMT\13SHORT_SLOTTIME\14RSN" \
	"\16DSSOFDM"

/*
 * 802.11i/WPA information element (maximally sized).
 */
struct ieee80211_ie_wpa {
	uint8_t		wpa_id;		/* IEEE80211_ELEMID_VENDOR */
	uint8_t		wpa_len;	/* length in bytes */
	uint8_t		wpa_oui[3];	/* 0x00, 0x50, 0xf2 */
	uint8_t		wpa_type;	/* OUI type */
	uint16_t	wpa_version;	/* spec revision */
	uint32_t	wpa_mcipher[1];	/* multicast/group key cipher */
	uint16_t	wpa_uciphercnt;	/* # pairwise key ciphers */
	uint32_t	wpa_uciphers[8];/* ciphers */
	uint16_t	wpa_authselcnt;	/* authentication selector cnt*/
	uint32_t	wpa_authsels[8];/* selectors */
	uint16_t	wpa_caps;	/* 802.11i capabilities */
	uint16_t	wpa_pmkidcnt;	/* 802.11i pmkid count */
	uint16_t	wpa_pmkids[8];	/* 802.11i pmkids */
} __packed;

/*
 * 802.11n HT Capability IE
 * NB: these reflect D1.10 
 */
struct ieee80211_ie_htcap {
	uint8_t		hc_id;			/* element ID */
	uint8_t		hc_len;			/* length in bytes */
	uint16_t	hc_cap;			/* HT caps (see below) */
	uint8_t		hc_param;		/* HT params (see below) */
	uint8_t 	hc_mcsset[16]; 		/* supported MCS set */
	uint16_t	hc_extcap;		/* extended HT capabilities */
	uint32_t	hc_txbf;		/* txbf capabilities */
	uint8_t		hc_antenna;		/* antenna capabilities */
} __packed;

/* HT capability flags (ht_cap) */
#define	IEEE80211_HTCAP_LDPC		0x0001	/* LDPC rx supported */
#define	IEEE80211_HTCAP_CHWIDTH40	0x0002	/* 20/40 supported */
#define	IEEE80211_HTCAP_SMPS		0x000c	/* SM Power Save mode */
#define	IEEE80211_HTCAP_SMPS_OFF	0x000c	/* disabled */
#define	IEEE80211_HTCAP_SMPS_DYNAMIC	0x0004	/* send RTS first */
/* NB: SMPS value 2 is reserved */
#define	IEEE80211_HTCAP_SMPS_ENA	0x0000	/* enabled (static mode) */
#define	IEEE80211_HTCAP_GREENFIELD	0x0010	/* Greenfield supported */
#define	IEEE80211_HTCAP_SHORTGI20	0x0020	/* Short GI in 20MHz */
#define	IEEE80211_HTCAP_SHORTGI40	0x0040	/* Short GI in 40MHz */
#define	IEEE80211_HTCAP_TXSTBC		0x0080	/* STBC tx ok */
#define	IEEE80211_HTCAP_RXSTBC		0x0300  /* STBC rx support */
#define	IEEE80211_HTCAP_RXSTBC_S	8
#define	IEEE80211_HTCAP_RXSTBC_1STREAM	0x0100  /* 1 spatial stream */
#define	IEEE80211_HTCAP_RXSTBC_2STREAM	0x0200  /* 1-2 spatial streams*/
#define	IEEE80211_HTCAP_RXSTBC_3STREAM	0x0300  /* 1-3 spatial streams*/
#define	IEEE80211_HTCAP_DELBA		0x0400	/* HT DELBA supported */
#define	IEEE80211_HTCAP_MAXAMSDU	0x0800	/* max A-MSDU length */
#define	IEEE80211_HTCAP_MAXAMSDU_7935	0x0800	/* 7935 octets */
#define	IEEE80211_HTCAP_MAXAMSDU_3839	0x0000	/* 3839 octets */
#define	IEEE80211_HTCAP_DSSSCCK40	0x1000  /* DSSS/CCK in 40MHz */
#define	IEEE80211_HTCAP_PSMP		0x2000  /* PSMP supported */
#define	IEEE80211_HTCAP_40INTOLERANT	0x4000  /* 40MHz intolerant */
#define	IEEE80211_HTCAP_LSIGTXOPPROT	0x8000  /* L-SIG TXOP prot */

#define	IEEE80211_HTCAP_BITS \
	"\20\1LDPC\2CHWIDTH40\5GREENFIELD\6SHORTGI20\7SHORTGI40\10TXSTBC" \
	"\13DELBA\14AMSDU(7935)\15DSSSCCK40\16PSMP\1740INTOLERANT" \
	"\20LSIGTXOPPROT"

/* HT parameters (hc_param) */
#define	IEEE80211_HTCAP_MAXRXAMPDU	0x03	/* max rx A-MPDU factor */
#define	IEEE80211_HTCAP_MAXRXAMPDU_S	0
#define	IEEE80211_HTCAP_MAXRXAMPDU_8K	0
#define	IEEE80211_HTCAP_MAXRXAMPDU_16K	1
#define	IEEE80211_HTCAP_MAXRXAMPDU_32K	2
#define	IEEE80211_HTCAP_MAXRXAMPDU_64K	3
#define	IEEE80211_HTCAP_MPDUDENSITY	0x1c	/* min MPDU start spacing */
#define	IEEE80211_HTCAP_MPDUDENSITY_S	2
#define	IEEE80211_HTCAP_MPDUDENSITY_NA	0	/* no time restriction */
#define	IEEE80211_HTCAP_MPDUDENSITY_025	1	/* 1/4 us */
#define	IEEE80211_HTCAP_MPDUDENSITY_05	2	/* 1/2 us */
#define	IEEE80211_HTCAP_MPDUDENSITY_1	3	/* 1 us */
#define	IEEE80211_HTCAP_MPDUDENSITY_2	4	/* 2 us */
#define	IEEE80211_HTCAP_MPDUDENSITY_4	5	/* 4 us */
#define	IEEE80211_HTCAP_MPDUDENSITY_8	6	/* 8 us */
#define	IEEE80211_HTCAP_MPDUDENSITY_16	7	/* 16 us */

/* HT extended capabilities (hc_extcap) */
#define	IEEE80211_HTCAP_PCO		0x0001	/* PCO capable */
#define	IEEE80211_HTCAP_PCOTRANS	0x0006	/* PCO transition time */
#define	IEEE80211_HTCAP_PCOTRANS_S	1
#define	IEEE80211_HTCAP_PCOTRANS_04	0x0002	/* 400 us */
#define	IEEE80211_HTCAP_PCOTRANS_15	0x0004	/* 1.5 ms */
#define	IEEE80211_HTCAP_PCOTRANS_5	0x0006	/* 5 ms */
/* bits 3-7 reserved */
#define	IEEE80211_HTCAP_MCSFBACK	0x0300	/* MCS feedback */
#define	IEEE80211_HTCAP_MCSFBACK_S	8
#define	IEEE80211_HTCAP_MCSFBACK_NONE	0x0000	/* nothing provided */
#define	IEEE80211_HTCAP_MCSFBACK_UNSOL	0x0200	/* unsolicited feedback */
#define	IEEE80211_HTCAP_MCSFBACK_MRQ	0x0300	/* " "+respond to MRQ */
#define	IEEE80211_HTCAP_HTC		0x0400	/* +HTC support */
#define	IEEE80211_HTCAP_RDR		0x0800	/* reverse direction responder*/
/* bits 12-15 reserved */

/*
 * 802.11n HT Information IE
 */
struct ieee80211_ie_htinfo {
	uint8_t		hi_id;			/* element ID */
	uint8_t		hi_len;			/* length in bytes */
	uint8_t		hi_ctrlchannel;		/* primary channel */
	uint8_t		hi_byte1;		/* ht ie byte 1 */
	uint8_t		hi_byte2;		/* ht ie byte 2 */
	uint8_t		hi_byte3;		/* ht ie byte 3 */
	uint16_t	hi_byte45;		/* ht ie bytes 4+5 */
	uint8_t 	hi_basicmcsset[16]; 	/* basic MCS set */
} __packed;

/* byte1 */
#define	IEEE80211_HTINFO_2NDCHAN	0x03	/* secondary/ext chan offset */
#define	IEEE80211_HTINFO_2NDCHAN_S	0
#define	IEEE80211_HTINFO_2NDCHAN_NONE	0x00	/* no secondary/ext channel */
#define	IEEE80211_HTINFO_2NDCHAN_ABOVE	0x01	/* above private channel */
/* NB: 2 is reserved */
#define	IEEE80211_HTINFO_2NDCHAN_BELOW	0x03	/* below primary channel */ 
#define	IEEE80211_HTINFO_TXWIDTH	0x04	/* tx channel width */
#define	IEEE80211_HTINFO_TXWIDTH_20	0x00	/* 20MHz width */
#define	IEEE80211_HTINFO_TXWIDTH_2040	0x04	/* any supported width */
#define	IEEE80211_HTINFO_RIFSMODE	0x08	/* Reduced IFS (RIFS) use */
#define	IEEE80211_HTINFO_RIFSMODE_PROH	0x00	/* RIFS use prohibited */
#define	IEEE80211_HTINFO_RIFSMODE_PERM	0x08	/* RIFS use permitted */
#define	IEEE80211_HTINFO_PMSPONLY	0x10	/* PSMP required to associate */
#define	IEEE80211_HTINFO_SIGRAN		0xe0	/* shortest Service Interval */
#define	IEEE80211_HTINFO_SIGRAN_S	5
#define	IEEE80211_HTINFO_SIGRAN_5	0x00	/* 5 ms */
/* XXX add rest */

/* bytes 2+3 */
#define	IEEE80211_HTINFO_OPMODE		0x03	/* operating mode */
#define	IEEE80211_HTINFO_OPMODE_S	0
#define	IEEE80211_HTINFO_OPMODE_PURE	0x00	/* no protection */
#define	IEEE80211_HTINFO_OPMODE_PROTOPT	0x01	/* protection optional */
#define	IEEE80211_HTINFO_OPMODE_HT20PR	0x02	/* protection for HT20 sta's */
#define	IEEE80211_HTINFO_OPMODE_MIXED	0x03	/* protection for legacy sta's*/
#define	IEEE80211_HTINFO_NONGF_PRESENT	0x04	/* non-GF sta's present */
#define	IEEE80211_HTINFO_TXBL		0x08	/* transmit burst limit */
#define	IEEE80211_HTINFO_NONHT_PRESENT	0x10	/* non-HT sta's present */
/* bits 5-15 reserved */

/* bytes 4+5 */
#define	IEEE80211_HTINFO_2NDARYBEACON	0x01
#define	IEEE80211_HTINFO_LSIGTXOPPROT	0x02
#define	IEEE80211_HTINFO_PCO_ACTIVE	0x04
#define	IEEE80211_HTINFO_40MHZPHASE	0x08

/* byte5 */
#define	IEEE80211_HTINFO_BASIC_STBCMCS	0x7f
#define	IEEE80211_HTINFO_BASIC_STBCMCS_S 0
#define	IEEE80211_HTINFO_DUALPROTECTED	0x80


/*
 * 802.11ac definitions - 802.11ac-2013 .
 */

/*
 * Maximum length of A-MPDU that the STA can RX in VHT.
 * Length = 2 ^ (13 + max_ampdu_length_exp) - 1 (octets)
 */
#define	IEEE80211_VHTCAP_MAX_AMPDU_8K		0
#define	IEEE80211_VHTCAP_MAX_AMPDU_16K		1
#define	IEEE80211_VHTCAP_MAX_AMPDU_32K		2
#define	IEEE80211_VHTCAP_MAX_AMPDU_64K		3
#define	IEEE80211_VHTCAP_MAX_AMPDU_128K		4
#define	IEEE80211_VHTCAP_MAX_AMPDU_256K		5
#define	IEEE80211_VHTCAP_MAX_AMPDU_512K		6
#define	IEEE80211_VHTCAP_MAX_AMPDU_1024K	7

/*
 * VHT MCS information.
 * + rx_highest/tx_highest: optional; maximum long GI VHT PPDU
 *    data rate.  1Mbit/sec units.
 * + rx_mcs_map/tx_mcs_map: bitmap of per-stream supported MCS;
 *    2 bits each.
 */
#define	IEEE80211_VHT_MCS_SUPPORT_0_7		0	/* MCS0-7 */
#define	IEEE80211_VHT_MCS_SUPPORT_0_8		1	/* MCS0-8 */
#define	IEEE80211_VHT_MCS_SUPPORT_0_9		2	/* MCS0-9 */
#define	IEEE80211_VHT_MCS_NOT_SUPPORTED		3	/* not supported */

struct ieee80211_vht_mcs_info {
	uint16_t rx_mcs_map;
	uint16_t rx_highest;
	uint16_t tx_mcs_map;
	uint16_t tx_highest;
} __packed;

/* VHT capabilities element: 802.11ac-2013 8.4.2.160 */
struct ieee80211_ie_vhtcap {
	uint8_t ie;
	uint8_t len;
	uint32_t vht_cap_info;
	struct ieee80211_vht_mcs_info supp_mcs;
} __packed;

/* VHT operation mode subfields - 802.11ac-2013 Table 8.183x */
#define	IEEE80211_VHT_CHANWIDTH_USE_HT		0	/* Use HT IE for chw */
#define	IEEE80211_VHT_CHANWIDTH_80MHZ		1	/* 80MHz */
#define	IEEE80211_VHT_CHANWIDTH_160MHZ		2	/* 160MHz */
#define	IEEE80211_VHT_CHANWIDTH_80P80MHZ	3	/* 80+80MHz */

/* VHT operation IE - 802.11ac-2013 8.4.2.161 */
struct ieee80211_ie_vht_operation {
	uint8_t ie;
	uint8_t len;
	uint8_t chan_width;
	uint8_t center_freq_seg1_idx;
	uint8_t center_freq_seg2_idx;
	uint16_t basic_mcs_set;
} __packed;

/* 802.11ac VHT Capabilities */
#define	IEEE80211_VHTCAP_MAX_MPDU_LENGTH_3895	0x00000000
#define	IEEE80211_VHTCAP_MAX_MPDU_LENGTH_7991	0x00000001
#define	IEEE80211_VHTCAP_MAX_MPDU_LENGTH_11454	0x00000002
#define	IEEE80211_VHTCAP_MAX_MPDU_MASK		0x00000003
#define	IEEE80211_VHTCAP_MAX_MPDU_MASK_S	0

#define	IEEE80211_VHTCAP_SUPP_CHAN_WIDTH_MASK	0x0000000C
#define	IEEE80211_VHTCAP_SUPP_CHAN_WIDTH_MASK_S	2
#define	IEEE80211_VHTCAP_SUPP_CHAN_WIDTH_NONE		0
#define	IEEE80211_VHTCAP_SUPP_CHAN_WIDTH_160MHZ		1
#define	IEEE80211_VHTCAP_SUPP_CHAN_WIDTH_160_80P80MHZ	2
#define	IEEE80211_VHTCAP_SUPP_CHAN_WIDTH_RESERVED	3

#define	IEEE80211_VHTCAP_RXLDPC		0x00000010
#define	IEEE80211_VHTCAP_RXLDPC_S	4

#define	IEEE80211_VHTCAP_SHORT_GI_80		0x00000020
#define	IEEE80211_VHTCAP_SHORT_GI_80_S		5

#define	IEEE80211_VHTCAP_SHORT_GI_160		0x00000040
#define	IEEE80211_VHTCAP_SHORT_GI_160_S		6

#define	IEEE80211_VHTCAP_TXSTBC		0x00000080
#define	IEEE80211_VHTCAP_TXSTBC_S	7

#define	IEEE80211_VHTCAP_RXSTBC_1		0x00000100
#define	IEEE80211_VHTCAP_RXSTBC_2		0x00000200
#define	IEEE80211_VHTCAP_RXSTBC_3		0x00000300
#define	IEEE80211_VHTCAP_RXSTBC_4		0x00000400
#define	IEEE80211_VHTCAP_RXSTBC_MASK		0x00000700
#define	IEEE80211_VHTCAP_RXSTBC_MASK_S		8

#define	IEEE80211_VHTCAP_SU_BEAMFORMER_CAPABLE	0x00000800
#define	IEEE80211_VHTCAP_SU_BEAMFORMER_CAPABLE_S	11

#define	IEEE80211_VHTCAP_SU_BEAMFORMEE_CAPABLE	0x00001000
#define	IEEE80211_VHTCAP_SU_BEAMFORMEE_CAPABLE_S	12

#define	IEEE80211_VHTCAP_BEAMFORMEE_STS_SHIFT	13
#define	IEEE80211_VHTCAP_BEAMFORMEE_STS_MASK \
	    (7 << IEEE80211_VHTCAP_BEAMFORMEE_STS_SHIFT)
#define	IEEE80211_VHTCAP_BEAMFORMEE_STS_MASK_S	13

#define	IEEE80211_VHTCAP_SOUNDING_DIMENSIONS_SHIFT	16
#define	IEEE80211_VHTCAP_SOUNDING_DIMENSIONS_MASK \
	    (7 << IEEE80211_VHTCAP_SOUNDING_DIMENSIONS_SHIFT)
#define	IEEE80211_VHTCAP_SOUNDING_DIMENSIONS_MASK_S	16

#define	IEEE80211_VHTCAP_MU_BEAMFORMER_CAPABLE	0x00080000
#define	IEEE80211_VHTCAP_MU_BEAMFORMER_CAPABLE_S	19
#define	IEEE80211_VHTCAP_MU_BEAMFORMEE_CAPABLE	0x00100000
#define	IEEE80211_VHTCAP_MU_BEAMFORMEE_CAPABLE_S	20
#define	IEEE80211_VHTCAP_VHT_TXOP_PS		0x00200000
#define	IEEE80211_VHTCAP_VHT_TXOP_PS_S		21
#define	IEEE80211_VHTCAP_HTC_VHT		0x00400000
#define	IEEE80211_VHTCAP_HTC_VHT_S		22

#define	IEEE80211_VHTCAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT	23
#define	IEEE80211_VHTCAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK \
	    (7 << IEEE80211_VHTCAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT)
#define	IEEE80211_VHTCAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK_S	23

#define	IEEE80211_VHTCAP_VHT_LINK_ADAPTATION_VHT_MASK	0x0c000000
#define	IEEE80211_VHTCAP_VHT_LINK_ADAPTATION_VHT_UNSOL_MFB	0x08000000
#define	IEEE80211_VHTCAP_VHT_LINK_ADAPTATION_VHT_MRQ_MFB	0x0c000000
#define	IEEE80211_VHTCAP_VHT_LINK_ADAPTATION_VHT_MASK_S	26

#define	IEEE80211_VHTCAP_RX_ANTENNA_PATTERN	0x10000000
#define	IEEE80211_VHTCAP_RX_ANTENNA_PATTERN_S	28
#define	IEEE80211_VHTCAP_TX_ANTENNA_PATTERN	0x20000000
#define	IEEE80211_VHTCAP_TX_ANTENNA_PATTERN_S	29

/*
 * XXX TODO: add the rest of the bits
 */
#define	IEEE80211_VHTCAP_BITS \
	"\20\1MPDU7991\2MPDU11454\3CHAN160\4CHAN8080\5RXLDPC\6SHORTGI80" \
	"\7SHORTGI160\10RXSTBC1\11RXSTBC2\12RXSTBC3\13RXSTBC4\14BFERCAP" \
	"\15BFEECAP\27VHT\37RXANTPTN\40TXANTPTN"

/*
 * VHT Transmit Power Envelope element - 802.11ac-2013 8.4.2.164
 *
 * This defines the maximum transmit power for various bandwidths.
 */
/*
 * Count is how many elements follow and what they're for:
 *
 * 0 - 20 MHz
 * 1 - 20+40 MHz
 * 2 - 20+40+80 MHz
 * 3 - 20+40+80+(160, 80+80) MHz
 */
#define	IEEE80211_VHT_TXPWRENV_INFO_COUNT_SHIFT	0
#define	IEEE80211_VHT_TXPWRENV_INFO_COUNT_MASK	0x07

/*
 * Unit is the tx power representation.  It should be EIRP for now;
 * other values are reserved.
 */
#define	IEEE80211_VHT_TXPWRENV_UNIT_MASK	0x38
#define	IEEE80211_VHT_TXPWRENV_UNIT_SHIFT	3

/* This value is within the unit mask/shift above */
#define	IEEE80211_VHT_TXPWRENV_UNIT_EIRP	0

struct ieee80211_ie_vht_txpwrenv {
	uint8_t ie;
	uint8_t len;
	uint8_t tx_info;
	int8_t tx_elem[0];	/* TX power elements, 1/2 dB, signed */
};

/* VHT action codes */
#define	WLAN_ACTION_VHT_COMPRESSED_BF		0
#define	WLAN_ACTION_VHT_GROUPID_MGMT		1
#define	WLAN_ACTION_VHT_OPMODE_NOTIF		2

/*
 * Management information element payloads.
 */

enum {
	IEEE80211_ELEMID_SSID		= 0,
	IEEE80211_ELEMID_RATES		= 1,
	IEEE80211_ELEMID_FHPARMS	= 2,
	IEEE80211_ELEMID_DSPARMS	= 3,
	IEEE80211_ELEMID_CFPARMS	= 4,
	IEEE80211_ELEMID_TIM		= 5,
	IEEE80211_ELEMID_IBSSPARMS	= 6,
	IEEE80211_ELEMID_COUNTRY	= 7,
	IEEE80211_ELEMID_BSSLOAD	= 11,
	IEEE80211_ELEMID_TSPEC		= 13,
	IEEE80211_ELEMID_TCLAS		= 14,
	IEEE80211_ELEMID_CHALLENGE	= 16,
	/* 17-31 reserved for challenge text extension */
	IEEE80211_ELEMID_PWRCNSTR	= 32,
	IEEE80211_ELEMID_PWRCAP		= 33,
	IEEE80211_ELEMID_TPCREQ		= 34,
	IEEE80211_ELEMID_TPCREP		= 35,
	IEEE80211_ELEMID_SUPPCHAN	= 36,
	IEEE80211_ELEMID_CSA		= 37,
	IEEE80211_ELEMID_MEASREQ	= 38,
	IEEE80211_ELEMID_MEASREP	= 39,
	IEEE80211_ELEMID_QUIET		= 40,
	IEEE80211_ELEMID_IBSSDFS	= 41,
	IEEE80211_ELEMID_ERP		= 42,
	IEEE80211_ELEMID_HTCAP		= 45,
	IEEE80211_ELEMID_QOS		= 46,
	IEEE80211_ELEMID_RESERVED_47	= 47,
	IEEE80211_ELEMID_RSN		= 48,
	IEEE80211_ELEMID_XRATES		= 50,
	IEEE80211_ELEMID_APCHANREP	= 51,
	IEEE80211_ELEMID_MOBILITY_DOMAIN	= 54,
	IEEE80211_ELEMID_HTINFO		= 61,
	IEEE80211_ELEMID_SECCHAN_OFFSET	= 62,
	IEEE80211_ELEMID_RRM_ENACAPS	= 70,
	IEEE80211_ELEMID_MULTIBSSID	= 71,
	IEEE80211_ELEMID_COEX_2040	= 72,
	IEEE80211_ELEMID_INTOL_CHN_REPORT	= 73,
	IEEE80211_ELEMID_OVERLAP_BSS_SCAN_PARAM = 74,
	IEEE80211_ELEMID_TSF_REQ	= 91,
	IEEE80211_ELEMID_TSF_RESP	= 92,
	IEEE80211_ELEMID_WNM_SLEEP_MODE	= 93,
	IEEE80211_ELEMID_TIM_BCAST_REQ	= 94,
	IEEE80211_ELEMID_TIM_BCAST_RESP	= 95,
	IEEE80211_ELEMID_TPC		= 150,
	IEEE80211_ELEMID_CCKM		= 156,
	IEEE80211_ELEMID_VENDOR		= 221,	/* vendor private */

	/*
	 * 802.11s IEs
	 * NB: On vanilla Linux still IEEE80211_ELEMID_MESHPEER = 55,
	 * but they defined a new with id 117 called PEER_MGMT.
	 * NB: complies with open80211
	 */
	IEEE80211_ELEMID_MESHCONF	= 113,
	IEEE80211_ELEMID_MESHID		= 114,
	IEEE80211_ELEMID_MESHLINK	= 115,
	IEEE80211_ELEMID_MESHCNGST	= 116,
	IEEE80211_ELEMID_MESHPEER	= 117,
	IEEE80211_ELEMID_MESHCSA	= 118,
	IEEE80211_ELEMID_MESHTIM	= 39, /* XXX: remove */
	IEEE80211_ELEMID_MESHAWAKEW	= 119,
	IEEE80211_ELEMID_MESHBEACONT	= 120,
	/* 121-124 MMCAOP not implemented yet */
	IEEE80211_ELEMID_MESHGANN	= 125,
	IEEE80211_ELEMID_MESHRANN	= 126,
	/* 127 Extended Capabilities */
	IEEE80211_ELEMID_EXTCAP		= 127,
	/* 128-129 reserved */
	IEEE80211_ELEMID_MESHPREQ	= 130,
	IEEE80211_ELEMID_MESHPREP	= 131,
	IEEE80211_ELEMID_MESHPERR	= 132,
	/* 133-136 reserved */
	IEEE80211_ELEMID_MESHPXU	= 137,
	IEEE80211_ELEMID_MESHPXUC	= 138,
	IEEE80211_ELEMID_MESHAH		= 60, /* XXX: remove */

	/* 802.11ac */
	IEEE80211_ELEMID_VHT_CAP	= 191,
	IEEE80211_ELEMID_VHT_OPMODE	= 192,
	IEEE80211_ELEMID_VHT_PWR_ENV	= 195,
};

struct ieee80211_tim_ie {
	uint8_t		tim_ie;			/* IEEE80211_ELEMID_TIM */
	uint8_t		tim_len;
	uint8_t		tim_count;		/* DTIM count */
	uint8_t		tim_period;		/* DTIM period */
	uint8_t		tim_bitctl;		/* bitmap control */
	uint8_t		tim_bitmap[1];		/* variable-length bitmap */
} __packed;

struct ieee80211_country_ie {
	uint8_t		ie;			/* IEEE80211_ELEMID_COUNTRY */
	uint8_t		len;
	uint8_t		cc[3];			/* ISO CC+(I)ndoor/(O)utdoor */
	struct {
		uint8_t schan;			/* starting channel */
		uint8_t nchan;			/* number channels */
		uint8_t maxtxpwr;		/* tx power cap */
	} __packed band[1];			/* sub bands (NB: var size) */
} __packed;

#define	IEEE80211_COUNTRY_MAX_BANDS	84	/* max possible bands */
#define	IEEE80211_COUNTRY_MAX_SIZE \
	(sizeof(struct ieee80211_country_ie) + 3*(IEEE80211_COUNTRY_MAX_BANDS-1))

struct ieee80211_bss_load_ie {
	uint8_t		ie;
	uint8_t		len;
	uint16_t	sta_count;	/* station count */
	uint8_t		chan_load;	/* channel utilization */
	uint8_t		aac;		/* available admission capacity */
} __packed;

struct ieee80211_ap_chan_report_ie {
	uint8_t		ie;
	uint8_t		len;
	uint8_t		i_class; /* operating class */
	/* Annex E, E.1 Country information and operating classes */
	uint8_t		chan_list[0];
} __packed;

#define IEEE80211_EXTCAP_CMS			(1ULL <<  0) /* 20/40 BSS coexistence management support */
#define IEEE80211_EXTCAP_RSVD_1			(1ULL <<  1)
#define IEEE80211_EXTCAP_ECS			(1ULL <<  2) /* extended channel switching */
#define IEEE80211_EXTCAP_RSVD_3			(1ULL <<  3)
#define IEEE80211_EXTCAP_PSMP_CAP		(1ULL <<  4) /* PSMP capability */
#define IEEE80211_EXTCAP_RSVD_5			(1ULL <<  5)
#define IEEE80211_EXTCAP_S_PSMP_SUPP		(1ULL <<  6)
#define IEEE80211_EXTCAP_EVENT			(1ULL <<  7)
#define IEEE80211_EXTCAP_DIAGNOSTICS		(1ULL <<  8)
#define IEEE80211_EXTCAP_MCAST_DIAG		(1ULL <<  9)
#define IEEE80211_EXTCAP_LOC_TRACKING		(1ULL << 10)
#define IEEE80211_EXTCAP_FMS			(1ULL << 11)
#define IEEE80211_EXTCAP_PROXY_ARP		(1ULL << 12)
#define IEEE80211_EXTCAP_CIR			(1ULL << 13) /* collocated interference reporting */
#define IEEE80211_EXTCAP_CIVIC_LOC		(1ULL << 14)
#define IEEE80211_EXTCAP_GEOSPATIAL_LOC		(1ULL << 15)
#define IEEE80211_EXTCAP_TFS			(1ULL << 16)
#define IEEE80211_EXTCAP_WNM_SLEEPMODE		(1ULL << 17)
#define IEEE80211_EXTCAP_TIM_BROADCAST		(1ULL << 18)
#define IEEE80211_EXTCAP_BSS_TRANSITION		(1ULL << 19)
#define IEEE80211_EXTCAP_QOS_TRAF_CAP		(1ULL << 20)
#define IEEE80211_EXTCAP_AC_STA_COUNT		(1ULL << 21)
#define IEEE80211_EXTCAP_M_BSSID		(1ULL << 22) /* multiple BSSID field */
#define IEEE80211_EXTCAP_TIMING_MEAS		(1ULL << 23)
#define IEEE80211_EXTCAP_CHAN_USAGE		(1ULL << 24)
#define IEEE80211_EXTCAP_SSID_LIST		(1ULL << 25)
#define IEEE80211_EXTCAP_DMS			(1ULL << 26)
#define IEEE80211_EXTCAP_UTC_TSF_OFFSET		(1ULL << 27)
#define IEEE80211_EXTCAP_TLDS_BUF_STA_SUPP	(1ULL << 28) /* TDLS peer U-APSP buffer STA support */
#define IEEE80211_EXTCAP_TLDS_PPSM_SUPP		(1ULL << 29) /* TDLS peer PSM support */
#define IEEE80211_EXTCAP_TLDS_CH_SW		(1ULL << 30) /* TDLS channel switching */
#define IEEE80211_EXTCAP_INTERWORKING		(1ULL << 31)
#define IEEE80211_EXTCAP_QOSMAP			(1ULL << 32)
#define IEEE80211_EXTCAP_EBR			(1ULL << 33)
#define IEEE80211_EXTCAP_SSPN_IF		(1ULL << 34)
#define IEEE80211_EXTCAP_RSVD_35		(1ULL << 35)
#define IEEE80211_EXTCAP_MSGCF_CAP		(1ULL << 36)
#define IEEE80211_EXTCAP_TLDS_SUPP		(1ULL << 37)
#define IEEE80211_EXTCAP_TLDS_PROHIB		(1ULL << 38)
#define IEEE80211_EXTCAP_TLDS_CH_SW_PROHIB	(1ULL << 39) /* TDLS channel switching prohibited */
#define IEEE80211_EXTCAP_RUF			(1ULL << 40) /* reject unadmitted frame */
/* service interval granularity */
#define IEEE80211_EXTCAP_SIG \
				((1ULL << 41) | (1ULL << 42) | (1ULL << 43))
#define IEEE80211_EXTCAP_ID_LOC			(1ULL << 44)
#define IEEE80211_EXTCAP_U_APSD_COEX		(1ULL << 45)
#define IEEE80211_EXTCAP_WNM_NOTIFICATION	(1ULL << 46)
#define IEEE80211_EXTCAP_RSVD_47		(1ULL << 47)
#define IEEE80211_EXTCAP_SSID			(1ULL << 48) /* UTF-8 SSID */
/* bits 49-n are reserved */

struct ieee80211_extcap_ie {
	uint8_t		ie;
	uint8_t		len;
} __packed;

/*
 * 802.11h Quiet Time Element.
 */
struct ieee80211_quiet_ie {
	uint8_t		quiet_ie;		/* IEEE80211_ELEMID_QUIET */
	uint8_t		len;
	uint8_t		tbttcount;		/* quiet start */
	uint8_t		period;			/* beacon intervals between quiets */
	uint16_t	duration;		/* TUs of each quiet*/
	uint16_t	offset;			/* TUs of from TBTT of quiet start */
} __packed;

/*
 * 802.11h Channel Switch Announcement (CSA).
 */
struct ieee80211_csa_ie {
	uint8_t		csa_ie;		/* IEEE80211_ELEMID_CHANSWITCHANN */
	uint8_t		csa_len;
	uint8_t		csa_mode;		/* Channel Switch Mode */
	uint8_t		csa_newchan;		/* New Channel Number */
	uint8_t		csa_count;		/* Channel Switch Count */
} __packed;

/*
 * Note the min acceptable CSA count is used to guard against
 * malicious CSA injection in station mode.  Defining this value
 * as other than 0 violates the 11h spec.
 */
#define	IEEE80211_CSA_COUNT_MIN	2
#define	IEEE80211_CSA_COUNT_MAX	255

/* rate set entries are in .5 Mb/s units, and potentially marked as basic */
#define	IEEE80211_RATE_BASIC		0x80
#define	IEEE80211_RATE_VAL		0x7f
#define	IEEE80211_RV(v)			((v) & IEEE80211_RATE_VAL)

/* ERP information element flags */
#define	IEEE80211_ERP_NON_ERP_PRESENT	0x01
#define	IEEE80211_ERP_USE_PROTECTION	0x02
#define	IEEE80211_ERP_LONG_PREAMBLE	0x04

#define	IEEE80211_ERP_BITS \
	"\20\1NON_ERP_PRESENT\2USE_PROTECTION\3LONG_PREAMBLE"

#define	ATH_OUI			0x7f0300	/* Atheros OUI */
#define	ATH_OUI_TYPE		0x01		/* Atheros protocol ie */

/* NB: Atheros allocated the OUI for this purpose ~2005 but beware ... */
#define	TDMA_OUI		ATH_OUI
#define	TDMA_OUI_TYPE		0x02		/* TDMA protocol ie */

#define	BCM_OUI			0x4c9000	/* Broadcom OUI */
#define	BCM_OUI_HTCAP		51		/* pre-draft HTCAP ie */
#define	BCM_OUI_HTINFO		52		/* pre-draft HTINFO ie */

#define	WPA_OUI			0xf25000
#define	WPA_OUI_TYPE		0x01
#define	WPA_VERSION		1		/* current supported version */

#define	WPA_CSE_NULL		0x00
#define	WPA_CSE_WEP40		0x01
#define	WPA_CSE_TKIP		0x02
#define	WPA_CSE_CCMP		0x04
#define	WPA_CSE_WEP104		0x05

#define	WPA_ASE_NONE		0x00
#define	WPA_ASE_8021X_UNSPEC	0x01
#define	WPA_ASE_8021X_PSK	0x02

#define	WPS_OUI_TYPE		0x04

#define	RSN_OUI			0xac0f00
#define	RSN_VERSION		1		/* current supported version */

#define	RSN_CSE_NULL		0x00
#define	RSN_CSE_WEP40		0x01
#define	RSN_CSE_TKIP		0x02
#define	RSN_CSE_WRAP		0x03
#define	RSN_CSE_CCMP		0x04
#define	RSN_CSE_WEP104		0x05

#define	RSN_ASE_NONE		0x00
#define	RSN_ASE_8021X_UNSPEC	0x01
#define	RSN_ASE_8021X_PSK	0x02

#define	RSN_CAP_PREAUTH		0x01

#define	WME_OUI			0xf25000
#define	WME_OUI_TYPE		0x02
#define	WME_INFO_OUI_SUBTYPE	0x00
#define	WME_PARAM_OUI_SUBTYPE	0x01
#define	WME_VERSION		1

/* WME stream classes */
#define	WME_AC_BE	0		/* best effort */
#define	WME_AC_BK	1		/* background */
#define	WME_AC_VI	2		/* video */
#define	WME_AC_VO	3		/* voice */

/*
 * AUTH management packets
 *
 *	octet algo[2]
 *	octet seq[2]
 *	octet status[2]
 *	octet chal.id
 *	octet chal.length
 *	octet chal.text[253]		NB: 1-253 bytes
 */

/* challenge length for shared key auth */
#define IEEE80211_CHALLENGE_LEN		128

#define	IEEE80211_AUTH_ALG_OPEN		0x0000
#define	IEEE80211_AUTH_ALG_SHARED	0x0001
#define	IEEE80211_AUTH_ALG_LEAP		0x0080

enum {
	IEEE80211_AUTH_OPEN_REQUEST		= 1,
	IEEE80211_AUTH_OPEN_RESPONSE		= 2,
};

enum {
	IEEE80211_AUTH_SHARED_REQUEST		= 1,
	IEEE80211_AUTH_SHARED_CHALLENGE		= 2,
	IEEE80211_AUTH_SHARED_RESPONSE		= 3,
	IEEE80211_AUTH_SHARED_PASS		= 4,
};

/*
 * Reason and status codes.
 *
 * Reason codes are used in management frames to indicate why an
 * action took place (e.g. on disassociation).  Status codes are
 * used in management frames to indicate the result of an operation.
 *
 * Unlisted codes are reserved
 */

enum {
	IEEE80211_REASON_UNSPECIFIED		= 1,
	IEEE80211_REASON_AUTH_EXPIRE		= 2,
	IEEE80211_REASON_AUTH_LEAVE		= 3,
	IEEE80211_REASON_ASSOC_EXPIRE		= 4,
	IEEE80211_REASON_ASSOC_TOOMANY		= 5,
	IEEE80211_REASON_NOT_AUTHED		= 6,
	IEEE80211_REASON_NOT_ASSOCED		= 7,
	IEEE80211_REASON_ASSOC_LEAVE		= 8,
	IEEE80211_REASON_ASSOC_NOT_AUTHED	= 9,
	IEEE80211_REASON_DISASSOC_PWRCAP_BAD	= 10,	/* 11h */
	IEEE80211_REASON_DISASSOC_SUPCHAN_BAD	= 11,	/* 11h */
	IEEE80211_REASON_IE_INVALID		= 13,	/* 11i */
	IEEE80211_REASON_MIC_FAILURE		= 14,	/* 11i */
	IEEE80211_REASON_4WAY_HANDSHAKE_TIMEOUT	= 15,	/* 11i */
	IEEE80211_REASON_GROUP_KEY_UPDATE_TIMEOUT = 16,	/* 11i */
	IEEE80211_REASON_IE_IN_4WAY_DIFFERS	= 17,	/* 11i */
	IEEE80211_REASON_GROUP_CIPHER_INVALID	= 18,	/* 11i */
	IEEE80211_REASON_PAIRWISE_CIPHER_INVALID= 19,	/* 11i */
	IEEE80211_REASON_AKMP_INVALID		= 20,	/* 11i */
	IEEE80211_REASON_UNSUPP_RSN_IE_VERSION	= 21,	/* 11i */
	IEEE80211_REASON_INVALID_RSN_IE_CAP	= 22,	/* 11i */
	IEEE80211_REASON_802_1X_AUTH_FAILED	= 23,	/* 11i */
	IEEE80211_REASON_CIPHER_SUITE_REJECTED	= 24,	/* 11i */
	IEEE80211_REASON_UNSPECIFIED_QOS	= 32,	/* 11e */
	IEEE80211_REASON_INSUFFICIENT_BW	= 33,	/* 11e */
	IEEE80211_REASON_TOOMANY_FRAMES		= 34,	/* 11e */
	IEEE80211_REASON_OUTSIDE_TXOP		= 35,	/* 11e */
	IEEE80211_REASON_LEAVING_QBSS		= 36,	/* 11e */
	IEEE80211_REASON_BAD_MECHANISM		= 37,	/* 11e */
	IEEE80211_REASON_SETUP_NEEDED		= 38,	/* 11e */
	IEEE80211_REASON_TIMEOUT		= 39,	/* 11e */

	IEEE80211_REASON_PEER_LINK_CANCELED	= 52,	/* 11s */
	IEEE80211_REASON_MESH_MAX_PEERS		= 53,	/* 11s */
	IEEE80211_REASON_MESH_CPVIOLATION	= 54,	/* 11s */
	IEEE80211_REASON_MESH_CLOSE_RCVD	= 55,	/* 11s */
	IEEE80211_REASON_MESH_MAX_RETRIES	= 56,	/* 11s */
	IEEE80211_REASON_MESH_CONFIRM_TIMEOUT	= 57,	/* 11s */
	IEEE80211_REASON_MESH_INVALID_GTK	= 58,	/* 11s */
	IEEE80211_REASON_MESH_INCONS_PARAMS	= 59,	/* 11s */
	IEEE80211_REASON_MESH_INVALID_SECURITY	= 60,	/* 11s */
	IEEE80211_REASON_MESH_PERR_NO_PROXY	= 61,	/* 11s */
	IEEE80211_REASON_MESH_PERR_NO_FI	= 62,	/* 11s */
	IEEE80211_REASON_MESH_PERR_DEST_UNREACH	= 63,	/* 11s */
	IEEE80211_REASON_MESH_MAC_ALRDY_EXISTS_MBSS = 64, /* 11s */
	IEEE80211_REASON_MESH_CHAN_SWITCH_REG	= 65,	/* 11s */
	IEEE80211_REASON_MESH_CHAN_SWITCH_UNSPEC = 66,	/* 11s */

	IEEE80211_STATUS_SUCCESS		= 0,
	IEEE80211_STATUS_UNSPECIFIED		= 1,
	IEEE80211_STATUS_CAPINFO		= 10,
	IEEE80211_STATUS_NOT_ASSOCED		= 11,
	IEEE80211_STATUS_OTHER			= 12,
	IEEE80211_STATUS_ALG			= 13,
	IEEE80211_STATUS_SEQUENCE		= 14,
	IEEE80211_STATUS_CHALLENGE		= 15,
	IEEE80211_STATUS_TIMEOUT		= 16,
	IEEE80211_STATUS_TOOMANY		= 17,
	IEEE80211_STATUS_BASIC_RATE		= 18,
	IEEE80211_STATUS_SP_REQUIRED		= 19,	/* 11b */
	IEEE80211_STATUS_PBCC_REQUIRED		= 20,	/* 11b */
	IEEE80211_STATUS_CA_REQUIRED		= 21,	/* 11b */
	IEEE80211_STATUS_SPECMGMT_REQUIRED	= 22,	/* 11h */
	IEEE80211_STATUS_PWRCAP_REQUIRED	= 23,	/* 11h */
	IEEE80211_STATUS_SUPCHAN_REQUIRED	= 24,	/* 11h */
	IEEE80211_STATUS_SHORTSLOT_REQUIRED	= 25,	/* 11g */
	IEEE80211_STATUS_DSSSOFDM_REQUIRED	= 26,	/* 11g */
	IEEE80211_STATUS_MISSING_HT_CAPS	= 27,	/* 11n D3.0 */
	IEEE80211_STATUS_INVALID_IE		= 40,	/* 11i */
	IEEE80211_STATUS_GROUP_CIPHER_INVALID	= 41,	/* 11i */
	IEEE80211_STATUS_PAIRWISE_CIPHER_INVALID = 42,	/* 11i */
	IEEE80211_STATUS_AKMP_INVALID		= 43,	/* 11i */
	IEEE80211_STATUS_UNSUPP_RSN_IE_VERSION	= 44,	/* 11i */
	IEEE80211_STATUS_INVALID_RSN_IE_CAP	= 45,	/* 11i */
	IEEE80211_STATUS_CIPHER_SUITE_REJECTED	= 46,	/* 11i */
};

#define	IEEE80211_WEP_KEYLEN		5	/* 40bit */
#define	IEEE80211_WEP_IVLEN		3	/* 24bit */
#define	IEEE80211_WEP_KIDLEN		1	/* 1 octet */
#define	IEEE80211_WEP_CRCLEN		4	/* CRC-32 */
#define	IEEE80211_WEP_TOTLEN		(IEEE80211_WEP_IVLEN + \
					 IEEE80211_WEP_KIDLEN + \
					 IEEE80211_WEP_CRCLEN)
#define	IEEE80211_WEP_NKID		4	/* number of key ids */

/*
 * 802.11i defines an extended IV for use with non-WEP ciphers.
 * When the EXTIV bit is set in the key id byte an additional
 * 4 bytes immediately follow the IV for TKIP.  For CCMP the
 * EXTIV bit is likewise set but the 8 bytes represent the
 * CCMP header rather than IV+extended-IV.
 */
#define	IEEE80211_WEP_EXTIV		0x20
#define	IEEE80211_WEP_EXTIVLEN		4	/* extended IV length */
#define	IEEE80211_WEP_MICLEN		8	/* trailing MIC */

#define	IEEE80211_CRC_LEN		4

/*
 * Maximum acceptable MTU is:
 *	IEEE80211_MAX_LEN - WEP overhead - CRC -
 *		QoS overhead - RSN/WPA overhead
 * Min is arbitrarily chosen > IEEE80211_MIN_LEN.  The default
 * mtu is Ethernet-compatible; it's set by ether_ifattach.
 */
#define	IEEE80211_MTU_MAX		2290
#define	IEEE80211_MTU_MIN		32

#define	IEEE80211_MAX_LEN		(2300 + IEEE80211_CRC_LEN + \
    (IEEE80211_WEP_IVLEN + IEEE80211_WEP_KIDLEN + IEEE80211_WEP_CRCLEN))
#define	IEEE80211_ACK_LEN \
	(sizeof(struct ieee80211_frame_ack) + IEEE80211_CRC_LEN)
#define	IEEE80211_MIN_LEN \
	(sizeof(struct ieee80211_frame_min) + IEEE80211_CRC_LEN)

/*
 * The 802.11 spec says at most 2007 stations may be
 * associated at once.  For most AP's this is way more
 * than is feasible so we use a default of IEEE80211_AID_DEF.
 * This number may be overridden by the driver and/or by
 * user configuration but may not be less than IEEE80211_AID_MIN
 * (see _ieee80211.h for implementation-specific settings).
 */
#define	IEEE80211_AID_MAX		2007

#define	IEEE80211_AID(b)	((b) &~ 0xc000)

/* 
 * RTS frame length parameters.  The default is specified in
 * the 802.11 spec as 512; we treat it as implementation-dependent
 * so it's defined in ieee80211_var.h.  The max may be wrong
 * for jumbo frames.
 */
#define	IEEE80211_RTS_MIN		1
#define	IEEE80211_RTS_MAX		2346

/* 
 * TX fragmentation parameters.  As above for RTS, we treat
 * default as implementation-dependent so define it elsewhere.
 */
#define	IEEE80211_FRAG_MIN		256
#define	IEEE80211_FRAG_MAX		2346

/*
 * Beacon interval (TU's).  Min+max come from WiFi requirements.
 * As above, we treat default as implementation-dependent so
 * define it elsewhere.
 */
#define	IEEE80211_BINTVAL_MAX	1000	/* max beacon interval (TU's) */
#define	IEEE80211_BINTVAL_MIN	25	/* min beacon interval (TU's) */

/*
 * DTIM period (beacons).  Min+max are not really defined
 * by the protocol but we want them publicly visible so
 * define them here.
 */
#define	IEEE80211_DTIM_MAX	15	/* max DTIM period */
#define	IEEE80211_DTIM_MIN	1	/* min DTIM period */

/*
 * Beacon miss threshold (beacons).  As for DTIM, we define
 * them here to be publicly visible.  Note the max may be
 * clamped depending on device capabilities.
 */
#define	IEEE80211_HWBMISS_MIN 	1
#define	IEEE80211_HWBMISS_MAX 	255

/*
 * 802.11 frame duration definitions.
 */

struct ieee80211_duration {
	uint16_t	d_rts_dur;
	uint16_t	d_data_dur;
	uint16_t	d_plcp_len;
	uint8_t		d_residue;	/* unused octets in time slot */
};

/* One Time Unit (TU) is 1Kus = 1024 microseconds. */
#define IEEE80211_DUR_TU		1024

/* IEEE 802.11b durations for DSSS PHY in microseconds */
#define IEEE80211_DUR_DS_LONG_PREAMBLE	144
#define IEEE80211_DUR_DS_SHORT_PREAMBLE	72

#define IEEE80211_DUR_DS_SLOW_PLCPHDR	48
#define IEEE80211_DUR_DS_FAST_PLCPHDR	24
#define IEEE80211_DUR_DS_SLOW_ACK	112
#define IEEE80211_DUR_DS_FAST_ACK	56
#define IEEE80211_DUR_DS_SLOW_CTS	112
#define IEEE80211_DUR_DS_FAST_CTS	56

#define IEEE80211_DUR_DS_SLOT		20
#define IEEE80211_DUR_DS_SIFS		10
#define IEEE80211_DUR_DS_PIFS	(IEEE80211_DUR_DS_SIFS + IEEE80211_DUR_DS_SLOT)
#define IEEE80211_DUR_DS_DIFS	(IEEE80211_DUR_DS_SIFS + \
				 2 * IEEE80211_DUR_DS_SLOT)
#define IEEE80211_DUR_DS_EIFS	(IEEE80211_DUR_DS_SIFS + \
				 IEEE80211_DUR_DS_SLOW_ACK + \
				 IEEE80211_DUR_DS_LONG_PREAMBLE + \
				 IEEE80211_DUR_DS_SLOW_PLCPHDR + \
				 IEEE80211_DUR_DIFS)

#endif /* _NET80211_IEEE80211_H_ */
