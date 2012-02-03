/*
 * Copyright (C) 1999-2011, Broadcom Corporation
 * 
 *         Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * Fundamental types and constants relating to 802.11
 *
 * $Id: 802.11.h,v 9.260.2.6 2010/12/15 21:41:14 Exp $
 */

#ifndef _802_11_H_
#define _802_11_H_

#ifndef _TYPEDEFS_H_
#include <typedefs.h>
#endif

#ifndef _NET_ETHERNET_H_
#include <proto/ethernet.h>
#endif

#include <proto/wpa.h>

/* This marks the start of a packed structure section. */
#include <packed_section_start.h>


#define DOT11_TU_TO_US          1024    /* 802.11 Time Unit is 1024 microseconds */

/* Generic 802.11 frame constants */
#define DOT11_A3_HDR_LEN        24  /* d11 header length with A3 */
#define DOT11_A4_HDR_LEN        30  /* d11 header length with A4 */
#define DOT11_MAC_HDR_LEN       DOT11_A3_HDR_LEN    /* MAC header length */
#define DOT11_FCS_LEN           4   /* d11 FCS length */
#define DOT11_ICV_LEN           4   /* d11 ICV length */
#define DOT11_ICV_AES_LEN       8   /* d11 ICV/AES length */
#define DOT11_QOS_LEN           2   /* d11 QoS length */
#define DOT11_HTC_LEN           4   /* d11 HT Control field length */

#define DOT11_KEY_INDEX_SHIFT       6   /* d11 key index shift */
#define DOT11_IV_LEN            4   /* d11 IV length */
#define DOT11_IV_TKIP_LEN       8   /* d11 IV TKIP length */
#define DOT11_IV_AES_OCB_LEN        4   /* d11 IV/AES/OCB length */
#define DOT11_IV_AES_CCM_LEN        8   /* d11 IV/AES/CCM length */
#define DOT11_IV_MAX_LEN        8   /* maximum iv len for any encryption */

/* Includes MIC */
#define DOT11_MAX_MPDU_BODY_LEN     2304    /* max MPDU body length */
/* A4 header + QoS + CCMP + PDU + ICV + FCS = 2352 */
#define DOT11_MAX_MPDU_LEN      (DOT11_A4_HDR_LEN + \
					 DOT11_QOS_LEN + \
					 DOT11_IV_AES_CCM_LEN + \
					 DOT11_MAX_MPDU_BODY_LEN + \
					 DOT11_ICV_LEN + \
					 DOT11_FCS_LEN) /* d11 max MPDU length */

#define DOT11_MAX_SSID_LEN      32  /* d11 max ssid length */

/* dot11RTSThreshold */
#define DOT11_DEFAULT_RTS_LEN       2347    /* d11 default RTS length */
#define DOT11_MAX_RTS_LEN       2347    /* d11 max RTS length */

/* dot11FragmentationThreshold */
#define DOT11_MIN_FRAG_LEN      256 /* d11 min fragmentation length */
#define DOT11_MAX_FRAG_LEN      2346    /* Max frag is also limited by aMPDUMaxLength
						* of the attached PHY
						*/
#define DOT11_DEFAULT_FRAG_LEN      2346    /* d11 default fragmentation length */

/* dot11BeaconPeriod */
#define DOT11_MIN_BEACON_PERIOD     1   /* d11 min beacon period */
#define DOT11_MAX_BEACON_PERIOD     0xFFFF  /* d11 max beacon period */

/* dot11DTIMPeriod */
#define DOT11_MIN_DTIM_PERIOD       1   /* d11 min DTIM period */
#define DOT11_MAX_DTIM_PERIOD       0xFF    /* d11 max DTIM period */

/* 802.2 LLC/SNAP header used by 802.11 per 802.1H */
#define DOT11_LLC_SNAP_HDR_LEN      8   /* d11 LLC/SNAP header length */
#define DOT11_OUI_LEN           3   /* d11 OUI length */
BWL_PRE_PACKED_STRUCT struct dot11_llc_snap_header {
	uint8   dsap;               /* always 0xAA */
	uint8   ssap;               /* always 0xAA */
	uint8   ctl;                /* always 0x03 */
	uint8   oui[DOT11_OUI_LEN];     /* RFC1042: 0x00 0x00 0x00
						 * Bridge-Tunnel: 0x00 0x00 0xF8
						 */
	uint16  type;               /* ethertype */
} BWL_POST_PACKED_STRUCT;

/* RFC1042 header used by 802.11 per 802.1H */
#define RFC1042_HDR_LEN (ETHER_HDR_LEN + DOT11_LLC_SNAP_HDR_LEN)    /* RCF1042 header length */

/* Generic 802.11 MAC header */
/*
 * N.B.: This struct reflects the full 4 address 802.11 MAC header.
 *       The fields are defined such that the shorter 1, 2, and 3
 *       address headers just use the first k fields.
 */
BWL_PRE_PACKED_STRUCT struct dot11_header {
	uint16          fc;     /* frame control */
	uint16          durid;      /* duration/ID */
	struct ether_addr   a1;     /* address 1 */
	struct ether_addr   a2;     /* address 2 */
	struct ether_addr   a3;     /* address 3 */
	uint16          seq;        /* sequence control */
	struct ether_addr   a4;     /* address 4 */
} BWL_POST_PACKED_STRUCT;

/* Control frames */

BWL_PRE_PACKED_STRUCT struct dot11_rts_frame {
	uint16          fc;     /* frame control */
	uint16          durid;      /* duration/ID */
	struct ether_addr   ra;     /* receiver address */
	struct ether_addr   ta;     /* transmitter address */
} BWL_POST_PACKED_STRUCT;
#define DOT11_RTS_LEN       16      /* d11 RTS frame length */

BWL_PRE_PACKED_STRUCT struct dot11_cts_frame {
	uint16          fc;     /* frame control */
	uint16          durid;      /* duration/ID */
	struct ether_addr   ra;     /* receiver address */
} BWL_POST_PACKED_STRUCT;
#define DOT11_CTS_LEN       10      /* d11 CTS frame length */

BWL_PRE_PACKED_STRUCT struct dot11_ack_frame {
	uint16          fc;     /* frame control */
	uint16          durid;      /* duration/ID */
	struct ether_addr   ra;     /* receiver address */
} BWL_POST_PACKED_STRUCT;
#define DOT11_ACK_LEN       10      /* d11 ACK frame length */

BWL_PRE_PACKED_STRUCT struct dot11_ps_poll_frame {
	uint16          fc;     /* frame control */
	uint16          durid;      /* AID */
	struct ether_addr   bssid;      /* receiver address, STA in AP */
	struct ether_addr   ta;     /* transmitter address */
} BWL_POST_PACKED_STRUCT;
#define DOT11_PS_POLL_LEN   16      /* d11 PS poll frame length */

BWL_PRE_PACKED_STRUCT struct dot11_cf_end_frame {
	uint16          fc;     /* frame control */
	uint16          durid;      /* duration/ID */
	struct ether_addr   ra;     /* receiver address */
	struct ether_addr   bssid;      /* transmitter address, STA in AP */
} BWL_POST_PACKED_STRUCT;
#define DOT11_CS_END_LEN    16      /* d11 CF-END frame length */

