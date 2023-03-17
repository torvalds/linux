/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (c) 2008-2011, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * Author: Lucy Liu <lucy.liu@intel.com>
 */

#ifndef __LINUX_DCBNL_H__
#define __LINUX_DCBNL_H__

#include <linux/types.h>

/* IEEE 802.1Qaz std supported values */
#define IEEE_8021QAZ_MAX_TCS	8

#define IEEE_8021QAZ_TSA_STRICT		0
#define IEEE_8021QAZ_TSA_CB_SHAPER	1
#define IEEE_8021QAZ_TSA_ETS		2
#define IEEE_8021QAZ_TSA_VENDOR		255

/* This structure contains the IEEE 802.1Qaz ETS managed object
 *
 * @willing: willing bit in ETS configuration TLV
 * @ets_cap: indicates supported capacity of ets feature
 * @cbs: credit based shaper ets algorithm supported
 * @tc_tx_bw: tc tx bandwidth indexed by traffic class
 * @tc_rx_bw: tc rx bandwidth indexed by traffic class
 * @tc_tsa: TSA Assignment table, indexed by traffic class
 * @prio_tc: priority assignment table mapping 8021Qp to traffic class
 * @tc_reco_bw: recommended tc bandwidth indexed by traffic class for TLV
 * @tc_reco_tsa: recommended tc bandwidth indexed by traffic class for TLV
 * @reco_prio_tc: recommended tc tx bandwidth indexed by traffic class for TLV
 *
 * Recommended values are used to set fields in the ETS recommendation TLV
 * with hardware offloaded LLDP.
 *
 * ----
 *  TSA Assignment 8 bit identifiers
 *	0	strict priority
 *	1	credit-based shaper
 *	2	enhanced transmission selection
 *	3-254	reserved
 *	255	vendor specific
 */
struct ieee_ets {
	__u8	willing;
	__u8	ets_cap;
	__u8	cbs;
	__u8	tc_tx_bw[IEEE_8021QAZ_MAX_TCS];
	__u8	tc_rx_bw[IEEE_8021QAZ_MAX_TCS];
	__u8	tc_tsa[IEEE_8021QAZ_MAX_TCS];
	__u8	prio_tc[IEEE_8021QAZ_MAX_TCS];
	__u8	tc_reco_bw[IEEE_8021QAZ_MAX_TCS];
	__u8	tc_reco_tsa[IEEE_8021QAZ_MAX_TCS];
	__u8	reco_prio_tc[IEEE_8021QAZ_MAX_TCS];
};

/* This structure contains rate limit extension to the IEEE 802.1Qaz ETS
 * managed object.
 * Values are 64 bits long and specified in Kbps to enable usage over both
 * slow and very fast networks.
 *
 * @tc_maxrate: maximal tc tx bandwidth indexed by traffic class
 */
struct ieee_maxrate {
	__u64	tc_maxrate[IEEE_8021QAZ_MAX_TCS];
};

enum dcbnl_cndd_states {
	DCB_CNDD_RESET = 0,
	DCB_CNDD_EDGE,
	DCB_CNDD_INTERIOR,
	DCB_CNDD_INTERIOR_READY,
};

/* This structure contains the IEEE 802.1Qau QCN managed object.
 *
 *@rpg_enable: enable QCN RP
 *@rppp_max_rps: maximum number of RPs allowed for this CNPV on this port
 *@rpg_time_reset: time between rate increases if no CNMs received.
 *		   given in u-seconds
 *@rpg_byte_reset: transmitted data between rate increases if no CNMs received.
 *		   given in Bytes
 *@rpg_threshold: The number of times rpByteStage or rpTimeStage can count
 *		   before RP rate control state machine advances states
 *@rpg_max_rate: the maxinun rate, in Mbits per second,
 *		 at which an RP can transmit
 *@rpg_ai_rate: The rate, in Mbits per second,
 *		used to increase rpTargetRate in the RPR_ACTIVE_INCREASE
 *@rpg_hai_rate: The rate, in Mbits per second,
 *		 used to increase rpTargetRate in the RPR_HYPER_INCREASE state
 *@rpg_gd: Upon CNM receive, flow rate is limited to (Fb/Gd)*CurrentRate.
 *	   rpgGd is given as log2(Gd), where Gd may only be powers of 2
 *@rpg_min_dec_fac: The minimum factor by which the current transmit rate
 *		    can be changed by reception of a CNM.
 *		    value is given as percentage (1-100)
 *@rpg_min_rate: The minimum value, in bits per second, for rate to limit
 *@cndd_state_machine: The state of the congestion notification domain
 *		       defense state machine, as defined by IEEE 802.3Qau
 *		       section 32.1.1. In the interior ready state,
 *		       the QCN capable hardware may add CN-TAG TLV to the
 *		       outgoing traffic, to specifically identify outgoing
 *		       flows.
 */

struct ieee_qcn {
	__u8 rpg_enable[IEEE_8021QAZ_MAX_TCS];
	__u32 rppp_max_rps[IEEE_8021QAZ_MAX_TCS];
	__u32 rpg_time_reset[IEEE_8021QAZ_MAX_TCS];
	__u32 rpg_byte_reset[IEEE_8021QAZ_MAX_TCS];
	__u32 rpg_threshold[IEEE_8021QAZ_MAX_TCS];
	__u32 rpg_max_rate[IEEE_8021QAZ_MAX_TCS];
	__u32 rpg_ai_rate[IEEE_8021QAZ_MAX_TCS];
	__u32 rpg_hai_rate[IEEE_8021QAZ_MAX_TCS];
	__u32 rpg_gd[IEEE_8021QAZ_MAX_TCS];
	__u32 rpg_min_dec_fac[IEEE_8021QAZ_MAX_TCS];
	__u32 rpg_min_rate[IEEE_8021QAZ_MAX_TCS];
	__u32 cndd_state_machine[IEEE_8021QAZ_MAX_TCS];
};

/* This structure contains the IEEE 802.1Qau QCN statistics.
 *
 *@rppp_rp_centiseconds: the number of RP-centiseconds accumulated
 *			 by RPs at this priority level on this Port
 *@rppp_created_rps: number of active RPs(flows) that react to CNMs
 */

struct ieee_qcn_stats {
	__u64 rppp_rp_centiseconds[IEEE_8021QAZ_MAX_TCS];
	__u32 rppp_created_rps[IEEE_8021QAZ_MAX_TCS];
};

/* This structure contains the IEEE 802.1Qaz PFC managed object
 *
 * @pfc_cap: Indicates the number of traffic classes on the local device
 *	     that may simultaneously have PFC enabled.
 * @pfc_en: bitmap indicating pfc enabled traffic classes
 * @mbc: enable macsec bypass capability
 * @delay: the allowance made for a round-trip propagation delay of the
 *	   link in bits.
 * @requests: count of the sent pfc frames
 * @indications: count of the received pfc frames
 */
struct ieee_pfc {
	__u8	pfc_cap;
	__u8	pfc_en;
	__u8	mbc;
	__u16	delay;
	__u64	requests[IEEE_8021QAZ_MAX_TCS];
	__u64	indications[IEEE_8021QAZ_MAX_TCS];
};

#define IEEE_8021Q_MAX_PRIORITIES 8
#define DCBX_MAX_BUFFERS  8
struct dcbnl_buffer {
	/* priority to buffer mapping */
	__u8    prio2buffer[IEEE_8021Q_MAX_PRIORITIES];
	/* buffer size in Bytes */
	__u32   buffer_size[DCBX_MAX_BUFFERS];
	__u32   total_size;
};

/* CEE DCBX std supported values */
#define CEE_DCBX_MAX_PGS	8
#define CEE_DCBX_MAX_PRIO	8

/**
 * struct cee_pg - CEE Priority-Group managed object
 *
 * @willing: willing bit in the PG tlv
 * @error: error bit in the PG tlv
 * @pg_en: enable bit of the PG feature
 * @tcs_supported: number of traffic classes supported
 * @pg_bw: bandwidth percentage for each priority group
 * @prio_pg: priority to PG mapping indexed by priority
 */
struct cee_pg {
	__u8    willing;
	__u8    error;
	__u8    pg_en;
	__u8    tcs_supported;
	__u8    pg_bw[CEE_DCBX_MAX_PGS];
	__u8    prio_pg[CEE_DCBX_MAX_PGS];
};

/**
 * struct cee_pfc - CEE PFC managed object
 *
 * @willing: willing bit in the PFC tlv
 * @error: error bit in the PFC tlv
 * @pfc_en: bitmap indicating pfc enabled traffic classes
 * @tcs_supported: number of traffic classes supported
 */
struct cee_pfc {
	__u8    willing;
	__u8    error;
	__u8    pfc_en;
	__u8    tcs_supported;
};

/* IEEE 802.1Qaz std supported values */
#define IEEE_8021QAZ_APP_SEL_ETHERTYPE	1
#define IEEE_8021QAZ_APP_SEL_STREAM	2
#define IEEE_8021QAZ_APP_SEL_DGRAM	3
#define IEEE_8021QAZ_APP_SEL_ANY	4
#define IEEE_8021QAZ_APP_SEL_DSCP       5

/* Non-std selector values */
#define DCB_APP_SEL_PCP 255