/* RWL wifi protocol: The Vendor Specific Action frame is defined for vendor-specific signaling
*  category+OUI+vendor specific content ( this can be variable)
*/
BWL_PRE_PACKED_STRUCT struct dot11_action_wifi_vendor_specific {
	uint8   category;
	uint8   OUI[3];
	uint8   type;
	uint8   subtype;
	uint8   data[1040];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_action_wifi_vendor_specific dot11_action_wifi_vendor_specific_t;

/* generic vender specific action frame with variable length */
BWL_PRE_PACKED_STRUCT struct dot11_action_vs_frmhdr {
	uint8   category;
	uint8   OUI[3];
	uint8   type;
	uint8   subtype;
	uint8   data[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_action_vs_frmhdr dot11_action_vs_frmhdr_t;
#define DOT11_ACTION_VS_HDR_LEN 6

#define BCM_ACTION_OUI_BYTE0    0x00
#define BCM_ACTION_OUI_BYTE1    0x90
#define BCM_ACTION_OUI_BYTE2    0x4c

/* BA/BAR Control parameters */
#define DOT11_BA_CTL_POLICY_NORMAL  0x0000  /* normal ack */
#define DOT11_BA_CTL_POLICY_NOACK   0x0001  /* no ack */
#define DOT11_BA_CTL_POLICY_MASK    0x0001  /* ack policy mask */

#define DOT11_BA_CTL_MTID       0x0002  /* multi tid BA */
#define DOT11_BA_CTL_COMPRESSED     0x0004  /* compressed bitmap */

#define DOT11_BA_CTL_NUMMSDU_MASK   0x0FC0  /* num msdu in bitmap mask */
#define DOT11_BA_CTL_NUMMSDU_SHIFT  6   /* num msdu in bitmap shift */

#define DOT11_BA_CTL_TID_MASK       0xF000  /* tid mask */
#define DOT11_BA_CTL_TID_SHIFT      12  /* tid shift */

/* control frame header (BA/BAR) */
BWL_PRE_PACKED_STRUCT struct dot11_ctl_header {
	uint16          fc;     /* frame control */
	uint16          durid;      /* duration/ID */
	struct ether_addr   ra;     /* receiver address */
	struct ether_addr   ta;     /* transmitter address */
} BWL_POST_PACKED_STRUCT;
#define DOT11_CTL_HDR_LEN   16      /* control frame hdr len */

/* BAR frame payload */
BWL_PRE_PACKED_STRUCT struct dot11_bar {
	uint16          bar_control;    /* BAR Control */
	uint16          seqnum;     /* Starting Sequence control */
} BWL_POST_PACKED_STRUCT;
#define DOT11_BAR_LEN       4       /* BAR frame payload length */

#define DOT11_BA_BITMAP_LEN 128     /* bitmap length */
#define DOT11_BA_CMP_BITMAP_LEN 8       /* compressed bitmap length */
/* BA frame payload */
BWL_PRE_PACKED_STRUCT struct dot11_ba {
	uint16          ba_control; /* BA Control */
	uint16          seqnum;     /* Starting Sequence control */
	uint8           bitmap[DOT11_BA_BITMAP_LEN];    /* Block Ack Bitmap */
} BWL_POST_PACKED_STRUCT;
#define DOT11_BA_LEN        4       /* BA frame payload len (wo bitmap) */

/* Management frame header */
BWL_PRE_PACKED_STRUCT struct dot11_management_header {
	uint16          fc;     /* frame control */
	uint16          durid;      /* duration/ID */
	struct ether_addr   da;     /* receiver address */
	struct ether_addr   sa;     /* transmitter address */
	struct ether_addr   bssid;      /* BSS ID */
	uint16          seq;        /* sequence control */
} BWL_POST_PACKED_STRUCT;
#define DOT11_MGMT_HDR_LEN  24      /* d11 management header length */

/* Management frame payloads */

BWL_PRE_PACKED_STRUCT struct dot11_bcn_prb {
	uint32          timestamp[2];
	uint16          beacon_interval;
	uint16          capability;
} BWL_POST_PACKED_STRUCT;
#define DOT11_BCN_PRB_LEN   12      /* 802.11 beacon/probe frame fixed length */
#define DOT11_BCN_PRB_FIXED_LEN 12      /* 802.11 beacon/probe frame fixed length */

BWL_PRE_PACKED_STRUCT struct dot11_auth {
	uint16          alg;        /* algorithm */
	uint16          seq;        /* sequence control */
	uint16          status;     /* status code */
} BWL_POST_PACKED_STRUCT;
#define DOT11_AUTH_FIXED_LEN    6       /* length of auth frame without challenge IE */

BWL_PRE_PACKED_STRUCT struct dot11_assoc_req {
	uint16          capability; /* capability information */
	uint16          listen;     /* listen interval */
} BWL_POST_PACKED_STRUCT;
#define DOT11_ASSOC_REQ_FIXED_LEN   4   /* length of assoc frame without info elts */

BWL_PRE_PACKED_STRUCT struct dot11_reassoc_req {
	uint16          capability; /* capability information */
	uint16          listen;     /* listen interval */
	struct ether_addr   ap;     /* Current AP address */
} BWL_POST_PACKED_STRUCT;
#define DOT11_REASSOC_REQ_FIXED_LEN 10  /* length of assoc frame without info elts */

BWL_PRE_PACKED_STRUCT struct dot11_assoc_resp {
	uint16          capability; /* capability information */
	uint16          status;     /* status code */
	uint16          aid;        /* association ID */
} BWL_POST_PACKED_STRUCT;
#define DOT11_ASSOC_RESP_FIXED_LEN  6   /* length of assoc resp frame without info elts */

BWL_PRE_PACKED_STRUCT struct dot11_action_measure {
	uint8   category;
	uint8   action;
	uint8   token;
	uint8   data[1];
} BWL_POST_PACKED_STRUCT;
#define DOT11_ACTION_MEASURE_LEN    3   /* d11 action measurement header length */

BWL_PRE_PACKED_STRUCT struct dot11_action_ht_ch_width {
	uint8   category;
	uint8   action;
	uint8   ch_width;
} BWL_POST_PACKED_STRUCT;

BWL_PRE_PACKED_STRUCT struct dot11_action_ht_mimops {
	uint8   category;
	uint8   action;
	uint8   control;
} BWL_POST_PACKED_STRUCT;

#define SM_PWRSAVE_ENABLE   1
#define SM_PWRSAVE_MODE     2

/* ************* 802.11h related definitions. ************* */
BWL_PRE_PACKED_STRUCT struct dot11_power_cnst {
	uint8 id;
	uint8 len;
	uint8 power;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_power_cnst dot11_power_cnst_t;

BWL_PRE_PACKED_STRUCT struct dot11_power_cap {
	uint8 min;
	uint8 max;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_power_cap dot11_power_cap_t;

BWL_PRE_PACKED_STRUCT struct dot11_tpc_rep {
	uint8 id;
	uint8 len;
	uint8 tx_pwr;
	uint8 margin;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_tpc_rep dot11_tpc_rep_t;
#define DOT11_MNG_IE_TPC_REPORT_LEN 2   /* length of IE data, not including 2 byte header */

BWL_PRE_PACKED_STRUCT struct dot11_supp_channels {
	uint8 id;
	uint8 len;
	uint8 first_channel;
	uint8 num_channels;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_supp_channels dot11_supp_channels_t;

/* Extension Channel Offset IE: 802.11n-D1.0 spec. added sideband
 * offset for 40MHz operation.  The possible 3 values are:
 * 1 = above control channel
 * 3 = below control channel
 * 0 = no extension channel
 */
BWL_PRE_PACKED_STRUCT struct dot11_extch {
	uint8   id;     /* IE ID, 62, DOT11_MNG_EXT_CHANNEL_OFFSET */
	uint8   len;        /* IE length */
	uint8   extch;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_extch dot11_extch_ie_t;

BWL_PRE_PACKED_STRUCT struct dot11_brcm_extch {
	uint8   id;     /* IE ID, 221, DOT11_MNG_PROPR_ID */
	uint8   len;        /* IE length */
	uint8   oui[3];     /* Proprietary OUI, BRCM_PROP_OUI */
	uint8   type;           /* type inidicates what follows */
	uint8   extch;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_brcm_extch dot11_brcm_extch_ie_t;

#define BRCM_EXTCH_IE_LEN   5
#define BRCM_EXTCH_IE_TYPE  53  /* 802.11n ID not yet assigned */
#define DOT11_EXTCH_IE_LEN  1
#define DOT11_EXT_CH_MASK   0x03    /* extension channel mask */
#define DOT11_EXT_CH_UPPER  0x01    /* ext. ch. on upper sb */
#define DOT11_EXT_CH_LOWER  0x03    /* ext. ch. on lower sb */
#define DOT11_EXT_CH_NONE   0x00    /* no extension ch.  */

BWL_PRE_PACKED_STRUCT struct dot11_action_frmhdr {
	uint8   category;
	uint8   action;
	uint8   data[1];
} BWL_POST_PACKED_STRUCT;
#define DOT11_ACTION_FRMHDR_LEN 2

/* CSA IE data structure */
BWL_PRE_PACKED_STRUCT struct dot11_channel_switch {
	uint8 id;   /* id DOT11_MNG_CHANNEL_SWITCH_ID */
	uint8 len;  /* length of IE */
	uint8 mode; /* mode 0 or 1 */
	uint8 channel;  /* channel switch to */
	uint8 count;    /* number of beacons before switching */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_channel_switch dot11_chan_switch_ie_t;

#define DOT11_SWITCH_IE_LEN 3   /* length of IE data, not including 2 byte header */
/* CSA mode - 802.11h-2003 $7.3.2.20 */
#define DOT11_CSA_MODE_ADVISORY     0   /* no DOT11_CSA_MODE_NO_TX restriction imposed */
#define DOT11_CSA_MODE_NO_TX        1   /* no transmission upon receiving CSA frame. */

BWL_PRE_PACKED_STRUCT struct dot11_action_switch_channel {
	uint8   category;
	uint8   action;
	dot11_chan_switch_ie_t chan_switch_ie;  /* for switch IE */
	dot11_brcm_extch_ie_t extch_ie;     /* extension channel offset */
} BWL_POST_PACKED_STRUCT;

BWL_PRE_PACKED_STRUCT struct dot11_csa_body {
	uint8 mode; /* mode 0 or 1 */
	uint8 reg;  /* regulatory class */
	uint8 channel;  /* channel switch to */
	uint8 count;    /* number of beacons before switching */
} BWL_POST_PACKED_STRUCT;

/* 11n Extended Channel Switch IE data structure */
BWL_PRE_PACKED_STRUCT struct dot11_ext_csa {
	uint8 id;   /* id DOT11_MNG_EXT_CHANNEL_SWITCH_ID */
	uint8 len;  /* length of IE */
	struct dot11_csa_body b;    /* body of the ie */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_ext_csa dot11_ext_csa_ie_t;
#define DOT11_EXT_CSA_IE_LEN    4   /* length of extended channel switch IE body */

BWL_PRE_PACKED_STRUCT struct dot11_action_ext_csa {
	uint8   category;
	uint8   action;
	dot11_ext_csa_ie_t chan_switch_ie;  /* for switch IE */
} BWL_POST_PACKED_STRUCT;

BWL_PRE_PACKED_STRUCT struct dot11y_action_ext_csa {
	uint8   category;
	uint8   action;
	struct dot11_csa_body b;    /* body of the ie */
} BWL_POST_PACKED_STRUCT;

BWL_PRE_PACKED_STRUCT struct dot11_obss_coex {
	uint8   id;
	uint8   len;
	uint8   info;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_obss_coex dot11_obss_coex_t;
#define DOT11_OBSS_COEXINFO_LEN 1   /* length of OBSS Coexistence INFO IE */

#define DOT11_OBSS_COEX_INFO_REQ        0x01
#define DOT11_OBSS_COEX_40MHZ_INTOLERANT    0x02
#define DOT11_OBSS_COEX_20MHZ_WIDTH_REQ 0x04

BWL_PRE_PACKED_STRUCT struct dot11_obss_chanlist {
	uint8   id;
	uint8   len;
	uint8   regclass;
	uint8   chanlist[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_obss_chanlist dot11_obss_chanlist_t;
#define DOT11_OBSS_CHANLIST_FIXED_LEN   1   /* fixed length of regclass */

BWL_PRE_PACKED_STRUCT struct dot11_extcap_ie {
	uint8 id;
	uint8 len;
	uint8 cap;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_extcap_ie dot11_extcap_ie_t;
#define DOT11_EXTCAP_LEN    1

/* 802.11h/802.11k Measurement Request/Report IEs */
/* Measurement Type field */
#define DOT11_MEASURE_TYPE_BASIC    0   /* d11 measurement basic type */
#define DOT11_MEASURE_TYPE_CCA      1   /* d11 measurement CCA type */
#define DOT11_MEASURE_TYPE_RPI      2   /* d11 measurement RPI type */
#define DOT11_MEASURE_TYPE_CHLOAD       3   /* d11 measurement Channel Load type */
#define DOT11_MEASURE_TYPE_NOISE        4   /* d11 measurement Noise Histogram type */
#define DOT11_MEASURE_TYPE_BEACON       5   /* d11 measurement Beacon type */
#define DOT11_MEASURE_TYPE_FRAME    6   /* d11 measurement Frame type */
#define DOT11_MEASURE_TYPE_STATS        7   /* d11 measurement STA Statistics type */
#define DOT11_MEASURE_TYPE_LCI      8   /* d11 measurement LCI type */
#define DOT11_MEASURE_TYPE_TXSTREAM     9   /* d11 measurement TX Stream type */
#define DOT11_MEASURE_TYPE_PAUSE        255 /* d11 measurement pause type */

/* Measurement Request Modes */
#define DOT11_MEASURE_MODE_PARALLEL     (1<<0)  /* d11 measurement parallel */
#define DOT11_MEASURE_MODE_ENABLE   (1<<1)  /* d11 measurement enable */
#define DOT11_MEASURE_MODE_REQUEST  (1<<2)  /* d11 measurement request */
#define DOT11_MEASURE_MODE_REPORT   (1<<3)  /* d11 measurement report */
#define DOT11_MEASURE_MODE_DUR  (1<<4)  /* d11 measurement dur mandatory */
/* Measurement Report Modes */
#define DOT11_MEASURE_MODE_LATE     (1<<0)  /* d11 measurement late */
#define DOT11_MEASURE_MODE_INCAPABLE    (1<<1)  /* d11 measurement incapable */
#define DOT11_MEASURE_MODE_REFUSED  (1<<2)  /* d11 measurement refuse */
/* Basic Measurement Map bits */
#define DOT11_MEASURE_BASIC_MAP_BSS ((uint8)(1<<0)) /* d11 measurement basic map BSS */
#define DOT11_MEASURE_BASIC_MAP_OFDM    ((uint8)(1<<1)) /* d11 measurement map OFDM */
#define DOT11_MEASURE_BASIC_MAP_UKNOWN  ((uint8)(1<<2)) /* d11 measurement map unknown */
#define DOT11_MEASURE_BASIC_MAP_RADAR   ((uint8)(1<<3)) /* d11 measurement map radar */
#define DOT11_MEASURE_BASIC_MAP_UNMEAS  ((uint8)(1<<4)) /* d11 measurement map unmeasuremnt */

BWL_PRE_PACKED_STRUCT struct dot11_meas_req {
	uint8 id;
	uint8 len;
	uint8 token;
	uint8 mode;
	uint8 type;
	uint8 channel;
	uint8 start_time[8];
	uint16 duration;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_meas_req dot11_meas_req_t;
#define DOT11_MNG_IE_MREQ_LEN 14    /* d11 measurement request IE length */
/* length of Measure Request IE data not including variable len */
#define DOT11_MNG_IE_MREQ_FIXED_LEN 3   /* d11 measurement request IE fixed length */

BWL_PRE_PACKED_STRUCT struct dot11_meas_rep {
	uint8 id;
	uint8 len;
	uint8 token;
	uint8 mode;
	uint8 type;
	BWL_PRE_PACKED_STRUCT union
	{
		BWL_PRE_PACKED_STRUCT struct {
			uint8 channel;
			uint8 start_time[8];
			uint16 duration;
			uint8 map;
		} BWL_POST_PACKED_STRUCT basic;
		uint8 data[1];
	} BWL_POST_PACKED_STRUCT rep;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_meas_rep dot11_meas_rep_t;

/* length of Measure Report IE data not including variable len */
#define DOT11_MNG_IE_MREP_FIXED_LEN 3   /* d11 measurement response IE fixed length */

BWL_PRE_PACKED_STRUCT struct dot11_meas_rep_basic {
	uint8 channel;
	uint8 start_time[8];
	uint16 duration;
	uint8 map;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_meas_rep_basic dot11_meas_rep_basic_t;
#define DOT11_MEASURE_BASIC_REP_LEN 12  /* d11 measurement basic report length */

BWL_PRE_PACKED_STRUCT struct dot11_quiet {
	uint8 id;
	uint8 len;
	uint8 count;    /* TBTTs until beacon interval in quiet starts */
	uint8 period;   /* Beacon intervals between periodic quiet periods ? */
	uint16 duration;    /* Length of quiet period, in TU's */
	uint16 offset;  /* TU's offset from TBTT in Count field */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_quiet dot11_quiet_t;

BWL_PRE_PACKED_STRUCT struct chan_map_tuple {
	uint8 channel;
	uint8 map;
} BWL_POST_PACKED_STRUCT;
typedef struct chan_map_tuple chan_map_tuple_t;

BWL_PRE_PACKED_STRUCT struct dot11_ibss_dfs {
	uint8 id;
	uint8 len;
	uint8 eaddr[ETHER_ADDR_LEN];
	uint8 interval;
	chan_map_tuple_t map[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_ibss_dfs dot11_ibss_dfs_t;

/* WME Elements */
#define WME_OUI         "\x00\x50\xf2"  /* WME OUI */
#define WME_OUI_LEN     3
#define WME_OUI_TYPE        2   /* WME type */
#define WME_VER         1   /* WME version */
#define WME_TYPE        2   /* WME type, deprecated */
#define WME_SUBTYPE_IE      0   /* Information Element */
#define WME_SUBTYPE_PARAM_IE    1   /* Parameter Element */
#define WME_SUBTYPE_TSPEC   2   /* Traffic Specification */

/* WME Access Category Indices (ACIs) */
#define AC_BE           0   /* Best Effort */
#define AC_BK           1   /* Background */
#define AC_VI           2   /* Video */
#define AC_VO           3   /* Voice */
#define AC_COUNT        4   /* number of ACs */

typedef uint8 ac_bitmap_t;  /* AC bitmap of (1 << AC_xx) */

#define AC_BITMAP_NONE      0x0 /* No ACs */
#define AC_BITMAP_ALL       0xf /* All ACs */
#define AC_BITMAP_TST(ab, ac)   (((ab) & (1 << (ac))) != 0)
#define AC_BITMAP_SET(ab, ac)   (((ab) |= (1 << (ac))))
#define AC_BITMAP_RESET(ab, ac) (((ab) &= ~(1 << (ac))))

/* WME Information Element (IE) */
BWL_PRE_PACKED_STRUCT struct wme_ie {
	uint8 oui[3];
	uint8 type;
	uint8 subtype;
	uint8 version;
	uint8 qosinfo;
} BWL_POST_PACKED_STRUCT;
typedef struct wme_ie wme_ie_t;
#define WME_IE_LEN 7    /* WME IE length */

BWL_PRE_PACKED_STRUCT struct edcf_acparam {
	uint8   ACI;
	uint8   ECW;
	uint16  TXOP;       /* stored in network order (ls octet first) */
} BWL_POST_PACKED_STRUCT;
typedef struct edcf_acparam edcf_acparam_t;

/* WME Parameter Element (PE) */
BWL_PRE_PACKED_STRUCT struct wme_param_ie {
	uint8 oui[3];
	uint8 type;
	uint8 subtype;
	uint8 version;
	uint8 qosinfo;
	uint8 rsvd;
	edcf_acparam_t acparam[AC_COUNT];
} BWL_POST_PACKED_STRUCT;
typedef struct wme_param_ie wme_param_ie_t;
#define WME_PARAM_IE_LEN            24          /* WME Parameter IE length */

/* QoS Info field for IE as sent from AP */
#define WME_QI_AP_APSD_MASK         0x80        /* U-APSD Supported mask */
#define WME_QI_AP_APSD_SHIFT        7           /* U-APSD Supported shift */
#define WME_QI_AP_COUNT_MASK        0x0f        /* Parameter set count mask */
#define WME_QI_AP_COUNT_SHIFT       0           /* Parameter set count shift */

/* QoS Info field for IE as sent from STA */
#define WME_QI_STA_MAXSPLEN_MASK    0x60        /* Max Service Period Length mask */
#define WME_QI_STA_MAXSPLEN_SHIFT   5           /* Max Service Period Length shift */
#define WME_QI_STA_APSD_ALL_MASK    0xf         /* APSD all AC bits mask */
#define WME_QI_STA_APSD_ALL_SHIFT   0           /* APSD all AC bits shift */
#define WME_QI_STA_APSD_BE_MASK     0x8         /* APSD AC_BE mask */
#define WME_QI_STA_APSD_BE_SHIFT    3           /* APSD AC_BE shift */
#define WME_QI_STA_APSD_BK_MASK     0x4         /* APSD AC_BK mask */
#define WME_QI_STA_APSD_BK_SHIFT    2           /* APSD AC_BK shift */
#define WME_QI_STA_APSD_VI_MASK     0x2         /* APSD AC_VI mask */
#define WME_QI_STA_APSD_VI_SHIFT    1           /* APSD AC_VI shift */
#define WME_QI_STA_APSD_VO_MASK     0x1         /* APSD AC_VO mask */
#define WME_QI_STA_APSD_VO_SHIFT    0           /* APSD AC_VO shift */

/* ACI */
#define EDCF_AIFSN_MIN               1           /* AIFSN minimum value */
#define EDCF_AIFSN_MAX               15          /* AIFSN maximum value */
#define EDCF_AIFSN_MASK              0x0f        /* AIFSN mask */
#define EDCF_ACM_MASK                0x10        /* ACM mask */
#define EDCF_ACI_MASK                0x60        /* ACI mask */
#define EDCF_ACI_SHIFT               5           /* ACI shift */
#define EDCF_AIFSN_SHIFT             12          /* 4 MSB(0xFFF) in ifs_ctl for AC idx */

/* ECW */
#define EDCF_ECW_MIN                 0           /* cwmin/cwmax exponent minimum value */
#define EDCF_ECW_MAX                 15          /* cwmin/cwmax exponent maximum value */
#define EDCF_ECW2CW(exp)             ((1 << (exp)) - 1)
#define EDCF_ECWMIN_MASK             0x0f        /* cwmin exponent form mask */
#define EDCF_ECWMAX_MASK             0xf0        /* cwmax exponent form mask */
#define EDCF_ECWMAX_SHIFT            4           /* cwmax exponent form shift */

/* TXOP */
#define EDCF_TXOP_MIN                0           /* TXOP minimum value */
#define EDCF_TXOP_MAX                65535       /* TXOP maximum value */
#define EDCF_TXOP2USEC(txop)         ((txop) << 5)

/* Default BE ACI value for non-WME connection STA */
#define NON_EDCF_AC_BE_ACI_STA          0x02

/* Default EDCF parameters that AP advertises for STA to use; WMM draft Table 12 */
#define EDCF_AC_BE_ACI_STA           0x03   /* STA ACI value for best effort AC */
#define EDCF_AC_BE_ECW_STA           0xA4   /* STA ECW value for best effort AC */
#define EDCF_AC_BE_TXOP_STA          0x0000 /* STA TXOP value for best effort AC */
#define EDCF_AC_BK_ACI_STA           0x27   /* STA ACI value for background AC */
#define EDCF_AC_BK_ECW_STA           0xA4   /* STA ECW value for background AC */
#define EDCF_AC_BK_TXOP_STA          0x0000 /* STA TXOP value for background AC */
#define EDCF_AC_VI_ACI_STA           0x42   /* STA ACI value for video AC */
#define EDCF_AC_VI_ECW_STA           0x43   /* STA ECW value for video AC */
#define EDCF_AC_VI_TXOP_STA          0x005e /* STA TXOP value for video AC */
#define EDCF_AC_VO_ACI_STA           0x62   /* STA ACI value for audio AC */
#define EDCF_AC_VO_ECW_STA           0x32   /* STA ECW value for audio AC */
#define EDCF_AC_VO_TXOP_STA          0x002f /* STA TXOP value for audio AC */

/* Default EDCF parameters that AP uses; WMM draft Table 14 */
#define EDCF_AC_BE_ACI_AP            0x03   /* AP ACI value for best effort AC */
#define EDCF_AC_BE_ECW_AP            0x64   /* AP ECW value for best effort AC */
#define EDCF_AC_BE_TXOP_AP           0x0000 /* AP TXOP value for best effort AC */
#define EDCF_AC_BK_ACI_AP            0x27   /* AP ACI value for background AC */
#define EDCF_AC_BK_ECW_AP            0xA4   /* AP ECW value for background AC */
#define EDCF_AC_BK_TXOP_AP           0x0000 /* AP TXOP value for background AC */
#define EDCF_AC_VI_ACI_AP            0x41   /* AP ACI value for video AC */
#define EDCF_AC_VI_ECW_AP            0x43   /* AP ECW value for video AC */
#define EDCF_AC_VI_TXOP_AP           0x005e /* AP TXOP value for video AC */
#define EDCF_AC_VO_ACI_AP            0x61   /* AP ACI value for audio AC */
#define EDCF_AC_VO_ECW_AP            0x32   /* AP ECW value for audio AC */
#define EDCF_AC_VO_TXOP_AP           0x002f /* AP TXOP value for audio AC */

/* EDCA Parameter IE */
BWL_PRE_PACKED_STRUCT struct edca_param_ie {
	uint8 qosinfo;
	uint8 rsvd;
	edcf_acparam_t acparam[AC_COUNT];
} BWL_POST_PACKED_STRUCT;
typedef struct edca_param_ie edca_param_ie_t;
#define EDCA_PARAM_IE_LEN            18          /* EDCA Parameter IE length */

/* QoS Capability IE */
BWL_PRE_PACKED_STRUCT struct qos_cap_ie {
	uint8 qosinfo;
} BWL_POST_PACKED_STRUCT;
typedef struct qos_cap_ie qos_cap_ie_t;

BWL_PRE_PACKED_STRUCT struct dot11_qbss_load_ie {
	uint8 id;           /* 11, DOT11_MNG_QBSS_LOAD_ID */
	uint8 length;
	uint16 station_count;       /* total number of STAs associated */
	uint8 channel_utilization;  /* % of time, normalized to 255, QAP sensed medium busy */
	uint16 aac;             /* available admission capacity */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_qbss_load_ie dot11_qbss_load_ie_t;

/* nom_msdu_size */
#define FIXED_MSDU_SIZE 0x8000      /* MSDU size is fixed */
#define MSDU_SIZE_MASK  0x7fff      /* (Nominal or fixed) MSDU size */

/* surplus_bandwidth */
/* Represented as 3 bits of integer, binary point, 13 bits fraction */
#define INTEGER_SHIFT   13  /* integer shift */
#define FRACTION_MASK   0x1FFF  /* fraction mask */

/* Management Notification Frame */
BWL_PRE_PACKED_STRUCT struct dot11_management_notification {
	uint8 category;         /* DOT11_ACTION_NOTIFICATION */
	uint8 action;
	uint8 token;
	uint8 status;
	uint8 data[1];          /* Elements */
} BWL_POST_PACKED_STRUCT;
#define DOT11_MGMT_NOTIFICATION_LEN 4   /* Fixed length */

/* WME Action Codes */
#define WME_ADDTS_REQUEST   0   /* WME ADDTS request */
#define WME_ADDTS_RESPONSE  1   /* WME ADDTS response */
#define WME_DELTS_REQUEST   2   /* WME DELTS request */

/* WME Setup Response Status Codes */
#define WME_ADMISSION_ACCEPTED      0   /* WME admission accepted */
#define WME_INVALID_PARAMETERS      1   /* WME invalide parameters */
#define WME_ADMISSION_REFUSED       3   /* WME admission refused */

/* Macro to take a pointer to a beacon or probe response
 * body and return the char* pointer to the SSID info element
 */
#define BCN_PRB_SSID(body) ((char*)(body) + DOT11_BCN_PRB_LEN)

/* Authentication frame payload constants */
#define DOT11_OPEN_SYSTEM   0   /* d11 open authentication */
#define DOT11_SHARED_KEY    1   /* d11 shared authentication */
#define DOT11_OPEN_SHARED   2   /* try open first, then shared if open failed */
#define DOT11_FAST_BSS      3   /* d11 fast bss authentication */
#define DOT11_CHALLENGE_LEN 128 /* d11 challenge text length */

/* Frame control macros */
#define FC_PVER_MASK        0x3 /* PVER mask */
#define FC_PVER_SHIFT       0   /* PVER shift */
#define FC_TYPE_MASK        0xC /* type mask */
#define FC_TYPE_SHIFT       2   /* type shift */
#define FC_SUBTYPE_MASK     0xF0    /* subtype mask */
#define FC_SUBTYPE_SHIFT    4   /* subtype shift */
#define FC_TODS         0x100   /* to DS */
#define FC_TODS_SHIFT       8   /* to DS shift */
#define FC_FROMDS       0x200   /* from DS */
#define FC_FROMDS_SHIFT     9   /* from DS shift */
#define FC_MOREFRAG     0x400   /* more frag. */
#define FC_MOREFRAG_SHIFT   10  /* more frag. shift */
#define FC_RETRY        0x800   /* retry */
#define FC_RETRY_SHIFT      11  /* retry shift */
#define FC_PM           0x1000  /* PM */
#define FC_PM_SHIFT     12  /* PM shift */
#define FC_MOREDATA     0x2000  /* more data */
#define FC_MOREDATA_SHIFT   13  /* more data shift */
#define FC_WEP          0x4000  /* WEP */
#define FC_WEP_SHIFT        14  /* WEP shift */
#define FC_ORDER        0x8000  /* order */
#define FC_ORDER_SHIFT      15  /* order shift */

/* sequence control macros */
#define SEQNUM_SHIFT        4   /* seq. number shift */
#define SEQNUM_MAX      0x1000  /* max seqnum + 1 */
#define FRAGNUM_MASK        0xF /* frag. number mask */

/* Frame Control type/subtype defs */

/* FC Types */
#define FC_TYPE_MNG     0   /* management type */
#define FC_TYPE_CTL     1   /* control type */
#define FC_TYPE_DATA        2   /* data type */

/* Management Subtypes */
#define FC_SUBTYPE_ASSOC_REQ        0   /* assoc. request */
#define FC_SUBTYPE_ASSOC_RESP       1   /* assoc. response */
#define FC_SUBTYPE_REASSOC_REQ      2   /* reassoc. request */
#define FC_SUBTYPE_REASSOC_RESP     3   /* reassoc. response */
#define FC_SUBTYPE_PROBE_REQ        4   /* probe request */
#define FC_SUBTYPE_PROBE_RESP       5   /* probe response */
#define FC_SUBTYPE_BEACON       8   /* beacon */
#define FC_SUBTYPE_ATIM         9   /* ATIM */
#define FC_SUBTYPE_DISASSOC     10  /* disassoc. */
#define FC_SUBTYPE_AUTH         11  /* authentication */
#define FC_SUBTYPE_DEAUTH       12  /* de-authentication */
#define FC_SUBTYPE_ACTION       13  /* action */
#define FC_SUBTYPE_ACTION_NOACK     14  /* action no-ack */

/* Control Subtypes */
#define FC_SUBTYPE_CTL_WRAPPER      7   /* Control Wrapper */
#define FC_SUBTYPE_BLOCKACK_REQ     8   /* Block Ack Req */
#define FC_SUBTYPE_BLOCKACK     9   /* Block Ack */
#define FC_SUBTYPE_PS_POLL      10  /* PS poll */
#define FC_SUBTYPE_RTS          11  /* RTS */
#define FC_SUBTYPE_CTS          12  /* CTS */
#define FC_SUBTYPE_ACK          13  /* ACK */
#define FC_SUBTYPE_CF_END       14  /* CF-END */
#define FC_SUBTYPE_CF_END_ACK       15  /* CF-END ACK */

/* Data Subtypes */
#define FC_SUBTYPE_DATA         0   /* Data */
#define FC_SUBTYPE_DATA_CF_ACK      1   /* Data + CF-ACK */
#define FC_SUBTYPE_DATA_CF_POLL     2   /* Data + CF-Poll */
#define FC_SUBTYPE_DATA_CF_ACK_POLL 3   /* Data + CF-Ack + CF-Poll */
#define FC_SUBTYPE_NULL         4   /* Null */
#define FC_SUBTYPE_CF_ACK       5   /* CF-Ack */
#define FC_SUBTYPE_CF_POLL      6   /* CF-Poll */
#define FC_SUBTYPE_CF_ACK_POLL      7   /* CF-Ack + CF-Poll */
#define FC_SUBTYPE_QOS_DATA     8   /* QoS Data */
#define FC_SUBTYPE_QOS_DATA_CF_ACK  9   /* QoS Data + CF-Ack */
#define FC_SUBTYPE_QOS_DATA_CF_POLL 10  /* QoS Data + CF-Poll */
#define FC_SUBTYPE_QOS_DATA_CF_ACK_POLL 11  /* QoS Data + CF-Ack + CF-Poll */
#define FC_SUBTYPE_QOS_NULL     12  /* QoS Null */
#define FC_SUBTYPE_QOS_CF_POLL      14  /* QoS CF-Poll */
#define FC_SUBTYPE_QOS_CF_ACK_POLL  15  /* QoS CF-Ack + CF-Poll */

/* Data Subtype Groups */
#define FC_SUBTYPE_ANY_QOS(s)       (((s) & 8) != 0)
#define FC_SUBTYPE_ANY_NULL(s)      (((s) & 4) != 0)
#define FC_SUBTYPE_ANY_CF_POLL(s)   (((s) & 2) != 0)
#define FC_SUBTYPE_ANY_CF_ACK(s)    (((s) & 1) != 0)

/* Type/Subtype Combos */
#define FC_KIND_MASK        (FC_TYPE_MASK | FC_SUBTYPE_MASK)    /* FC kind mask */

#define FC_KIND(t, s)   (((t) << FC_TYPE_SHIFT) | ((s) << FC_SUBTYPE_SHIFT))    /* FC kind */

#define FC_SUBTYPE(fc)  (((fc) & FC_SUBTYPE_MASK) >> FC_SUBTYPE_SHIFT)  /* Subtype from FC */
#define FC_TYPE(fc) (((fc) & FC_TYPE_MASK) >> FC_TYPE_SHIFT)    /* Type from FC */

#define FC_ASSOC_REQ    FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_ASSOC_REQ)  /* assoc. request */
#define FC_ASSOC_RESP   FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_ASSOC_RESP) /* assoc. response */
#define FC_REASSOC_REQ  FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_REASSOC_REQ)    /* reassoc. request */
#define FC_REASSOC_RESP FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_REASSOC_RESP)   /* reassoc. response */
#define FC_PROBE_REQ    FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_PROBE_REQ)  /* probe request */
#define FC_PROBE_RESP   FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_PROBE_RESP) /* probe response */
#define FC_BEACON   FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_BEACON)     /* beacon */
#define FC_DISASSOC FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_DISASSOC)   /* disassoc */
#define FC_AUTH     FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_AUTH)       /* authentication */
#define FC_DEAUTH   FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_DEAUTH)     /* deauthentication */
#define FC_ACTION   FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_ACTION)     /* action */
#define FC_ACTION_NOACK FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_ACTION_NOACK)   /* action no-ack */

#define FC_CTL_WRAPPER  FC_KIND(FC_TYPE_CTL, FC_SUBTYPE_CTL_WRAPPER)    /* Control Wrapper */
#define FC_BLOCKACK_REQ FC_KIND(FC_TYPE_CTL, FC_SUBTYPE_BLOCKACK_REQ)   /* Block Ack Req */
#define FC_BLOCKACK FC_KIND(FC_TYPE_CTL, FC_SUBTYPE_BLOCKACK)   /* Block Ack */
#define FC_PS_POLL  FC_KIND(FC_TYPE_CTL, FC_SUBTYPE_PS_POLL)    /* PS poll */
#define FC_RTS      FC_KIND(FC_TYPE_CTL, FC_SUBTYPE_RTS)        /* RTS */
#define FC_CTS      FC_KIND(FC_TYPE_CTL, FC_SUBTYPE_CTS)        /* CTS */
#define FC_ACK      FC_KIND(FC_TYPE_CTL, FC_SUBTYPE_ACK)        /* ACK */
#define FC_CF_END   FC_KIND(FC_TYPE_CTL, FC_SUBTYPE_CF_END)     /* CF-END */
#define FC_CF_END_ACK   FC_KIND(FC_TYPE_CTL, FC_SUBTYPE_CF_END_ACK) /* CF-END ACK */

#define FC_DATA     FC_KIND(FC_TYPE_DATA, FC_SUBTYPE_DATA)      /* data */
#define FC_NULL_DATA    FC_KIND(FC_TYPE_DATA, FC_SUBTYPE_NULL)      /* null data */
#define FC_DATA_CF_ACK  FC_KIND(FC_TYPE_DATA, FC_SUBTYPE_DATA_CF_ACK)   /* data CF ACK */
#define FC_QOS_DATA FC_KIND(FC_TYPE_DATA, FC_SUBTYPE_QOS_DATA)  /* QoS data */
#define FC_QOS_NULL FC_KIND(FC_TYPE_DATA, FC_SUBTYPE_QOS_NULL)  /* QoS null */

/* QoS Control Field */

/* 802.1D Priority */
#define QOS_PRIO_SHIFT      0   /* QoS priority shift */
#define QOS_PRIO_MASK       0x0007  /* QoS priority mask */
#define QOS_PRIO(qos)       (((qos) & QOS_PRIO_MASK) >> QOS_PRIO_SHIFT) /* QoS priority */

/* Traffic Identifier */
#define QOS_TID_SHIFT       0   /* QoS TID shift */
#define QOS_TID_MASK        0x000f  /* QoS TID mask */
#define QOS_TID(qos)        (((qos) & QOS_TID_MASK) >> QOS_TID_SHIFT)   /* QoS TID */

/* End of Service Period (U-APSD) */
#define QOS_EOSP_SHIFT      4   /* QoS End of Service Period shift */
#define QOS_EOSP_MASK       0x0010  /* QoS End of Service Period mask */
#define QOS_EOSP(qos)       (((qos) & QOS_EOSP_MASK) >> QOS_EOSP_SHIFT) /* Qos EOSP */

/* Ack Policy */
#define QOS_ACK_NORMAL_ACK  0   /* Normal Ack */
#define QOS_ACK_NO_ACK      1   /* No Ack (eg mcast) */
#define QOS_ACK_NO_EXP_ACK  2   /* No Explicit Ack */
#define QOS_ACK_BLOCK_ACK   3   /* Block Ack */
#define QOS_ACK_SHIFT       5   /* QoS ACK shift */
#define QOS_ACK_MASK        0x0060  /* QoS ACK mask */
#define QOS_ACK(qos)        (((qos) & QOS_ACK_MASK) >> QOS_ACK_SHIFT)   /* QoS ACK */

/* A-MSDU flag */
#define QOS_AMSDU_SHIFT     7   /* AMSDU shift */
#define QOS_AMSDU_MASK      0x0080  /* AMSDU mask */

/* Management Frames */

/* Management Frame Constants */

/* Fixed fields */
#define DOT11_MNG_AUTH_ALGO_LEN     2   /* d11 management auth. algo. length */
#define DOT11_MNG_AUTH_SEQ_LEN      2   /* d11 management auth. seq. length */
#define DOT11_MNG_BEACON_INT_LEN    2   /* d11 management beacon interval length */
#define DOT11_MNG_CAP_LEN       2   /* d11 management cap. length */
#define DOT11_MNG_AP_ADDR_LEN       6   /* d11 management AP address length */
#define DOT11_MNG_LISTEN_INT_LEN    2   /* d11 management listen interval length */
#define DOT11_MNG_REASON_LEN        2   /* d11 management reason length */
#define DOT11_MNG_AID_LEN       2   /* d11 management AID length */
#define DOT11_MNG_STATUS_LEN        2   /* d11 management status length */
#define DOT11_MNG_TIMESTAMP_LEN     8   /* d11 management timestamp length */

/* DUR/ID field in assoc resp is 0xc000 | AID */
#define DOT11_AID_MASK          0x3fff  /* d11 AID mask */

/* Reason Codes */
#define DOT11_RC_RESERVED       0   /* d11 RC reserved */
#define DOT11_RC_UNSPECIFIED        1   /* Unspecified reason */
#define DOT11_RC_AUTH_INVAL     2   /* Previous authentication no longer valid */
#define DOT11_RC_DEAUTH_LEAVING     3   /* Deauthenticated because sending station
						 * is leaving (or has left) IBSS or ESS
						 */
#define DOT11_RC_INACTIVITY     4   /* Disassociated due to inactivity */
#define DOT11_RC_BUSY           5   /* Disassociated because AP is unable to handle
						 * all currently associated stations
						 */
#define DOT11_RC_INVAL_CLASS_2      6   /* Class 2 frame received from
						 * nonauthenticated station
						 */
#define DOT11_RC_INVAL_CLASS_3      7   /* Class 3 frame received from
						 *  nonassociated station
						 */
#define DOT11_RC_DISASSOC_LEAVING   8   /* Disassociated because sending station is
						 * leaving (or has left) BSS
						 */
#define DOT11_RC_NOT_AUTH       9   /* Station requesting (re)association is not
						 * authenticated with responding station
						 */
#define DOT11_RC_BAD_PC         10  /* Unacceptable power capability element */
#define DOT11_RC_BAD_CHANNELS       11  /* Unacceptable supported channels element */
/* 12 is unused */

/* 32-39 are QSTA specific reasons added in 11e */
#define DOT11_RC_UNSPECIFIED_QOS    32  /* unspecified QoS-related reason */
#define DOT11_RC_INSUFFCIENT_BW     33  /* QAP lacks sufficient bandwidth */
#define DOT11_RC_EXCESSIVE_FRAMES   34  /* excessive number of frames need ack */
#define DOT11_RC_TX_OUTSIDE_TXOP    35  /* transmitting outside the limits of txop */
#define DOT11_RC_LEAVING_QBSS       36  /* QSTA is leaving the QBSS (or restting) */
#define DOT11_RC_BAD_MECHANISM      37  /* does not want to use the mechanism */
#define DOT11_RC_SETUP_NEEDED       38  /* mechanism needs a setup */
#define DOT11_RC_TIMEOUT        39  /* timeout */

#define DOT11_RC_MAX            23  /* Reason codes > 23 are reserved */

/* Status Codes */
#define DOT11_SC_SUCCESS        0   /* Successful */
#define DOT11_SC_FAILURE        1   /* Unspecified failure */
#define DOT11_SC_CAP_MISMATCH       10  /* Cannot support all requested
						 * capabilities in the Capability
						 * Information field
						 */
#define DOT11_SC_REASSOC_FAIL       11  /* Reassociation denied due to inability
						 * to confirm that association exists
						 */
#define DOT11_SC_ASSOC_FAIL     12  /* Association denied due to reason
						 * outside the scope of this standard
						 */
#define DOT11_SC_AUTH_MISMATCH      13  /* Responding station does not support
						 * the specified authentication
						 * algorithm
						 */
#define DOT11_SC_AUTH_SEQ       14  /* Received an Authentication frame
						 * with authentication transaction
						 * sequence number out of expected
						 * sequence
						 */
#define DOT11_SC_AUTH_CHALLENGE_FAIL    15  /* Authentication rejected because of
						 * challenge failure
						 */
#define DOT11_SC_AUTH_TIMEOUT       16  /* Authentication rejected due to timeout
						 * waiting for next frame in sequence
						 */
#define DOT11_SC_ASSOC_BUSY_FAIL    17  /* Association denied because AP is
						 * unable to handle additional
						 * associated stations
						 */
#define DOT11_SC_ASSOC_RATE_MISMATCH    18  /* Association denied due to requesting
						 * station not supporting all of the
						 * data rates in the BSSBasicRateSet
						 * parameter
						 */
#define DOT11_SC_ASSOC_SHORT_REQUIRED   19  /* Association denied due to requesting
						 * station not supporting the Short
						 * Preamble option
						 */
#define DOT11_SC_ASSOC_PBCC_REQUIRED    20  /* Association denied due to requesting
						 * station not supporting the PBCC
						 * Modulation option
						 */
#define DOT11_SC_ASSOC_AGILITY_REQUIRED 21  /* Association denied due to requesting
						 * station not supporting the Channel
						 * Agility option
						 */
#define DOT11_SC_ASSOC_SPECTRUM_REQUIRED    22  /* Association denied because Spectrum
							 * Management capability is required.
							 */
#define DOT11_SC_ASSOC_BAD_POWER_CAP    23  /* Association denied because the info
						 * in the Power Cap element is
						 * unacceptable.
						 */
#define DOT11_SC_ASSOC_BAD_SUP_CHANNELS 24  /* Association denied because the info
						 * in the Supported Channel element is
						 * unacceptable
						 */
#define DOT11_SC_ASSOC_SHORTSLOT_REQUIRED   25  /* Association denied due to requesting
							 * station not supporting the Short Slot
							 * Time option
							 */
#define DOT11_SC_ASSOC_ERPBCC_REQUIRED  26  /* Association denied due to requesting
						 * station not supporting the ER-PBCC
						 * Modulation option
						 */
#define DOT11_SC_ASSOC_DSSOFDM_REQUIRED 27  /* Association denied due to requesting
						 * station not supporting the DSS-OFDM
						 * option
						 */

#define DOT11_SC_DECLINED       37  /* request declined */
#define DOT11_SC_INVALID_PARAMS     38  /* One or more params have invalid values */
#define DOT11_SC_INVALID_AKMP       43  /* Association denied due to invalid AKMP */
#define DOT11_SC_INVALID_MDID       54  /* Association denied due to invalid MDID */
#define DOT11_SC_INVALID_FTIE       55  /* Association denied due to invalid FTIE */

/* Info Elts, length of INFORMATION portion of Info Elts */
#define DOT11_MNG_DS_PARAM_LEN          1   /* d11 management DS parameter length */
#define DOT11_MNG_IBSS_PARAM_LEN        2   /* d11 management IBSS parameter length */

/* TIM Info element has 3 bytes fixed info in INFORMATION field,
 * followed by 1 to 251 bytes of Partial Virtual Bitmap
 */
#define DOT11_MNG_TIM_FIXED_LEN         3   /* d11 management TIM fixed length */
#define DOT11_MNG_TIM_DTIM_COUNT        0   /* d11 management DTIM count */
#define DOT11_MNG_TIM_DTIM_PERIOD       1   /* d11 management DTIM period */
#define DOT11_MNG_TIM_BITMAP_CTL        2   /* d11 management TIM BITMAP control  */
#define DOT11_MNG_TIM_PVB           3   /* d11 management TIM PVB */

/* TLV defines */
#define TLV_TAG_OFF     0   /* tag offset */
#define TLV_LEN_OFF     1   /* length offset */
#define TLV_HDR_LEN     2   /* header length */
#define TLV_BODY_OFF        2   /* body offset */

/* Management Frame Information Element IDs */
#define DOT11_MNG_SSID_ID           0   /* d11 management SSID id */
#define DOT11_MNG_RATES_ID          1   /* d11 management rates id */
#define DOT11_MNG_FH_PARMS_ID           2   /* d11 management FH parameter id */
#define DOT11_MNG_DS_PARMS_ID           3   /* d11 management DS parameter id */
#define DOT11_MNG_CF_PARMS_ID           4   /* d11 management CF parameter id */
#define DOT11_MNG_TIM_ID            5   /* d11 management TIM id */
#define DOT11_MNG_IBSS_PARMS_ID         6   /* d11 management IBSS parameter id */
#define DOT11_MNG_COUNTRY_ID            7   /* d11 management country id */
#define DOT11_MNG_HOPPING_PARMS_ID      8   /* d11 management hopping parameter id */
#define DOT11_MNG_HOPPING_TABLE_ID      9   /* d11 management hopping table id */
#define DOT11_MNG_REQUEST_ID            10  /* d11 management request id */
#define DOT11_MNG_QBSS_LOAD_ID          11  /* d11 management QBSS Load id */
#define DOT11_MNG_EDCA_PARAM_ID         12  /* 11E EDCA Parameter id */
#define DOT11_MNG_CHALLENGE_ID          16  /* d11 management chanllenge id */
#define DOT11_MNG_PWR_CONSTRAINT_ID     32  /* 11H PowerConstraint */
#define DOT11_MNG_PWR_CAP_ID            33  /* 11H PowerCapability */
#define DOT11_MNG_TPC_REQUEST_ID        34  /* 11H TPC Request */
#define DOT11_MNG_TPC_REPORT_ID         35  /* 11H TPC Report */
#define DOT11_MNG_SUPP_CHANNELS_ID      36  /* 11H Supported Channels */
#define DOT11_MNG_CHANNEL_SWITCH_ID     37  /* 11H ChannelSwitch Announcement */
#define DOT11_MNG_MEASURE_REQUEST_ID        38  /* 11H MeasurementRequest */
#define DOT11_MNG_MEASURE_REPORT_ID     39  /* 11H MeasurementReport */
#define DOT11_MNG_QUIET_ID          40  /* 11H Quiet */
#define DOT11_MNG_IBSS_DFS_ID           41  /* 11H IBSS_DFS */
#define DOT11_MNG_ERP_ID            42  /* d11 management ERP id */
#define DOT11_MNG_TS_DELAY_ID           43  /* d11 management TS Delay id */
#define DOT11_MNG_HT_CAP            45  /* d11 mgmt HT cap id */
#define DOT11_MNG_QOS_CAP_ID            46  /* 11E QoS Capability id */
#define DOT11_MNG_NONERP_ID         47  /* d11 management NON-ERP id */
#define DOT11_MNG_RSN_ID            48  /* d11 management RSN id */
#define DOT11_MNG_EXT_RATES_ID          50  /* d11 management ext. rates id */
#define DOT11_MNG_AP_CHREP_ID       51  /* 11k AP Channel report id */
#define DOT11_MNG_NBR_REP_ID        52  /* 11k Neighbor report id */
#define DOT11_MNG_MDIE_ID       54  /* 11r Mobility domain id */
#define DOT11_MNG_FTIE_ID       55  /* 11r Fast Bss Transition id */
#define DOT11_MNG_FT_TI_ID      56  /* 11r Timeout Interval id */
#define DOT11_MNG_REGCLASS_ID           59  /* d11 management regulatory class id */
#define DOT11_MNG_EXT_CSA_ID            60  /* d11 Extended CSA */
#define DOT11_MNG_HT_ADD            61  /* d11 mgmt additional HT info */
#define DOT11_MNG_EXT_CHANNEL_OFFSET        62  /* d11 mgmt ext channel offset */


#define DOT11_MNG_RRM_CAP_ID        70  /* 11k radio measurement capability */
#define DOT11_MNG_HT_BSS_COEXINFO_ID        72  /* d11 mgmt OBSS Coexistence INFO */
#define DOT11_MNG_HT_BSS_CHANNEL_REPORT_ID  73  /* d11 mgmt OBSS Intolerant Channel list */
#define DOT11_MNG_HT_OBSS_ID            74  /* d11 mgmt OBSS HT info */
#define DOT11_MNG_EXT_CAP           127 /* d11 mgmt ext capability */
#define DOT11_MNG_WPA_ID            221 /* d11 management WPA id */
#define DOT11_MNG_PROPR_ID          221 /* d11 management proprietary id */
/* should start using this one instead of above two */
#define DOT11_MNG_VS_ID             221 /* d11 management Vendor Specific IE */

/* Rate element Basic flag and rate mask */
#define DOT11_RATE_BASIC            0x80    /* flag for a Basic Rate */
#define DOT11_RATE_MASK             0x7F    /* mask for numeric part of rate */

/* ERP info element bit values */
#define DOT11_MNG_ERP_LEN           1   /* ERP is currently 1 byte long */
#define DOT11_MNG_NONERP_PRESENT        0x01    /* NonERP (802.11b) STAs are present
							 *in the BSS
							 */
#define DOT11_MNG_USE_PROTECTION        0x02    /* Use protection mechanisms for
							 *ERP-OFDM frames
							 */
#define DOT11_MNG_BARKER_PREAMBLE       0x04    /* Short Preambles: 0 == allowed,
							 * 1 == not allowed
							 */
/* TS Delay element offset & size */
#define DOT11_MGN_TS_DELAY_LEN      4   /* length of TS DELAY IE */
#define TS_DELAY_FIELD_SIZE         4   /* TS DELAY field size */

/* Capability Information Field */
#define DOT11_CAP_ESS               0x0001  /* d11 cap. ESS */
#define DOT11_CAP_IBSS              0x0002  /* d11 cap. IBSS */
#define DOT11_CAP_POLLABLE          0x0004  /* d11 cap. pollable */
#define DOT11_CAP_POLL_RQ           0x0008  /* d11 cap. poll request */
#define DOT11_CAP_PRIVACY           0x0010  /* d11 cap. privacy */
#define DOT11_CAP_SHORT             0x0020  /* d11 cap. short */
#define DOT11_CAP_PBCC              0x0040  /* d11 cap. PBCC */
#define DOT11_CAP_AGILITY           0x0080  /* d11 cap. agility */
#define DOT11_CAP_SPECTRUM          0x0100  /* d11 cap. spectrum */
#define DOT11_CAP_SHORTSLOT         0x0400  /* d11 cap. shortslot */
#define DOT11_CAP_RRM           0x1000  /* d11 cap. 11k radio measurement */
#define DOT11_CAP_CCK_OFDM          0x2000  /* d11 cap. CCK/OFDM */

/* Extended Capability Information Field */
#define DOT11_OBSS_COEX_MNG_SUPPORT 0x01    /* 20/40 BSS Coexistence Management support */

/*
 * Action Frame Constants
 */
#define DOT11_ACTION_HDR_LEN        2   /* action frame category + action field */
#define DOT11_ACTION_CAT_OFF        0   /* category offset */
#define DOT11_ACTION_ACT_OFF        1   /* action offset */

/* Action Category field (sec 7.3.1.11) */
#define DOT11_ACTION_CAT_ERR_MASK   0x80    /* category error mask */
#define DOT11_ACTION_CAT_MASK       0x7F    /* category mask */
#define DOT11_ACTION_CAT_SPECT_MNG  0   /* category spectrum management */
#define DOT11_ACTION_CAT_QOS        1   /* category QoS */
#define DOT11_ACTION_CAT_DLS        2   /* category DLS */
#define DOT11_ACTION_CAT_BLOCKACK   3   /* category block ack */
#define DOT11_ACTION_CAT_PUBLIC     4   /* category public */
#define DOT11_ACTION_CAT_RRM        5   /* category radio measurements */
#define DOT11_ACTION_CAT_FBT    6   /* category fast bss transition */
#define DOT11_ACTION_CAT_HT     7   /* category for HT */
#define DOT11_ACTION_CAT_BSSMGMT    10  /* category for BSS transition management */
#define DOT11_ACTION_NOTIFICATION   17
#define DOT11_ACTION_CAT_VS     127 /* category Vendor Specific */

/* Spectrum Management Action IDs (sec 7.4.1) */
#define DOT11_SM_ACTION_M_REQ       0   /* d11 action measurement request */
#define DOT11_SM_ACTION_M_REP       1   /* d11 action measurement response */
#define DOT11_SM_ACTION_TPC_REQ     2   /* d11 action TPC request */
#define DOT11_SM_ACTION_TPC_REP     3   /* d11 action TPC response */
#define DOT11_SM_ACTION_CHANNEL_SWITCH  4   /* d11 action channel switch */
#define DOT11_SM_ACTION_EXT_CSA     5   /* d11 extened CSA for 11n */

/* HT action ids */
#define DOT11_ACTION_ID_HT_CH_WIDTH 0   /* notify channel width action id */
#define DOT11_ACTION_ID_HT_MIMO_PS  1   /* mimo ps action id */

/* Public action ids */
#define DOT11_PUB_ACTION_BSS_COEX_MNG   0   /* 20/40 Coexistence Management action id */
#define DOT11_PUB_ACTION_CHANNEL_SWITCH 4   /* d11 action channel switch */

/* Block Ack action types */
#define DOT11_BA_ACTION_ADDBA_REQ   0   /* ADDBA Req action frame type */
#define DOT11_BA_ACTION_ADDBA_RESP  1   /* ADDBA Resp action frame type */
#define DOT11_BA_ACTION_DELBA       2   /* DELBA action frame type */

/* ADDBA action parameters */
#define DOT11_ADDBA_PARAM_AMSDU_SUP 0x0001  /* AMSDU supported under BA */
#define DOT11_ADDBA_PARAM_POLICY_MASK   0x0002  /* policy mask(ack vs delayed) */
#define DOT11_ADDBA_PARAM_POLICY_SHIFT  1   /* policy shift */
#define DOT11_ADDBA_PARAM_TID_MASK  0x003c  /* tid mask */
#define DOT11_ADDBA_PARAM_TID_SHIFT 2   /* tid shift */
#define DOT11_ADDBA_PARAM_BSIZE_MASK    0xffc0  /* buffer size mask */
#define DOT11_ADDBA_PARAM_BSIZE_SHIFT   6   /* buffer size shift */

#define DOT11_ADDBA_POLICY_DELAYED  0   /* delayed BA policy */
#define DOT11_ADDBA_POLICY_IMMEDIATE    1   /* immediate BA policy */

BWL_PRE_PACKED_STRUCT struct dot11_addba_req {
	uint8 category;             /* category of action frame (3) */
	uint8 action;               /* action: addba req */
	uint8 token;                /* identifier */
	uint16 addba_param_set;         /* parameter set */
	uint16 timeout;             /* timeout in seconds */
	uint16 start_seqnum;            /* starting sequence number */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_addba_req dot11_addba_req_t;
#define DOT11_ADDBA_REQ_LEN     9   /* length of addba req frame */

BWL_PRE_PACKED_STRUCT struct dot11_addba_resp {
	uint8 category;             /* category of action frame (3) */
	uint8 action;               /* action: addba resp */
	uint8 token;                /* identifier */
	uint16 status;              /* status of add request */
	uint16 addba_param_set;         /* negotiated parameter set */
	uint16 timeout;             /* negotiated timeout in seconds */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_addba_resp dot11_addba_resp_t;
#define DOT11_ADDBA_RESP_LEN        9   /* length of addba resp frame */

/* DELBA action parameters */
#define DOT11_DELBA_PARAM_INIT_MASK 0x0800  /* initiator mask */
#define DOT11_DELBA_PARAM_INIT_SHIFT    11  /* initiator shift */
#define DOT11_DELBA_PARAM_TID_MASK  0xf000  /* tid mask */
#define DOT11_DELBA_PARAM_TID_SHIFT 12  /* tid shift */

BWL_PRE_PACKED_STRUCT struct dot11_delba {
	uint8 category;             /* category of action frame (3) */
	uint8 action;               /* action: addba req */
	uint16 delba_param_set;         /* paarmeter set */
	uint16 reason;              /* reason for dellba */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_delba dot11_delba_t;
#define DOT11_DELBA_LEN         6   /* length of delba frame */

/* ************* 802.11k related definitions. ************* */

/* Radio measurements enabled capability ie */

#define DOT11_RRM_CAP_LEN       5   /* length of rrm cap bitmap */
BWL_PRE_PACKED_STRUCT struct dot11_rrm_cap_ie {
	uint8 cap[DOT11_RRM_CAP_LEN];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rrm_cap_ie dot11_rrm_cap_ie_t;

/* Bitmap definitions for cap ie */
#define DOT11_RRM_CAP_LINK          0
#define DOT11_RRM_CAP_NEIGHBOR_REPORT   1
#define DOT11_RRM_CAP_PARALLEL      2
#define DOT11_RRM_CAP_REPEATED      3
#define DOT11_RRM_CAP_BCN_PASSIVE   4
#define DOT11_RRM_CAP_BCN_ACTIVE    5
#define DOT11_RRM_CAP_BCN_TABLE     6
#define DOT11_RRM_CAP_BCN_REP_COND  7
#define DOT11_RRM_CAP_AP_CHANREP    16

/* Radio Measurements action ids */
#define DOT11_RM_ACTION_RM_REQ      0   /* Radio measurement request */
#define DOT11_RM_ACTION_RM_REP      1   /* Radio measurement report */
#define DOT11_RM_ACTION_LM_REQ      2   /* Link measurement request */
#define DOT11_RM_ACTION_LM_REP      3   /* Link measurement report */
#define DOT11_RM_ACTION_NR_REQ      4   /* Neighbor report request */
#define DOT11_RM_ACTION_NR_REP      5   /* Neighbor report response */

/* Generic radio measurement action frame header */
BWL_PRE_PACKED_STRUCT struct dot11_rm_action {
	uint8 category;             /* category of action frame (5) */
	uint8 action;               /* radio measurement action */
	uint8 token;                /* dialog token */
	uint8 data[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rm_action dot11_rm_action_t;
#define DOT11_RM_ACTION_LEN 3

BWL_PRE_PACKED_STRUCT struct dot11_rmreq {
	uint8 category;             /* category of action frame (5) */
	uint8 action;               /* radio measurement action */
	uint8 token;                /* dialog token */
	uint16 reps;                /* no. of repetitions */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rmreq dot11_rmreq_t;
#define DOT11_RMREQ_LEN 5

BWL_PRE_PACKED_STRUCT struct dot11_rm_ie {
	uint8 id;
	uint8 len;
	uint8 token;
	uint8 mode;
	uint8 type;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rm_ie dot11_rm_ie_t;
#define DOT11_RM_IE_LEN 5

/* Definitions for "mode" bits in rm req */
#define DOT11_RMREQ_MODE_PARALLEL   1
#define DOT11_RMREQ_MODE_ENABLE     2
#define DOT11_RMREQ_MODE_REQUEST    4
#define DOT11_RMREQ_MODE_REPORT     8
#define DOT11_RMREQ_MODE_DURMAND    0x10    /* Duration Mandatory */

/* Definitions for "mode" bits in rm rep */
#define DOT11_RMREP_MODE_LATE       1
#define DOT11_RMREP_MODE_INCAPABLE  2
#define DOT11_RMREP_MODE_REFUSED    4

BWL_PRE_PACKED_STRUCT struct dot11_rmreq_bcn {
	uint8 id;
	uint8 len;
	uint8 token;
	uint8 mode;
	uint8 type;
	uint8 reg;
	uint8 channel;
	uint16 interval;
	uint16 duration;
	uint8 bcn_mode;
	struct ether_addr   bssid;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rmreq_bcn dot11_rmreq_bcn_t;
#define DOT11_RMREQ_BCN_LEN 18

BWL_PRE_PACKED_STRUCT struct dot11_rmrep_bcn {
	uint8 reg;
	uint8 channel;
	uint32 starttime[2];
	uint16 duration;
	uint8 frame_info;
	uint8 rcpi;
	uint8 rsni;
	struct ether_addr   bssid;
	uint8 antenna_id;
	uint32 parent_tsf;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rmrep_bcn dot11_rmrep_bcn_t;
#define DOT11_RMREP_BCN_LEN 26

/* Beacon request measurement mode */
#define DOT11_RMREQ_BCN_PASSIVE 0
#define DOT11_RMREQ_BCN_ACTIVE  1
#define DOT11_RMREQ_BCN_TABLE   2

/* Sub-element IDs for Beacon Request */
#define DOT11_RMREQ_BCN_SSID_ID 0
#define DOT11_RMREQ_BCN_REPINFO_ID  1
#define DOT11_RMREQ_BCN_REPDET_ID   2
#define DOT11_RMREQ_BCN_REQUEST_ID  10
#define DOT11_RMREQ_BCN_APCHREP_ID  51

/* Reporting Detail element definition */
#define DOT11_RMREQ_BCN_REPDET_FIXED    0   /* Fixed length fields only */
#define DOT11_RMREQ_BCN_REPDET_REQUEST  1   /* + requested information elems */
#define DOT11_RMREQ_BCN_REPDET_ALL  2   /* All fields */

/* Sub-element IDs for Beacon Report */
#define DOT11_RMREP_BCN_FRM_BODY    1

/* Neighbor measurement report */
BWL_PRE_PACKED_STRUCT struct dot11_rmrep_nbr {
	struct ether_addr   bssid;
	uint32  bssid_info;
	uint8 reg;
	uint8 channel;
	uint8 phytype;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rmrep_nbr dot11_rmrep_nbr_t;
#define DOT11_RMREP_NBR_LEN 13

/* MLME Enumerations */
#define DOT11_BSSTYPE_INFRASTRUCTURE        0   /* d11 infrastructure */
#define DOT11_BSSTYPE_INDEPENDENT       1   /* d11 independent */
#define DOT11_BSSTYPE_ANY           2   /* d11 any BSS type */
#define DOT11_SCANTYPE_ACTIVE           0   /* d11 scan active */
#define DOT11_SCANTYPE_PASSIVE          1   /* d11 scan passive */

/* Link Measurement */
BWL_PRE_PACKED_STRUCT struct dot11_lmreq {
	uint8 category;             /* category of action frame (5) */
	uint8 action;               /* radio measurement action */
	uint8 token;                /* dialog token */
	uint8 txpwr;                /* Transmit Power Used */
	uint8 maxtxpwr;             /* Max Transmit Power */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_lmreq dot11_lmreq_t;
#define DOT11_LMREQ_LEN 5

BWL_PRE_PACKED_STRUCT struct dot11_lmrep {
	uint8 category;             /* category of action frame (5) */
	uint8 action;               /* radio measurement action */
	uint8 token;                /* dialog token */
	dot11_tpc_rep_t tpc;            /* TPC element */
	uint8 rxant;                /* Receive Antenna ID */
	uint8 txant;                /* Transmit Antenna ID */
	uint8 rcpi;             /* RCPI */
	uint8 rsni;             /* RSNI */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_lmrep dot11_lmrep_t;
#define DOT11_LMREP_LEN 11

/* 802.11 BRCM "Compromise" Pre N constants */
#define PREN_PREAMBLE       24  /* green field preamble time */
#define PREN_MM_EXT     12  /* extra mixed mode preamble time */
#define PREN_PREAMBLE_EXT   4   /* extra preamble (multiply by unique_streams-1) */

/* 802.11N PHY constants */
#define RIFS_11N_TIME       2   /* NPHY RIFS time */

/* 802.11 HT PLCP format 802.11n-2009, sec 20.3.9.4.3
 * HT-SIG is composed of two 24 bit parts, HT-SIG1 and HT-SIG2
 */
/* HT-SIG1 */
#define HT_SIG1_MCS_MASK        0x00007F
#define HT_SIG1_CBW             0x000080
#define HT_SIG1_HT_LENGTH       0xFFFF00

/* HT-SIG2 */
#define HT_SIG2_SMOOTHING       0x000001
#define HT_SIG2_NOT_SOUNDING    0x000002
#define HT_SIG2_RESERVED        0x000004
#define HT_SIG2_AGGREGATION     0x000008
#define HT_SIG2_STBC_MASK       0x000030
#define HT_SIG2_STBC_SHIFT      4
#define HT_SIG2_FEC_CODING      0x000040
#define HT_SIG2_SHORT_GI        0x000080
#define HT_SIG2_ESS_MASK        0x000300
#define HT_SIG2_ESS_SHIFT       8
#define HT_SIG2_CRC             0x03FC00
#define HT_SIG2_TAIL            0x1C0000

/* 802.11 A PHY constants */
#define APHY_SLOT_TIME      9   /* APHY slot time */
#define APHY_SIFS_TIME      16  /* APHY SIFS time */
#define APHY_DIFS_TIME      (APHY_SIFS_TIME + (2 * APHY_SLOT_TIME)) /* APHY DIFS time */
#define APHY_PREAMBLE_TIME  16  /* APHY preamble time */
#define APHY_SIGNAL_TIME    4   /* APHY signal time */
#define APHY_SYMBOL_TIME    4   /* APHY symbol time */
#define APHY_SERVICE_NBITS  16  /* APHY service nbits */
#define APHY_TAIL_NBITS     6   /* APHY tail nbits */
#define APHY_CWMIN      15  /* APHY cwmin */

/* 802.11 B PHY constants */
#define BPHY_SLOT_TIME      20  /* BPHY slot time */
#define BPHY_SIFS_TIME      10  /* BPHY SIFS time */
#define BPHY_DIFS_TIME      50  /* BPHY DIFS time */
#define BPHY_PLCP_TIME      192 /* BPHY PLCP time */
#define BPHY_PLCP_SHORT_TIME    96  /* BPHY PLCP short time */
#define BPHY_CWMIN      31  /* BPHY cwmin */

/* 802.11 G constants */
#define DOT11_OFDM_SIGNAL_EXTENSION 6   /* d11 OFDM signal extension */

#define PHY_CWMAX       1023    /* PHY cwmax */

#define DOT11_MAXNUMFRAGS   16  /* max # fragments per MSDU */

/* dot11Counters Table - 802.11 spec., Annex D */
typedef struct d11cnt {
	uint32      txfrag;     /* dot11TransmittedFragmentCount */
	uint32      txmulti;    /* dot11MulticastTransmittedFrameCount */
	uint32      txfail;     /* dot11FailedCount */
	uint32      txretry;    /* dot11RetryCount */
	uint32      txretrie;   /* dot11MultipleRetryCount */
	uint32      rxdup;      /* dot11FrameduplicateCount */
	uint32      txrts;      /* dot11RTSSuccessCount */
	uint32      txnocts;    /* dot11RTSFailureCount */
	uint32      txnoack;    /* dot11ACKFailureCount */
	uint32      rxfrag;     /* dot11ReceivedFragmentCount */
	uint32      rxmulti;    /* dot11MulticastReceivedFrameCount */
	uint32      rxcrc;      /* dot11FCSErrorCount */
	uint32      txfrmsnt;   /* dot11TransmittedFrameCount */
	uint32      rxundec;    /* dot11WEPUndecryptableCount */
} d11cnt_t;

/* OUI for BRCM proprietary IE */
#define BRCM_PROP_OUI       "\x00\x90\x4C"  /* Broadcom proprietary OUI */

#ifndef LINUX_POSTMOGRIFY_REMOVAL
/* The following BRCM_PROP_OUI types are currently in use (defined in
 * relevant subsections). Each of them will be in a separate proprietary(221) IE
 * #define SES_VNDR_IE_TYPE 1   (defined in src/ses/shared/ses.h)
 * #define DPT_IE_TYPE      2
 * #define HT_CAP_IE_TYPE   51
 * #define HT_ADD_IE_TYPE   52
 * #define BRCM_EXTCH_IE_TYPE   53
 */

/* Following is the generic structure for brcm_prop_ie (uses BRCM_PROP_OUI).
 * DPT uses this format with type set to DPT_IE_TYPE
 */
BWL_PRE_PACKED_STRUCT struct brcm_prop_ie_s {
	uint8 id;       /* IE ID, 221, DOT11_MNG_PROPR_ID */
	uint8 len;      /* IE length */
	uint8 oui[3];       /* Proprietary OUI, BRCM_PROP_OUI */
	uint8 type;     /* type of this IE */
	uint16 cap;     /* DPT capabilities */
} BWL_POST_PACKED_STRUCT;
typedef struct brcm_prop_ie_s brcm_prop_ie_t;

#define BRCM_PROP_IE_LEN    6   /* len of fixed part of brcm_prop ie */

#define DPT_IE_TYPE     2
#endif /* LINUX_POSTMOGRIFY_REMOVAL */

/* BRCM OUI: Used in the proprietary(221) IE in all broadcom devices */
#define BRCM_OUI        "\x00\x10\x18"  /* Broadcom OUI */

/* BRCM info element */
BWL_PRE_PACKED_STRUCT struct brcm_ie {
	uint8   id;     /* IE ID, 221, DOT11_MNG_PROPR_ID */
	uint8   len;        /* IE length */
	uint8   oui[3];     /* Proprietary OUI, BRCM_OUI */
	uint8   ver;        /* type/ver of this IE */
	uint8   assoc;      /* # of assoc STAs */
	uint8   flags;      /* misc flags */
	uint8   flags1;     /* misc flags */
	uint16  amsdu_mtu_pref; /* preferred A-MSDU MTU */
} BWL_POST_PACKED_STRUCT;
typedef struct brcm_ie brcm_ie_t;
#define BRCM_IE_LEN     11  /* BRCM IE length */
#define BRCM_IE_VER     2   /* BRCM IE version */
#define BRCM_IE_LEGACY_AES_VER  1   /* BRCM IE legacy AES version */

/* brcm_ie flags */
#ifdef WLAFTERBURNER
#define BRF_ABCAP       0x1 /* afterburner capable */
#define BRF_ABRQRD      0x2 /* afterburner requested */
#define BRF_ABCOUNTER_MASK  0xf0    /* afterburner wds "state" counter */
#define BRF_ABCOUNTER_SHIFT 4   /* offset of afterburner wds "state" counter */
#endif /* WLAFTERBURNER */
#define BRF_LZWDS       0x4 /* lazy wds enabled */
#define BRF_BLOCKACK        0x8 /* BlockACK capable */

/* brcm_ie flags1 */
#define BRF1_AMSDU      0x1 /* A-MSDU capable */
#define BRF1_WMEPS      0x4 /* AP is capable of handling WME + PS w/o APSD */
#define BRF1_PSOFIX     0x8 /* AP has fixed PS mode out-of-order packets */
#define BRF1_RX_LARGE_AGG   0x10    /* device can rx large aggregates */
#define BRF1_SOFTAP             0x40    /* Configure as Broadcom SOFTAP */

#ifdef WLAFTERBURNER
#define AB_WDS_TIMEOUT_MAX  15  /* AB wds Max count indicating not locally capable */
#define AB_WDS_TIMEOUT_MIN  1   /* AB wds, use zero count as indicating "downrev" */
#endif

#define AB_GUARDCOUNT   10      /* seconds, time to swtich ab <--> 11n */

/* Vendor IE structure */
BWL_PRE_PACKED_STRUCT struct vndr_ie {
	uchar id;
	uchar len;
	uchar oui [3];
	uchar data [1];     /* Variable size data */
} BWL_POST_PACKED_STRUCT;
typedef struct vndr_ie vndr_ie_t;

#define VNDR_IE_HDR_LEN     2   /* id + len field */
#define VNDR_IE_MIN_LEN     3   /* size of the oui field */
#define VNDR_IE_MAX_LEN     256 /* verdor IE max length */

/* ************* HT definitions. ************* */
#define MCSSET_LEN  16  /* 16-bits per 8-bit set to give 128-bits bitmap of MCS Index */
#define MAX_MCS_NUM (128)   /* max mcs number = 128 */

BWL_PRE_PACKED_STRUCT struct ht_cap_ie {
	uint16  cap;
	uint8   params;
	uint8   supp_mcs[MCSSET_LEN];
	uint16  ext_htcap;
	uint32  txbf_cap;
	uint8   as_cap;
} BWL_POST_PACKED_STRUCT;
typedef struct ht_cap_ie ht_cap_ie_t;

/* CAP IE: HT 1.0 spec. simply stole a 802.11 IE, we use our prop. IE until this is resolved */
/* the capability IE is primarily used to convey this nodes abilities */
BWL_PRE_PACKED_STRUCT struct ht_prop_cap_ie {
	uint8   id;     /* IE ID, 221, DOT11_MNG_PROPR_ID */
	uint8   len;        /* IE length */
	uint8   oui[3];     /* Proprietary OUI, BRCM_PROP_OUI */
	uint8   type;           /* type inidicates what follows */
	ht_cap_ie_t cap_ie;
} BWL_POST_PACKED_STRUCT;
typedef struct ht_prop_cap_ie ht_prop_cap_ie_t;

#define HT_PROP_IE_OVERHEAD 4   /* overhead bytes for prop oui ie */
#define HT_CAP_IE_LEN       26  /* HT capability len (based on .11n d2.0) */
#define HT_CAP_IE_TYPE      51

#define HT_CAP_LDPC_CODING  0x0001  /* Support for rx of LDPC coded pkts */
#define HT_CAP_40MHZ        0x0002  /* FALSE:20Mhz, TRUE:20/40MHZ supported */
#define HT_CAP_MIMO_PS_MASK 0x000C  /* Mimo PS mask */
#define HT_CAP_MIMO_PS_SHIFT    0x0002  /* Mimo PS shift */
#define HT_CAP_MIMO_PS_OFF  0x0003  /* Mimo PS, no restriction */
#define HT_CAP_MIMO_PS_RTS  0x0001  /* Mimo PS, send RTS/CTS around MIMO frames */
#define HT_CAP_MIMO_PS_ON   0x0000  /* Mimo PS, MIMO disallowed */
#define HT_CAP_GF       0x0010  /* Greenfield preamble support */
#define HT_CAP_SHORT_GI_20  0x0020  /* 20MHZ short guard interval support */
#define HT_CAP_SHORT_GI_40  0x0040  /* 40Mhz short guard interval support */
#define HT_CAP_TX_STBC      0x0080  /* Tx STBC support */
#define HT_CAP_RX_STBC_MASK 0x0300  /* Rx STBC mask */
#define HT_CAP_RX_STBC_SHIFT    8   /* Rx STBC shift */
#define HT_CAP_DELAYED_BA   0x0400  /* delayed BA support */
#define HT_CAP_MAX_AMSDU    0x0800  /* Max AMSDU size in bytes , 0=3839, 1=7935 */
#define HT_CAP_DSSS_CCK 0x1000  /* DSSS/CCK supported by the BSS */
#define HT_CAP_PSMP     0x2000  /* Power Save Multi Poll support */
#define HT_CAP_40MHZ_INTOLERANT 0x4000  /* 40MHz Intolerant */
#define HT_CAP_LSIG_TXOP    0x8000  /* L-SIG TXOP protection support */

#define HT_CAP_RX_STBC_NO       0x0 /* no rx STBC support */
#define HT_CAP_RX_STBC_ONE_STREAM   0x1 /* rx STBC support of 1 spatial stream */
#define HT_CAP_RX_STBC_TWO_STREAM   0x2 /* rx STBC support of 1-2 spatial streams */
#define HT_CAP_RX_STBC_THREE_STREAM 0x3 /* rx STBC support of 1-3 spatial streams */

#define HT_MAX_AMSDU        7935    /* max amsdu size (bytes) per the HT spec */
#define HT_MIN_AMSDU        3835    /* min amsdu size (bytes) per the HT spec */

#define HT_PARAMS_RX_FACTOR_MASK    0x03    /* ampdu rcv factor mask */
#define HT_PARAMS_DENSITY_MASK      0x1C    /* ampdu density mask */
#define HT_PARAMS_DENSITY_SHIFT 2   /* ampdu density shift */

/* HT/AMPDU specific define */
#define AMPDU_MAX_MPDU_DENSITY  7   /* max mpdu density; in 1/8 usec units */
#define AMPDU_RX_FACTOR_8K  0   /* max rcv ampdu len (8kb) */
#define AMPDU_RX_FACTOR_16K 1   /* max rcv ampdu len (16kb) */
#define AMPDU_RX_FACTOR_32K 2   /* max rcv ampdu len (32kb) */
#define AMPDU_RX_FACTOR_64K 3   /* max rcv ampdu len (64kb) */
#define AMPDU_RX_FACTOR_BASE    8*1024  /* ampdu factor base for rx len */

#define AMPDU_DELIMITER_LEN 4   /* length of ampdu delimiter */
#define AMPDU_DELIMITER_LEN_MAX 63  /* max length of ampdu delimiter(enforced in HW) */

BWL_PRE_PACKED_STRUCT struct ht_add_ie {
	uint8   ctl_ch;         /* control channel number */
	uint8   byte1;          /* ext ch,rec. ch. width, RIFS support */
	uint16  opmode;         /* operation mode */
	uint16  misc_bits;      /* misc bits */
	uint8   basic_mcs[MCSSET_LEN];  /* required MCS set */
} BWL_POST_PACKED_STRUCT;
typedef struct ht_add_ie ht_add_ie_t;

/* ADD IE: HT 1.0 spec. simply stole a 802.11 IE, we use our prop. IE until this is resolved */
/* the additional IE is primarily used to convey the current BSS configuration */
BWL_PRE_PACKED_STRUCT struct ht_prop_add_ie {
	uint8   id;     /* IE ID, 221, DOT11_MNG_PROPR_ID */
	uint8   len;        /* IE length */
	uint8   oui[3];     /* Proprietary OUI, BRCM_PROP_OUI */
	uint8   type;       /* indicates what follows */
	ht_add_ie_t add_ie;
} BWL_POST_PACKED_STRUCT;
typedef struct ht_prop_add_ie ht_prop_add_ie_t;

#define HT_ADD_IE_LEN   22
#define HT_ADD_IE_TYPE  52

/* byte1 defn's */
#define HT_BW_ANY       0x04    /* set, STA can use 20 or 40MHz */
#define HT_RIFS_PERMITTED       0x08    /* RIFS allowed */

/* opmode defn's */
#define HT_OPMODE_MASK          0x0003  /* protection mode mask */
#define HT_OPMODE_SHIFT     0   /* protection mode shift */
#define HT_OPMODE_PURE      0x0000  /* protection mode PURE */
#define HT_OPMODE_OPTIONAL  0x0001  /* protection mode optional */
#define HT_OPMODE_HT20IN40  0x0002  /* protection mode 20MHz HT in 40MHz BSS */
#define HT_OPMODE_MIXED 0x0003  /* protection mode Mixed Mode */
#define HT_OPMODE_NONGF 0x0004  /* protection mode non-GF */
#define DOT11N_TXBURST      0x0008  /* Tx burst limit */
#define DOT11N_OBSS_NONHT   0x0010  /* OBSS Non-HT STA present */

/* misc_bites defn's */
#define HT_BASIC_STBC_MCS   0x007f  /* basic STBC MCS */
#define HT_DUAL_STBC_PROT   0x0080  /* Dual STBC Protection */
#define HT_SECOND_BCN       0x0100  /* Secondary beacon support */
#define HT_LSIG_TXOP        0x0200  /* L-SIG TXOP Protection full support */
#define HT_PCO_ACTIVE       0x0400  /* PCO active */
#define HT_PCO_PHASE        0x0800  /* PCO phase */

/* Tx Burst Limits */
#define DOT11N_2G_TXBURST_LIMIT 6160    /* 2G band Tx burst limit per 802.11n Draft 1.10 (usec) */
#define DOT11N_5G_TXBURST_LIMIT 3080    /* 5G band Tx burst limit per 802.11n Draft 1.10 (usec) */

/* Macros for opmode */
#define GET_HT_OPMODE(add_ie)       ((ltoh16_ua(&add_ie->opmode) & HT_OPMODE_MASK) \
					>> HT_OPMODE_SHIFT)
#define HT_MIXEDMODE_PRESENT(add_ie)    ((ltoh16_ua(&add_ie->opmode) & HT_OPMODE_MASK) \
					== HT_OPMODE_MIXED) /* mixed mode present */
#define HT_HT20_PRESENT(add_ie) ((ltoh16_ua(&add_ie->opmode) & HT_OPMODE_MASK) \
					== HT_OPMODE_HT20IN40)  /* 20MHz HT present */
#define HT_OPTIONAL_PRESENT(add_ie) ((ltoh16_ua(&add_ie->opmode) & HT_OPMODE_MASK) \
					== HT_OPMODE_OPTIONAL)  /* Optional protection present */
#define HT_USE_PROTECTION(add_ie)   (HT_HT20_PRESENT((add_ie)) || \
					HT_MIXEDMODE_PRESENT((add_ie))) /* use protection */
#define HT_NONGF_PRESENT(add_ie)    ((ltoh16_ua(&add_ie->opmode) & HT_OPMODE_NONGF) \
					== HT_OPMODE_NONGF) /* non-GF present */
#define DOT11N_TXBURST_PRESENT(add_ie)  ((ltoh16_ua(&add_ie->opmode) & DOT11N_TXBURST) \
					== DOT11N_TXBURST)  /* Tx Burst present */
#define DOT11N_OBSS_NONHT_PRESENT(add_ie)   ((ltoh16_ua(&add_ie->opmode) & DOT11N_OBSS_NONHT) \
					== DOT11N_OBSS_NONHT)   /* OBSS Non-HT present */

BWL_PRE_PACKED_STRUCT struct obss_params {
	uint16  passive_dwell;
	uint16  active_dwell;
	uint16  bss_widthscan_interval;
	uint16  passive_total;
	uint16  active_total;
	uint16  chanwidth_transition_dly;
	uint16  activity_threshold;
} BWL_POST_PACKED_STRUCT;
typedef struct obss_params obss_params_t;

BWL_PRE_PACKED_STRUCT struct dot11_obss_ie {
	uint8   id;
	uint8   len;
	obss_params_t obss_params;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_obss_ie dot11_obss_ie_t;
#define DOT11_OBSS_SCAN_IE_LEN  sizeof(obss_params_t)   /* HT OBSS len (based on 802.11n d3.0) */

/* HT control field */
#define HT_CTRL_LA_TRQ      0x00000002  /* sounding request */
#define HT_CTRL_LA_MAI      0x0000003C  /* MCS request or antenna selection indication */
#define HT_CTRL_LA_MAI_SHIFT    2
#define HT_CTRL_LA_MAI_MRQ  0x00000004  /* MCS request */
#define HT_CTRL_LA_MAI_MSI  0x00000038  /* MCS request sequence identifier */
#define HT_CTRL_LA_MFSI     0x000001C0  /* MFB sequence identifier */
#define HT_CTRL_LA_MFSI_SHIFT   6
#define HT_CTRL_LA_MFB_ASELC    0x0000FE00  /* MCS feedback, antenna selection command/data */
#define HT_CTRL_LA_MFB_ASELC_SH 9
#define HT_CTRL_LA_ASELC_CMD    0x00000C00  /* ASEL command */
#define HT_CTRL_LA_ASELC_DATA   0x0000F000  /* ASEL data */
#define HT_CTRL_CAL_POS     0x00030000  /* Calibration position */
#define HT_CTRL_CAL_SEQ     0x000C0000  /* Calibration sequence */
#define HT_CTRL_CSI_STEERING    0x00C00000  /* CSI/Steering */
#define HT_CTRL_CSI_STEER_SHIFT 22
#define HT_CTRL_CSI_STEER_NFB   0       /* no fedback required */
#define HT_CTRL_CSI_STEER_CSI   1       /* CSI, H matrix */
#define HT_CTRL_CSI_STEER_NCOM  2       /* non-compressed beamforming */
#define HT_CTRL_CSI_STEER_COM   3       /* compressed beamforming */
#define HT_CTRL_NDP_ANNOUNCE    0x01000000  /* NDP announcement */
#define HT_CTRL_AC_CONSTRAINT   0x40000000  /* AC Constraint */
#define HT_CTRL_RDG_MOREPPDU    0x80000000  /* RDG/More PPDU */

#define HT_OPMODE_OPTIONAL  0x0001  /* protection mode optional */
#define HT_OPMODE_HT20IN40  0x0002  /* protection mode 20MHz HT in 40MHz BSS */
#define HT_OPMODE_MIXED 0x0003  /* protection mode Mixed Mode */
#define HT_OPMODE_NONGF 0x0004  /* protection mode non-GF */
#define DOT11N_TXBURST      0x0008  /* Tx burst limit */
#define DOT11N_OBSS_NONHT   0x0010  /* OBSS Non-HT STA present */


/* ************* WPA definitions. ************* */
#define WPA_OUI         "\x00\x50\xF2"  /* WPA OUI */
#define WPA_OUI_LEN     3       /* WPA OUI length */
#define WPA_OUI_TYPE        1
#define WPA_VERSION     1   /* WPA version */
#define WPA2_OUI        "\x00\x0F\xAC"  /* WPA2 OUI */
#define WPA2_OUI_LEN        3       /* WPA2 OUI length */
#define WPA2_VERSION        1   /* WPA2 version */
#define WPA2_VERSION_LEN    2   /* WAP2 version length */

/* ************* WPS definitions. ************* */
#define WPS_OUI         "\x00\x50\xF2"  /* WPS OUI */
#define WPS_OUI_LEN     3       /* WPS OUI length */
#define WPS_OUI_TYPE        4

/* ************* WFA definitions. ************* */
#define WFA_OUI         "\x50\x6F\x9A"  /* WFA OUI */
#define WFA_OUI_LEN 3   /* WFA OUI length */

#define WFA_OUI_TYPE_WPA    1
#define WFA_OUI_TYPE_WPS    4
#define WFA_OUI_TYPE_TPC    8
#define WFA_OUI_TYPE_P2P    9

/* RSN authenticated key managment suite */
#define RSN_AKM_NONE        0   /* None (IBSS) */
#define RSN_AKM_UNSPECIFIED 1   /* Over 802.1x */
#define RSN_AKM_PSK     2   /* Pre-shared Key */
#define RSN_AKM_FBT_1X      3   /* Fast Bss transition using 802.1X */
#define RSN_AKM_FBT_PSK     4   /* Fast Bss transition using Pre-shared Key */

/* Key related defines */
#define DOT11_MAX_DEFAULT_KEYS  4   /* number of default keys */
#define DOT11_MAX_KEY_SIZE  32  /* max size of any key */
#define DOT11_MAX_IV_SIZE   16  /* max size of any IV */
#define DOT11_EXT_IV_FLAG   (1<<5)  /* flag to indicate IV is > 4 bytes */
#define DOT11_WPA_KEY_RSC_LEN   8       /* WPA RSC key len */

#define WEP1_KEY_SIZE       5   /* max size of any WEP key */
#define WEP1_KEY_HEX_SIZE   10  /* size of WEP key in hex. */
#define WEP128_KEY_SIZE     13  /* max size of any WEP key */
#define WEP128_KEY_HEX_SIZE 26  /* size of WEP key in hex. */
#define TKIP_MIC_SIZE       8   /* size of TKIP MIC */
#define TKIP_EOM_SIZE       7   /* max size of TKIP EOM */
#define TKIP_EOM_FLAG       0x5a    /* TKIP EOM flag byte */
#define TKIP_KEY_SIZE       32  /* size of any TKIP key */
#define TKIP_MIC_AUTH_TX    16  /* offset to Authenticator MIC TX key */
#define TKIP_MIC_AUTH_RX    24  /* offset to Authenticator MIC RX key */
#define TKIP_MIC_SUP_RX     TKIP_MIC_AUTH_TX    /* offset to Supplicant MIC RX key */
#define TKIP_MIC_SUP_TX     TKIP_MIC_AUTH_RX    /* offset to Supplicant MIC TX key */
#define AES_KEY_SIZE        16  /* size of AES key */
#define AES_MIC_SIZE        8   /* size of AES MIC */

/* WCN */
#define WCN_OUI         "\x00\x50\xf2"  /* WCN OUI */
#define WCN_TYPE        4   /* WCN type */


/* 802.11r protocol definitions */

/* Mobility Domain IE */
BWL_PRE_PACKED_STRUCT struct dot11_mdid_ie {
	uint8 id;
	uint8 len;
	uint16 mdid;        /* Mobility Domain Id */
	uint8 cap;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_mdid_ie dot11_mdid_ie_t;

#define FBT_MDID_CAP_OVERDS 0x01    /* Fast Bss transition over the DS support */
#define FBT_MDID_CAP_RRP    0x02    /* Resource request protocol support */

/* Fast Bss Transition IE */
BWL_PRE_PACKED_STRUCT struct dot11_ft_ie {
	uint8 id;
	uint8 len;
	uint16 mic_control;     /* Mic Control */
	uint8 mic[16];
	uint8 anonce[32];
	uint8 snonce[32];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_ft_ie dot11_ft_ie_t;

/* GTK ie */
BWL_PRE_PACKED_STRUCT struct dot11_gtk_ie {
	uint8 id;
	uint8 len;
	uint16 key_info;
	uint8 key_len;
	uint8 rsc[8];
	uint8 data[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_gtk_ie dot11_gtk_ie_t;


/* This marks the end of a packed structure section. */
#include <packed_section_end.h>

#endif /* _802_11_H_ */