/* This structure contains the IEEE 802.1Qaz APP managed object. This
 * object is also used for the CEE std as well.
 *
 * @selector: protocol identifier type
 * @protocol: protocol of type indicated
 * @priority: 3-bit unsigned integer indicating priority for IEEE
 *            8-bit 802.1p user priority bitmap for CEE
 *
 * ----
 *  Selector field values for IEEE 802.1Qaz
 *	0	Reserved
 *	1	Ethertype
 *	2	Well known port number over TCP or SCTP
 *	3	Well known port number over UDP or DCCP
 *	4	Well known port number over TCP, SCTP, UDP, or DCCP
 *	5	Differentiated Services Code Point (DSCP) value
 *	6-7	Reserved
 *
 *  Selector field values for CEE
 *	0	Ethertype
 *	1	Well known port number over TCP or UDP
 *	2-3	Reserved
 */
struct dcb_app {
	__u8	selector;
	__u8	priority;
	__u16	protocol;
};

#define IEEE_8021QAZ_APP_SEL_MAX 255

/**
 * struct dcb_peer_app_info - APP feature information sent by the peer
 *
 * @willing: willing bit in the peer APP tlv
 * @error: error bit in the peer APP tlv
 *
 * In addition to this information the full peer APP tlv also contains
 * a table of 'app_count' APP objects defined above.
 */
struct dcb_peer_app_info {
	__u8	willing;
	__u8	error;
};

struct dcbmsg {
	__u8               dcb_family;
	__u8               cmd;
	__u16              dcb_pad;
};

/**
 * enum dcbnl_commands - supported DCB commands
 *
 * @DCB_CMD_UNDEFINED: unspecified command to catch errors
 * @DCB_CMD_GSTATE: request the state of DCB in the device
 * @DCB_CMD_SSTATE: set the state of DCB in the device
 * @DCB_CMD_PGTX_GCFG: request the priority group configuration for Tx
 * @DCB_CMD_PGTX_SCFG: set the priority group configuration for Tx
 * @DCB_CMD_PGRX_GCFG: request the priority group configuration for Rx
 * @DCB_CMD_PGRX_SCFG: set the priority group configuration for Rx
 * @DCB_CMD_PFC_GCFG: request the priority flow control configuration
 * @DCB_CMD_PFC_SCFG: set the priority flow control configuration
 * @DCB_CMD_SET_ALL: apply all changes to the underlying device
 * @DCB_CMD_GPERM_HWADDR: get the permanent MAC address of the underlying
 *                        device.  Only useful when using bonding.
 * @DCB_CMD_GCAP: request the DCB capabilities of the device
 * @DCB_CMD_GNUMTCS: get the number of traffic classes currently supported
 * @DCB_CMD_SNUMTCS: set the number of traffic classes
 * @DCB_CMD_GBCN: set backward congestion notification configuration
 * @DCB_CMD_SBCN: get backward congestion notification configuration.
 * @DCB_CMD_GAPP: get application protocol configuration
 * @DCB_CMD_SAPP: set application protocol configuration
 * @DCB_CMD_IEEE_SET: set IEEE 802.1Qaz configuration
 * @DCB_CMD_IEEE_GET: get IEEE 802.1Qaz configuration
 * @DCB_CMD_GDCBX: get DCBX engine configuration
 * @DCB_CMD_SDCBX: set DCBX engine configuration
 * @DCB_CMD_GFEATCFG: get DCBX features flags
 * @DCB_CMD_SFEATCFG: set DCBX features negotiation flags
 * @DCB_CMD_CEE_GET: get CEE aggregated configuration
 * @DCB_CMD_IEEE_DEL: delete IEEE 802.1Qaz configuration
 */
enum dcbnl_commands {
	DCB_CMD_UNDEFINED,

	DCB_CMD_GSTATE,
	DCB_CMD_SSTATE,

	DCB_CMD_PGTX_GCFG,
	DCB_CMD_PGTX_SCFG,
	DCB_CMD_PGRX_GCFG,
	DCB_CMD_PGRX_SCFG,

	DCB_CMD_PFC_GCFG,
	DCB_CMD_PFC_SCFG,

	DCB_CMD_SET_ALL,

	DCB_CMD_GPERM_HWADDR,

	DCB_CMD_GCAP,

	DCB_CMD_GNUMTCS,
	DCB_CMD_SNUMTCS,

	DCB_CMD_PFC_GSTATE,
	DCB_CMD_PFC_SSTATE,

	DCB_CMD_BCN_GCFG,
	DCB_CMD_BCN_SCFG,

	DCB_CMD_GAPP,
	DCB_CMD_SAPP,

	DCB_CMD_IEEE_SET,
	DCB_CMD_IEEE_GET,

	DCB_CMD_GDCBX,
	DCB_CMD_SDCBX,

	DCB_CMD_GFEATCFG,
	DCB_CMD_SFEATCFG,

	DCB_CMD_CEE_GET,
	DCB_CMD_IEEE_DEL,

	__DCB_CMD_ENUM_MAX,
	DCB_CMD_MAX = __DCB_CMD_ENUM_MAX - 1,
};

/**
 * enum dcbnl_attrs - DCB top-level netlink attributes
 *
 * @DCB_ATTR_UNDEFINED: unspecified attribute to catch errors
 * @DCB_ATTR_IFNAME: interface name of the underlying device (NLA_STRING)
 * @DCB_ATTR_STATE: enable state of DCB in the device (NLA_U8)
 * @DCB_ATTR_PFC_STATE: enable state of PFC in the device (NLA_U8)
 * @DCB_ATTR_PFC_CFG: priority flow control configuration (NLA_NESTED)
 * @DCB_ATTR_NUM_TC: number of traffic classes supported in the device (NLA_U8)
 * @DCB_ATTR_PG_CFG: priority group configuration (NLA_NESTED)
 * @DCB_ATTR_SET_ALL: bool to commit changes to hardware or not (NLA_U8)
 * @DCB_ATTR_PERM_HWADDR: MAC address of the physical device (NLA_NESTED)
 * @DCB_ATTR_CAP: DCB capabilities of the device (NLA_NESTED)
 * @DCB_ATTR_NUMTCS: number of traffic classes supported (NLA_NESTED)
 * @DCB_ATTR_BCN: backward congestion notification configuration (NLA_NESTED)
 * @DCB_ATTR_IEEE: IEEE 802.1Qaz supported attributes (NLA_NESTED)
 * @DCB_ATTR_DCBX: DCBX engine configuration in the device (NLA_U8)
 * @DCB_ATTR_FEATCFG: DCBX features flags (NLA_NESTED)
 * @DCB_ATTR_CEE: CEE std supported attributes (NLA_NESTED)
 */
enum dcbnl_attrs {
	DCB_ATTR_UNDEFINED,

	DCB_ATTR_IFNAME,
	DCB_ATTR_STATE,
	DCB_ATTR_PFC_STATE,
	DCB_ATTR_PFC_CFG,
	DCB_ATTR_NUM_TC,
	DCB_ATTR_PG_CFG,
	DCB_ATTR_SET_ALL,
	DCB_ATTR_PERM_HWADDR,
	DCB_ATTR_CAP,
	DCB_ATTR_NUMTCS,
	DCB_ATTR_BCN,
	DCB_ATTR_APP,

	/* IEEE std attributes */
	DCB_ATTR_IEEE,

	DCB_ATTR_DCBX,
	DCB_ATTR_FEATCFG,

	/* CEE nested attributes */
	DCB_ATTR_CEE,

	__DCB_ATTR_ENUM_MAX,
	DCB_ATTR_MAX = __DCB_ATTR_ENUM_MAX - 1,
};

/**
 * enum ieee_attrs - IEEE 802.1Qaz get/set attributes
 *
 * @DCB_ATTR_IEEE_UNSPEC: unspecified
 * @DCB_ATTR_IEEE_ETS: negotiated ETS configuration
 * @DCB_ATTR_IEEE_PFC: negotiated PFC configuration
 * @DCB_ATTR_IEEE_APP_TABLE: negotiated APP configuration
 * @DCB_ATTR_IEEE_PEER_ETS: peer ETS configuration - get only
 * @DCB_ATTR_IEEE_PEER_PFC: peer PFC configuration - get only
 * @DCB_ATTR_IEEE_PEER_APP: peer APP tlv - get only
 * @DCB_ATTR_DCB_APP_TRUST_TABLE: selector trust table
 * @DCB_ATTR_DCB_REWR_TABLE: rewrite configuration
 */
enum ieee_attrs {
	DCB_ATTR_IEEE_UNSPEC,
	DCB_ATTR_IEEE_ETS,
	DCB_ATTR_IEEE_PFC,
	DCB_ATTR_IEEE_APP_TABLE,
	DCB_ATTR_IEEE_PEER_ETS,
	DCB_ATTR_IEEE_PEER_PFC,
	DCB_ATTR_IEEE_PEER_APP,
	DCB_ATTR_IEEE_MAXRATE,
	DCB_ATTR_IEEE_QCN,
	DCB_ATTR_IEEE_QCN_STATS,
	DCB_ATTR_DCB_BUFFER,
	DCB_ATTR_DCB_APP_TRUST_TABLE,
	DCB_ATTR_DCB_REWR_TABLE,
	__DCB_ATTR_IEEE_MAX
};
#define DCB_ATTR_IEEE_MAX (__DCB_ATTR_IEEE_MAX - 1)

enum ieee_attrs_app {
	DCB_ATTR_IEEE_APP_UNSPEC,
	DCB_ATTR_IEEE_APP,
	DCB_ATTR_DCB_APP,
	__DCB_ATTR_IEEE_APP_MAX
};
#define DCB_ATTR_IEEE_APP_MAX (__DCB_ATTR_IEEE_APP_MAX - 1)

/**
 * enum cee_attrs - CEE DCBX get attributes.
 *
 * @DCB_ATTR_CEE_UNSPEC: unspecified
 * @DCB_ATTR_CEE_PEER_PG: peer PG configuration - get only
 * @DCB_ATTR_CEE_PEER_PFC: peer PFC configuration - get only
 * @DCB_ATTR_CEE_PEER_APP_TABLE: peer APP tlv - get only
 * @DCB_ATTR_CEE_TX_PG: TX PG configuration (DCB_CMD_PGTX_GCFG)
 * @DCB_ATTR_CEE_RX_PG: RX PG configuration (DCB_CMD_PGRX_GCFG)
 * @DCB_ATTR_CEE_PFC: PFC configuration (DCB_CMD_PFC_GCFG)
 * @DCB_ATTR_CEE_APP_TABLE: APP configuration (multi DCB_CMD_GAPP)
 * @DCB_ATTR_CEE_FEAT: DCBX features flags (DCB_CMD_GFEATCFG)
 *
 * An aggregated collection of the cee std negotiated parameters.
 */
enum cee_attrs {
	DCB_ATTR_CEE_UNSPEC,
	DCB_ATTR_CEE_PEER_PG,
	DCB_ATTR_CEE_PEER_PFC,
	DCB_ATTR_CEE_PEER_APP_TABLE,
	DCB_ATTR_CEE_TX_PG,
	DCB_ATTR_CEE_RX_PG,
	DCB_ATTR_CEE_PFC,
	DCB_ATTR_CEE_APP_TABLE,
	DCB_ATTR_CEE_FEAT,
	__DCB_ATTR_CEE_MAX
};
#define DCB_ATTR_CEE_MAX (__DCB_ATTR_CEE_MAX - 1)

enum peer_app_attr {
	DCB_ATTR_CEE_PEER_APP_UNSPEC,
	DCB_ATTR_CEE_PEER_APP_INFO,
	DCB_ATTR_CEE_PEER_APP,
	__DCB_ATTR_CEE_PEER_APP_MAX
};
#define DCB_ATTR_CEE_PEER_APP_MAX (__DCB_ATTR_CEE_PEER_APP_MAX - 1)

enum cee_attrs_app {
	DCB_ATTR_CEE_APP_UNSPEC,
	DCB_ATTR_CEE_APP,
	__DCB_ATTR_CEE_APP_MAX
};
#define DCB_ATTR_CEE_APP_MAX (__DCB_ATTR_CEE_APP_MAX - 1)

/**
 * enum dcbnl_pfc_attrs - DCB Priority Flow Control user priority nested attrs
 *
 * @DCB_PFC_UP_ATTR_UNDEFINED: unspecified attribute to catch errors
 * @DCB_PFC_UP_ATTR_0: Priority Flow Control value for User Priority 0 (NLA_U8)
 * @DCB_PFC_UP_ATTR_1: Priority Flow Control value for User Priority 1 (NLA_U8)
 * @DCB_PFC_UP_ATTR_2: Priority Flow Control value for User Priority 2 (NLA_U8)
 * @DCB_PFC_UP_ATTR_3: Priority Flow Control value for User Priority 3 (NLA_U8)
 * @DCB_PFC_UP_ATTR_4: Priority Flow Control value for User Priority 4 (NLA_U8)
 * @DCB_PFC_UP_ATTR_5: Priority Flow Control value for User Priority 5 (NLA_U8)
 * @DCB_PFC_UP_ATTR_6: Priority Flow Control value for User Priority 6 (NLA_U8)
 * @DCB_PFC_UP_ATTR_7: Priority Flow Control value for User Priority 7 (NLA_U8)
 * @DCB_PFC_UP_ATTR_MAX: highest attribute number currently defined
 * @DCB_PFC_UP_ATTR_ALL: apply to all priority flow control attrs (NLA_FLAG)
 *
 */
enum dcbnl_pfc_up_attrs {
	DCB_PFC_UP_ATTR_UNDEFINED,

	DCB_PFC_UP_ATTR_0,
	DCB_PFC_UP_ATTR_1,
	DCB_PFC_UP_ATTR_2,
	DCB_PFC_UP_ATTR_3,
	DCB_PFC_UP_ATTR_4,
	DCB_PFC_UP_ATTR_5,
	DCB_PFC_UP_ATTR_6,
	DCB_PFC_UP_ATTR_7,
	DCB_PFC_UP_ATTR_ALL,

	__DCB_PFC_UP_ATTR_ENUM_MAX,
	DCB_PFC_UP_ATTR_MAX = __DCB_PFC_UP_ATTR_ENUM_MAX - 1,
};

/**
 * enum dcbnl_pg_attrs - DCB Priority Group attributes
 *
 * @DCB_PG_ATTR_UNDEFINED: unspecified attribute to catch errors
 * @DCB_PG_ATTR_TC_0: Priority Group Traffic Class 0 configuration (NLA_NESTED)
 * @DCB_PG_ATTR_TC_1: Priority Group Traffic Class 1 configuration (NLA_NESTED)
 * @DCB_PG_ATTR_TC_2: Priority Group Traffic Class 2 configuration (NLA_NESTED)
 * @DCB_PG_ATTR_TC_3: Priority Group Traffic Class 3 configuration (NLA_NESTED)
 * @DCB_PG_ATTR_TC_4: Priority Group Traffic Class 4 configuration (NLA_NESTED)
 * @DCB_PG_ATTR_TC_5: Priority Group Traffic Class 5 configuration (NLA_NESTED)
 * @DCB_PG_ATTR_TC_6: Priority Group Traffic Class 6 configuration (NLA_NESTED)
 * @DCB_PG_ATTR_TC_7: Priority Group Traffic Class 7 configuration (NLA_NESTED)
 * @DCB_PG_ATTR_TC_MAX: highest attribute number currently defined
 * @DCB_PG_ATTR_TC_ALL: apply to all traffic classes (NLA_NESTED)
 * @DCB_PG_ATTR_BW_ID_0: Percent of link bandwidth for Priority Group 0 (NLA_U8)
 * @DCB_PG_ATTR_BW_ID_1: Percent of link bandwidth for Priority Group 1 (NLA_U8)
 * @DCB_PG_ATTR_BW_ID_2: Percent of link bandwidth for Priority Group 2 (NLA_U8)
 * @DCB_PG_ATTR_BW_ID_3: Percent of link bandwidth for Priority Group 3 (NLA_U8)
 * @DCB_PG_ATTR_BW_ID_4: Percent of link bandwidth for Priority Group 4 (NLA_U8)
 * @DCB_PG_ATTR_BW_ID_5: Percent of link bandwidth for Priority Group 5 (NLA_U8)
 * @DCB_PG_ATTR_BW_ID_6: Percent of link bandwidth for Priority Group 6 (NLA_U8)
 * @DCB_PG_ATTR_BW_ID_7: Percent of link bandwidth for Priority Group 7 (NLA_U8)
 * @DCB_PG_ATTR_BW_ID_MAX: highest attribute number currently defined
 * @DCB_PG_ATTR_BW_ID_ALL: apply to all priority groups (NLA_FLAG)
 *
 */
enum dcbnl_pg_attrs {
	DCB_PG_ATTR_UNDEFINED,

	DCB_PG_ATTR_TC_0,
	DCB_PG_ATTR_TC_1,
	DCB_PG_ATTR_TC_2,
	DCB_PG_ATTR_TC_3,
	DCB_PG_ATTR_TC_4,
	DCB_PG_ATTR_TC_5,
	DCB_PG_ATTR_TC_6,
	DCB_PG_ATTR_TC_7,
	DCB_PG_ATTR_TC_MAX,
	DCB_PG_ATTR_TC_ALL,

	DCB_PG_ATTR_BW_ID_0,
	DCB_PG_ATTR_BW_ID_1,
	DCB_PG_ATTR_BW_ID_2,
	DCB_PG_ATTR_BW_ID_3,
	DCB_PG_ATTR_BW_ID_4,
	DCB_PG_ATTR_BW_ID_5,
	DCB_PG_ATTR_BW_ID_6,
	DCB_PG_ATTR_BW_ID_7,
	DCB_PG_ATTR_BW_ID_MAX,
	DCB_PG_ATTR_BW_ID_ALL,

	__DCB_PG_ATTR_ENUM_MAX,
	DCB_PG_ATTR_MAX = __DCB_PG_ATTR_ENUM_MAX - 1,
};

/**
 * enum dcbnl_tc_attrs - DCB Traffic Class attributes
 *
 * @DCB_TC_ATTR_PARAM_UNDEFINED: unspecified attribute to catch errors
 * @DCB_TC_ATTR_PARAM_PGID: (NLA_U8) Priority group the traffic class belongs to
 *                          Valid values are:  0-7
 * @DCB_TC_ATTR_PARAM_UP_MAPPING: (NLA_U8) Traffic class to user priority map
 *                                Some devices may not support changing the
 *                                user priority map of a TC.
 * @DCB_TC_ATTR_PARAM_STRICT_PRIO: (NLA_U8) Strict priority setting
 *                                 0 - none
 *                                 1 - group strict
 *                                 2 - link strict
 * @DCB_TC_ATTR_PARAM_BW_PCT: optional - (NLA_U8) If supported by the device and
 *                            not configured to use link strict priority,
 *                            this is the percentage of bandwidth of the
 *                            priority group this traffic class belongs to
 * @DCB_TC_ATTR_PARAM_ALL: (NLA_FLAG) all traffic class parameters
 *
 */
enum dcbnl_tc_attrs {
	DCB_TC_ATTR_PARAM_UNDEFINED,

	DCB_TC_ATTR_PARAM_PGID,
	DCB_TC_ATTR_PARAM_UP_MAPPING,
	DCB_TC_ATTR_PARAM_STRICT_PRIO,
	DCB_TC_ATTR_PARAM_BW_PCT,
	DCB_TC_ATTR_PARAM_ALL,

	__DCB_TC_ATTR_PARAM_ENUM_MAX,
	DCB_TC_ATTR_PARAM_MAX = __DCB_TC_ATTR_PARAM_ENUM_MAX - 1,
};

/**
 * enum dcbnl_cap_attrs - DCB Capability attributes
 *
 * @DCB_CAP_ATTR_UNDEFINED: unspecified attribute to catch errors
 * @DCB_CAP_ATTR_ALL: (NLA_FLAG) all capability parameters
 * @DCB_CAP_ATTR_PG: (NLA_U8) device supports Priority Groups
 * @DCB_CAP_ATTR_PFC: (NLA_U8) device supports Priority Flow Control
 * @DCB_CAP_ATTR_UP2TC: (NLA_U8) device supports user priority to
 *                               traffic class mapping
 * @DCB_CAP_ATTR_PG_TCS: (NLA_U8) bitmap where each bit represents a
 *                                number of traffic classes the device
 *                                can be configured to use for Priority Groups
 * @DCB_CAP_ATTR_PFC_TCS: (NLA_U8) bitmap where each bit represents a
 *                                 number of traffic classes the device can be
 *                                 configured to use for Priority Flow Control
 * @DCB_CAP_ATTR_GSP: (NLA_U8) device supports group strict priority
 * @DCB_CAP_ATTR_BCN: (NLA_U8) device supports Backwards Congestion
 *                             Notification
 * @DCB_CAP_ATTR_DCBX: (NLA_U8) device supports DCBX engine
 *
 */
enum dcbnl_cap_attrs {
	DCB_CAP_ATTR_UNDEFINED,
	DCB_CAP_ATTR_ALL,
	DCB_CAP_ATTR_PG,
	DCB_CAP_ATTR_PFC,
	DCB_CAP_ATTR_UP2TC,
	DCB_CAP_ATTR_PG_TCS,
	DCB_CAP_ATTR_PFC_TCS,
	DCB_CAP_ATTR_GSP,
	DCB_CAP_ATTR_BCN,
	DCB_CAP_ATTR_DCBX,

	__DCB_CAP_ATTR_ENUM_MAX,
	DCB_CAP_ATTR_MAX = __DCB_CAP_ATTR_ENUM_MAX - 1,
};

/**
 * DCBX capability flags
 *
 * @DCB_CAP_DCBX_HOST: DCBX negotiation is performed by the host LLDP agent.
 *                     'set' routines are used to configure the device with
 *                     the negotiated parameters
 *
 * @DCB_CAP_DCBX_LLD_MANAGED: DCBX negotiation is not performed in the host but
 *                            by another entity
 *                            'get' routines are used to retrieve the
 *                            negotiated parameters
 *                            'set' routines can be used to set the initial
 *                            negotiation configuration
 *
 * @DCB_CAP_DCBX_VER_CEE: for a non-host DCBX engine, indicates the engine
 *                        supports the CEE protocol flavor
 *
 * @DCB_CAP_DCBX_VER_IEEE: for a non-host DCBX engine, indicates the engine
 *                         supports the IEEE protocol flavor
 *
 * @DCB_CAP_DCBX_STATIC: for a non-host DCBX engine, indicates the engine
 *                       supports static configuration (i.e no actual
 *                       negotiation is performed negotiated parameters equal
 *                       the initial configuration)
 *
 */
#define DCB_CAP_DCBX_HOST		0x01
#define DCB_CAP_DCBX_LLD_MANAGED	0x02
#define DCB_CAP_DCBX_VER_CEE		0x04
#define DCB_CAP_DCBX_VER_IEEE		0x08
#define DCB_CAP_DCBX_STATIC		0x10

/**
 * enum dcbnl_numtcs_attrs - number of traffic classes
 *
 * @DCB_NUMTCS_ATTR_UNDEFINED: unspecified attribute to catch errors
 * @DCB_NUMTCS_ATTR_ALL: (NLA_FLAG) all traffic class attributes
 * @DCB_NUMTCS_ATTR_PG: (NLA_U8) number of traffic classes used for
 *                               priority groups
 * @DCB_NUMTCS_ATTR_PFC: (NLA_U8) number of traffic classes which can
 *                                support priority flow control
 */
enum dcbnl_numtcs_attrs {
	DCB_NUMTCS_ATTR_UNDEFINED,
	DCB_NUMTCS_ATTR_ALL,
	DCB_NUMTCS_ATTR_PG,
	DCB_NUMTCS_ATTR_PFC,

	__DCB_NUMTCS_ATTR_ENUM_MAX,
	DCB_NUMTCS_ATTR_MAX = __DCB_NUMTCS_ATTR_ENUM_MAX - 1,
};

enum dcbnl_bcn_attrs{
	DCB_BCN_ATTR_UNDEFINED = 0,

	DCB_BCN_ATTR_RP_0,
	DCB_BCN_ATTR_RP_1,
	DCB_BCN_ATTR_RP_2,
	DCB_BCN_ATTR_RP_3,
	DCB_BCN_ATTR_RP_4,
	DCB_BCN_ATTR_RP_5,
	DCB_BCN_ATTR_RP_6,
	DCB_BCN_ATTR_RP_7,
	DCB_BCN_ATTR_RP_ALL,

	DCB_BCN_ATTR_BCNA_0,
	DCB_BCN_ATTR_BCNA_1,
	DCB_BCN_ATTR_ALPHA,
	DCB_BCN_ATTR_BETA,
	DCB_BCN_ATTR_GD,
	DCB_BCN_ATTR_GI,
	DCB_BCN_ATTR_TMAX,
	DCB_BCN_ATTR_TD,
	DCB_BCN_ATTR_RMIN,
	DCB_BCN_ATTR_W,
	DCB_BCN_ATTR_RD,
	DCB_BCN_ATTR_RU,
	DCB_BCN_ATTR_WRTT,
	DCB_BCN_ATTR_RI,
	DCB_BCN_ATTR_C,
	DCB_BCN_ATTR_ALL,

	__DCB_BCN_ATTR_ENUM_MAX,
	DCB_BCN_ATTR_MAX = __DCB_BCN_ATTR_ENUM_MAX - 1,
};

/**
 * enum dcb_general_attr_values - general DCB attribute values
 *
 * @DCB_ATTR_UNDEFINED: value used to indicate an attribute is not supported
 *
 */
enum dcb_general_attr_values {
	DCB_ATTR_VALUE_UNDEFINED = 0xff
};

#define DCB_APP_IDTYPE_ETHTYPE	0x00
#define DCB_APP_IDTYPE_PORTNUM	0x01
enum dcbnl_app_attrs {
	DCB_APP_ATTR_UNDEFINED,

	DCB_APP_ATTR_IDTYPE,
	DCB_APP_ATTR_ID,
	DCB_APP_ATTR_PRIORITY,

	__DCB_APP_ATTR_ENUM_MAX,
	DCB_APP_ATTR_MAX = __DCB_APP_ATTR_ENUM_MAX - 1,
};

/**
 * enum dcbnl_featcfg_attrs - features conifiguration flags
 *
 * @DCB_FEATCFG_ATTR_UNDEFINED: unspecified attribute to catch errors
 * @DCB_FEATCFG_ATTR_ALL: (NLA_FLAG) all features configuration attributes
 * @DCB_FEATCFG_ATTR_PG: (NLA_U8) configuration flags for priority groups
 * @DCB_FEATCFG_ATTR_PFC: (NLA_U8) configuration flags for priority
 *                                 flow control
 * @DCB_FEATCFG_ATTR_APP: (NLA_U8) configuration flags for application TLV
 *
 */
#define DCB_FEATCFG_ERROR	0x01	/* error in feature resolution */
#define DCB_FEATCFG_ENABLE	0x02	/* enable feature */
#define DCB_FEATCFG_WILLING	0x04	/* feature is willing */
#define DCB_FEATCFG_ADVERTISE	0x08	/* advertise feature */
enum dcbnl_featcfg_attrs {
	DCB_FEATCFG_ATTR_UNDEFINED,
	DCB_FEATCFG_ATTR_ALL,
	DCB_FEATCFG_ATTR_PG,
	DCB_FEATCFG_ATTR_PFC,
	DCB_FEATCFG_ATTR_APP,

	__DCB_FEATCFG_ATTR_ENUM_MAX,
	DCB_FEATCFG_ATTR_MAX = __DCB_FEATCFG_ATTR_ENUM_MAX - 1,
};

#endif /* __LINUX_DCBNL_H__ */
