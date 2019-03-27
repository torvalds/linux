/*
 * Copyright (c) 2017-2018 Cavium, Inc. 
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#ifndef __ECORE_HSI_FCOE__
#define __ECORE_HSI_FCOE__ 
/****************************************/
/* Add include to common storage target */
/****************************************/
#include "storage_common.h"

/************************************************************************/
/* Add include to common fcoe target for both eCore and protocol driver */
/************************************************************************/
#include "fcoe_common.h"


/*
 * The fcoe storm context of Ystorm
 */
struct ystorm_fcoe_conn_st_ctx
{
	u8 func_mode /* Function mode */;
	u8 cos /* Transmission cos */;
	u8 conf_version /* Is dcb_version or vntag_version changed */;
	u8 eth_hdr_size /* Ethernet header size */;
	__le16 stat_ram_addr /* Statistics ram adderss */;
	__le16 mtu /* MTU limitation */;
	__le16 max_fc_payload_len /* Max payload length according to target limitation and mtu. 8 bytes aligned (required for protection fast-path) */;
	__le16 tx_max_fc_pay_len /* Max payload length according to target limitation */;
	u8 fcp_cmd_size /* FCP cmd size. for performance reasons */;
	u8 fcp_rsp_size /* FCP RSP size. for performance reasons */;
	__le16 mss /* MSS for PBF (MSS we negotiate with target - protection data per segment. If we are not in perf mode it will be according to worse case) */;
	struct regpair reserved;
	__le16 min_frame_size /* The minimum ETH frame size required for transmission (including ETH header) */;
	u8 protection_info_flags;
#define YSTORM_FCOE_CONN_ST_CTX_SUPPORT_PROTECTION_MASK  0x1 /* Does this connection support protection (if couple of GOS share this connection it× â‚¬â„¢s enough that one of them support protection) */
#define YSTORM_FCOE_CONN_ST_CTX_SUPPORT_PROTECTION_SHIFT 0
#define YSTORM_FCOE_CONN_ST_CTX_VALID_MASK               0x1 /* Are we in protection perf mode (there is only one protection mode for this connection and we manage to create mss that contain fixed amount of protection segment and we are only restrict by the target limitation and not line mss this is critical since if line mss restrict us we can× â‚¬â„¢t rely on this size × â‚¬â€œ it depends on vlan num) */
#define YSTORM_FCOE_CONN_ST_CTX_VALID_SHIFT              1
#define YSTORM_FCOE_CONN_ST_CTX_RESERVED1_MASK           0x3F
#define YSTORM_FCOE_CONN_ST_CTX_RESERVED1_SHIFT          2
	u8 dst_protection_per_mss /* Destination Protection data per mss (if we are not in perf mode it will be worse case). Destination is the data add/remove from the transmitted packet (as opposed to src which is data validate by the nic they might not be identical) */;
	u8 src_protection_per_mss /* Source Protection data per mss (if we are not in perf mode it will be worse case). Source  is the data validated by the nic  (as opposed to destination which is data add/remove from the transmitted packet they might not be identical) */;
	u8 ptu_log_page_size /* 0-4K, 1-8K, 2-16K, 3-32K... */;
	u8 flags;
#define YSTORM_FCOE_CONN_ST_CTX_INNER_VLAN_FLAG_MASK     0x1 /* Inner Vlan flag */
#define YSTORM_FCOE_CONN_ST_CTX_INNER_VLAN_FLAG_SHIFT    0
#define YSTORM_FCOE_CONN_ST_CTX_OUTER_VLAN_FLAG_MASK     0x1 /* Outer Vlan flag */
#define YSTORM_FCOE_CONN_ST_CTX_OUTER_VLAN_FLAG_SHIFT    1
#define YSTORM_FCOE_CONN_ST_CTX_RSRV_MASK                0x3F
#define YSTORM_FCOE_CONN_ST_CTX_RSRV_SHIFT               2
	u8 fcp_xfer_size /* FCP xfer size. for performance reasons */;
};

/*
 * FCoE 16-bits vlan structure
 */
struct fcoe_vlan_fields
{
	__le16 fields;
#define FCOE_VLAN_FIELDS_VID_MASK  0xFFF
#define FCOE_VLAN_FIELDS_VID_SHIFT 0
#define FCOE_VLAN_FIELDS_CLI_MASK  0x1
#define FCOE_VLAN_FIELDS_CLI_SHIFT 12
#define FCOE_VLAN_FIELDS_PRI_MASK  0x7
#define FCOE_VLAN_FIELDS_PRI_SHIFT 13
};

/*
 * FCoE 16-bits vlan union
 */
union fcoe_vlan_field_union
{
	struct fcoe_vlan_fields fields /* Parameters field */;
	__le16 val /* Global value */;
};

/*
 * FCoE 16-bits vlan, vif union
 */
union fcoe_vlan_vif_field_union
{
	union fcoe_vlan_field_union vlan /* Vlan */;
	__le16 vif /* VIF */;
};

/*
 * Ethernet context section
 */
struct pstorm_fcoe_eth_context_section
{
	u8 remote_addr_3 /* Remote Mac Address, used in PBF Header Builder Command */;
	u8 remote_addr_2 /* Remote Mac Address, used in PBF Header Builder Command */;
	u8 remote_addr_1 /* Remote Mac Address, used in PBF Header Builder Command */;
	u8 remote_addr_0 /* Remote Mac Address, used in PBF Header Builder Command */;
	u8 local_addr_1 /* Local Mac Address, used in PBF Header Builder Command */;
	u8 local_addr_0 /* Local Mac Address, used in PBF Header Builder Command */;
	u8 remote_addr_5 /* Remote Mac Address, used in PBF Header Builder Command */;
	u8 remote_addr_4 /* Remote Mac Address, used in PBF Header Builder Command */;
	u8 local_addr_5 /* Local Mac Address, used in PBF Header Builder Command */;
	u8 local_addr_4 /* Loca lMac Address, used in PBF Header Builder Command */;
	u8 local_addr_3 /* Local Mac Address, used in PBF Header Builder Command */;
	u8 local_addr_2 /* Local Mac Address, used in PBF Header Builder Command */;
	union fcoe_vlan_vif_field_union vif_outer_vlan /* Union of VIF and outer vlan */;
	__le16 vif_outer_eth_type /* reserved place for Ethernet type */;
	union fcoe_vlan_vif_field_union inner_vlan /* inner vlan tag */;
	__le16 inner_eth_type /* reserved place for Ethernet type */;
};

/*
 * The fcoe storm context of Pstorm
 */
struct pstorm_fcoe_conn_st_ctx
{
	u8 func_mode /* Function mode */;
	u8 cos /* Transmission cos */;
	u8 conf_version /* Is dcb_version or vntag_version changed */;
	u8 rsrv;
	__le16 stat_ram_addr /* Statistics ram adderss */;
	__le16 mss /* MSS for PBF (MSS we negotiate with target - protection data per segment. If we are not in perf mode it will be according to worse case) */;
	struct regpair abts_cleanup_addr /* Host addr of ABTS /Cleanup info. since we pass it  through session context, we pass only the addr to save space */;
	struct pstorm_fcoe_eth_context_section eth /* Source mac */;
	u8 sid_2 /* SID FC address - Third byte that is sent to NW via PBF For example is SID is 01:02:03 then sid_2 is 0x03 */;
	u8 sid_1 /* SID FC address - Second byte that is sent to NW via PBF */;
	u8 sid_0 /* SID FC address - First byte that is sent to NW via PBF */;
	u8 flags;
#define PSTORM_FCOE_CONN_ST_CTX_VNTAG_VLAN_MASK          0x1 /* Is inner vlan taken from vntag default vlan (in this case I have to update inner vlan each time the default change) */
#define PSTORM_FCOE_CONN_ST_CTX_VNTAG_VLAN_SHIFT         0
#define PSTORM_FCOE_CONN_ST_CTX_SUPPORT_REC_RR_TOV_MASK  0x1 /* AreSupport rec_tov timer */
#define PSTORM_FCOE_CONN_ST_CTX_SUPPORT_REC_RR_TOV_SHIFT 1
#define PSTORM_FCOE_CONN_ST_CTX_INNER_VLAN_FLAG_MASK     0x1 /* Inner Vlan flag */
#define PSTORM_FCOE_CONN_ST_CTX_INNER_VLAN_FLAG_SHIFT    2
#define PSTORM_FCOE_CONN_ST_CTX_OUTER_VLAN_FLAG_MASK     0x1 /* Outer Vlan flag */
#define PSTORM_FCOE_CONN_ST_CTX_OUTER_VLAN_FLAG_SHIFT    3
#define PSTORM_FCOE_CONN_ST_CTX_SINGLE_VLAN_FLAG_MASK    0x1 /* Indicaiton that there should be a single vlan (for UFP mode) */
#define PSTORM_FCOE_CONN_ST_CTX_SINGLE_VLAN_FLAG_SHIFT   4
#define PSTORM_FCOE_CONN_ST_CTX_RESERVED_MASK            0x7
#define PSTORM_FCOE_CONN_ST_CTX_RESERVED_SHIFT           5
	u8 did_2 /* DID FC address - Third byte that is sent to NW via PBF */;
	u8 did_1 /* DID FC address - Second byte that is sent to NW via PBF */;
	u8 did_0 /* DID FC address - First byte that is sent to NW via PBF */;
	u8 src_mac_index;
	__le16 rec_rr_tov_val /* REC_TOV value negotiated during PLOGI (in msec) */;
	u8 q_relative_offset /* CQ, RQ (and CMDQ) relative offset for connection */;
	u8 reserved1;
};

/*
 * The fcoe storm context of Xstorm
 */
struct xstorm_fcoe_conn_st_ctx
{
	u8 func_mode /* Function mode */;
	u8 src_mac_index /* Index to the src_mac arr held in the xStorm RAM. Provided at the xStorm offload connection handler */;
	u8 conf_version /* Advance if vntag/dcb version advance */;
	u8 cached_wqes_avail /* Number of cached wqes available */;
	__le16 stat_ram_addr /* Statistics ram adderss */;
	u8 flags;
#define XSTORM_FCOE_CONN_ST_CTX_SQ_DEFERRED_MASK             0x1 /* SQ deferred (happens when we wait for xfer wqe to complete cleanup/abts */
#define XSTORM_FCOE_CONN_ST_CTX_SQ_DEFERRED_SHIFT            0
#define XSTORM_FCOE_CONN_ST_CTX_INNER_VLAN_FLAG_MASK         0x1 /* Inner vlan flag â€  for calculating eth header size */
#define XSTORM_FCOE_CONN_ST_CTX_INNER_VLAN_FLAG_SHIFT        1
#define XSTORM_FCOE_CONN_ST_CTX_INNER_VLAN_FLAG_ORIG_MASK    0x1 /* Original vlan configuration. used when we switch from dcb enable to dcb disabled */
#define XSTORM_FCOE_CONN_ST_CTX_INNER_VLAN_FLAG_ORIG_SHIFT   2
#define XSTORM_FCOE_CONN_ST_CTX_LAST_QUEUE_HANDLED_MASK      0x3
#define XSTORM_FCOE_CONN_ST_CTX_LAST_QUEUE_HANDLED_SHIFT     3
#define XSTORM_FCOE_CONN_ST_CTX_RSRV_MASK                    0x7
#define XSTORM_FCOE_CONN_ST_CTX_RSRV_SHIFT                   5
	u8 cached_wqes_offset /* Offset of first valid cached wqe */;
	u8 reserved2;
	u8 eth_hdr_size /* Ethernet header size */;
	u8 seq_id /* Sequence id */;
	u8 max_conc_seqs /* Max concurrent sequence id */;
	__le16 num_pages_in_pbl /* Num of pages in SQ/RESPQ/XFERQ Pbl */;
	__le16 reserved;
	struct regpair sq_pbl_addr /* SQ address */;
	struct regpair sq_curr_page_addr /* SQ current page address */;
	struct regpair sq_next_page_addr /* SQ next page address */;
	struct regpair xferq_pbl_addr /* XFERQ address */;
	struct regpair xferq_curr_page_addr /* XFERQ current page address */;
	struct regpair xferq_next_page_addr /* XFERQ next page address */;
	struct regpair respq_pbl_addr /* RESPQ address */;
	struct regpair respq_curr_page_addr /* RESPQ current page address */;
	struct regpair respq_next_page_addr /* RESPQ next page address */;
	__le16 mtu /* MTU limitation */;
	__le16 tx_max_fc_pay_len /* Max payload length according to target limitation */;
	__le16 max_fc_payload_len /* Max payload length according to target limitation and mtu. Aligned to 4 bytes. */;
	__le16 min_frame_size /* The minimum ETH frame size required for transmission (including ETH header, excluding ETH CRC */;
	__le16 sq_pbl_next_index /* Next index of SQ Pbl */;
	__le16 respq_pbl_next_index /* Next index of RESPQ Pbl */;
	u8 fcp_cmd_byte_credit /* Pre-calculated byte credit that single FCP command can consume */;
	u8 fcp_rsp_byte_credit /* Pre-calculated byte credit that single FCP RSP can consume. */;
	__le16 protection_info;
#define XSTORM_FCOE_CONN_ST_CTX_PROTECTION_PERF_MASK         0x1 /* Intend to accelerate the protection flows */
#define XSTORM_FCOE_CONN_ST_CTX_PROTECTION_PERF_SHIFT        0
#define XSTORM_FCOE_CONN_ST_CTX_SUPPORT_PROTECTION_MASK      0x1 /* Does this connection support protection (if couple of GOS share this connection is enough that one of them support protection) */
#define XSTORM_FCOE_CONN_ST_CTX_SUPPORT_PROTECTION_SHIFT     1
#define XSTORM_FCOE_CONN_ST_CTX_VALID_MASK                   0x1 /* Are we in protection perf mode (there is only one protection mode for this connection and we manage to create mss that contain fixed amount of protection segment and we are only restrict by the target limitation and not line mss this is critical since if line mss restrict us we canâ€™t rely on this size â€  it depends on vlan num) */
#define XSTORM_FCOE_CONN_ST_CTX_VALID_SHIFT                  2
#define XSTORM_FCOE_CONN_ST_CTX_FRAME_PROT_ALIGNED_MASK      0x1 /* Is size of tx_max_pay_len_prot can be aligned to protection intervals. This means that pure data in each frame is 2k exactly, and protection intervals are no bigger than 2k */
#define XSTORM_FCOE_CONN_ST_CTX_FRAME_PROT_ALIGNED_SHIFT     3
#define XSTORM_FCOE_CONN_ST_CTX_RESERVED3_MASK               0xF
#define XSTORM_FCOE_CONN_ST_CTX_RESERVED3_SHIFT              4
#define XSTORM_FCOE_CONN_ST_CTX_DST_PROTECTION_PER_MSS_MASK  0xFF /* Destination Pro tection data per mss (if we are not in perf mode it will be worse case). Destination is the data add/remove from the transmitted packet (as opposed to src which is data validate by the nic they might not be identical) */
#define XSTORM_FCOE_CONN_ST_CTX_DST_PROTECTION_PER_MSS_SHIFT 8
	__le16 xferq_pbl_next_index /* Next index of XFERQ Pbl */;
	__le16 page_size /* Page size (in bytes) */;
	u8 mid_seq /* Equals 1 for Middle sequence indication, otherwise 0 */;
	u8 fcp_xfer_byte_credit /* Pre-calculated byte credit that single FCP command can consume */;
	u8 reserved1[2];
	struct fcoe_wqe cached_wqes[16] /* cached wqe (8) = 8*8*8Bytes */;
};

struct e4_xstorm_fcoe_conn_ag_ctx
{
	u8 reserved0 /* cdu_validation */;
	u8 state /* state */;
	u8 flags0;
#define E4_XSTORM_FCOE_CONN_AG_CTX_EXIST_IN_QM0_MASK       0x1 /* exist_in_qm0 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_EXIST_IN_QM0_SHIFT      0
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED1_MASK          0x1 /* exist_in_qm1 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED1_SHIFT         1
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED2_MASK          0x1 /* exist_in_qm2 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED2_SHIFT         2
#define E4_XSTORM_FCOE_CONN_AG_CTX_EXIST_IN_QM3_MASK       0x1 /* exist_in_qm3 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_EXIST_IN_QM3_SHIFT      3
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED3_MASK          0x1 /* bit4 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED3_SHIFT         4
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED4_MASK          0x1 /* cf_array_active */
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED4_SHIFT         5
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED5_MASK          0x1 /* bit6 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED5_SHIFT         6
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED6_MASK          0x1 /* bit7 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED6_SHIFT         7
	u8 flags1;
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED7_MASK          0x1 /* bit8 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED7_SHIFT         0
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED8_MASK          0x1 /* bit9 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED8_SHIFT         1
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED9_MASK          0x1 /* bit10 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED9_SHIFT         2
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT11_MASK              0x1 /* bit11 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT11_SHIFT             3
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT12_MASK              0x1 /* bit12 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT12_SHIFT             4
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT13_MASK              0x1 /* bit13 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT13_SHIFT             5
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT14_MASK              0x1 /* bit14 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT14_SHIFT             6
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT15_MASK              0x1 /* bit15 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT15_SHIFT             7
	u8 flags2;
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF0_MASK                0x3 /* timer0cf */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF0_SHIFT               0
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF1_MASK                0x3 /* timer1cf */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF1_SHIFT               2
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF2_MASK                0x3 /* timer2cf */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF2_SHIFT               4
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF3_MASK                0x3 /* timer_stop_all */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF3_SHIFT               6
	u8 flags3;
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF4_MASK                0x3 /* cf4 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF4_SHIFT               0
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF5_MASK                0x3 /* cf5 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF5_SHIFT               2
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF6_MASK                0x3 /* cf6 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF6_SHIFT               4
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF7_MASK                0x3 /* cf7 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF7_SHIFT               6
	u8 flags4;
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF8_MASK                0x3 /* cf8 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF8_SHIFT               0
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF9_MASK                0x3 /* cf9 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF9_SHIFT               2
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF10_MASK               0x3 /* cf10 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF10_SHIFT              4
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF11_MASK               0x3 /* cf11 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF11_SHIFT              6
	u8 flags5;
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF12_MASK               0x3 /* cf12 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF12_SHIFT              0
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF13_MASK               0x3 /* cf13 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF13_SHIFT              2
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF14_MASK               0x3 /* cf14 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF14_SHIFT              4
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF15_MASK               0x3 /* cf15 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF15_SHIFT              6
	u8 flags6;
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF16_MASK               0x3 /* cf16 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF16_SHIFT              0
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF17_MASK               0x3 /* cf_array_cf */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF17_SHIFT              2
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF18_MASK               0x3 /* cf18 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF18_SHIFT              4
#define E4_XSTORM_FCOE_CONN_AG_CTX_DQ_CF_MASK              0x3 /* cf19 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_DQ_CF_SHIFT             6
	u8 flags7;
#define E4_XSTORM_FCOE_CONN_AG_CTX_FLUSH_Q0_MASK           0x3 /* cf20 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_FLUSH_Q0_SHIFT          0
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED10_MASK         0x3 /* cf21 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED10_SHIFT        2
#define E4_XSTORM_FCOE_CONN_AG_CTX_SLOW_PATH_MASK          0x3 /* cf22 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_SLOW_PATH_SHIFT         4
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF0EN_MASK              0x1 /* cf0en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF0EN_SHIFT             6
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF1EN_MASK              0x1 /* cf1en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF1EN_SHIFT             7
	u8 flags8;
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF2EN_MASK              0x1 /* cf2en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF2EN_SHIFT             0
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF3EN_MASK              0x1 /* cf3en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF3EN_SHIFT             1
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF4EN_MASK              0x1 /* cf4en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF4EN_SHIFT             2
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF5EN_MASK              0x1 /* cf5en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF5EN_SHIFT             3
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF6EN_MASK              0x1 /* cf6en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF6EN_SHIFT             4
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF7EN_MASK              0x1 /* cf7en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF7EN_SHIFT             5
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF8EN_MASK              0x1 /* cf8en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF8EN_SHIFT             6
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF9EN_MASK              0x1 /* cf9en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF9EN_SHIFT             7
	u8 flags9;
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF10EN_MASK             0x1 /* cf10en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF10EN_SHIFT            0
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF11EN_MASK             0x1 /* cf11en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF11EN_SHIFT            1
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF12EN_MASK             0x1 /* cf12en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF12EN_SHIFT            2
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF13EN_MASK             0x1 /* cf13en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF13EN_SHIFT            3
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF14EN_MASK             0x1 /* cf14en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF14EN_SHIFT            4
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF15EN_MASK             0x1 /* cf15en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF15EN_SHIFT            5
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF16EN_MASK             0x1 /* cf16en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF16EN_SHIFT            6
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF17EN_MASK             0x1 /* cf_array_cf_en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF17EN_SHIFT            7
	u8 flags10;
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF18EN_MASK             0x1 /* cf18en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF18EN_SHIFT            0
#define E4_XSTORM_FCOE_CONN_AG_CTX_DQ_CF_EN_MASK           0x1 /* cf19en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_DQ_CF_EN_SHIFT          1
#define E4_XSTORM_FCOE_CONN_AG_CTX_FLUSH_Q0_EN_MASK        0x1 /* cf20en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_FLUSH_Q0_EN_SHIFT       2
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED11_MASK         0x1 /* cf21en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED11_SHIFT        3
#define E4_XSTORM_FCOE_CONN_AG_CTX_SLOW_PATH_EN_MASK       0x1 /* cf22en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_SLOW_PATH_EN_SHIFT      4
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF23EN_MASK             0x1 /* cf23en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF23EN_SHIFT            5
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED12_MASK         0x1 /* rule0en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED12_SHIFT        6
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED13_MASK         0x1 /* rule1en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED13_SHIFT        7
	u8 flags11;
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED14_MASK         0x1 /* rule2en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED14_SHIFT        0
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED15_MASK         0x1 /* rule3en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED15_SHIFT        1
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED16_MASK         0x1 /* rule4en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED16_SHIFT        2
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE5EN_MASK            0x1 /* rule5en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE5EN_SHIFT           3
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE6EN_MASK            0x1 /* rule6en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE6EN_SHIFT           4
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE7EN_MASK            0x1 /* rule7en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE7EN_SHIFT           5
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED1_MASK       0x1 /* rule8en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED1_SHIFT      6
#define E4_XSTORM_FCOE_CONN_AG_CTX_XFERQ_DECISION_EN_MASK  0x1 /* rule9en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_XFERQ_DECISION_EN_SHIFT 7
	u8 flags12;
#define E4_XSTORM_FCOE_CONN_AG_CTX_SQ_DECISION_EN_MASK     0x1 /* rule10en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_SQ_DECISION_EN_SHIFT    0
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE11EN_MASK           0x1 /* rule11en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE11EN_SHIFT          1
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED2_MASK       0x1 /* rule12en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED2_SHIFT      2
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED3_MASK       0x1 /* rule13en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED3_SHIFT      3
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE14EN_MASK           0x1 /* rule14en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE14EN_SHIFT          4
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE15EN_MASK           0x1 /* rule15en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE15EN_SHIFT          5
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE16EN_MASK           0x1 /* rule16en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE16EN_SHIFT          6
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE17EN_MASK           0x1 /* rule17en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE17EN_SHIFT          7
	u8 flags13;
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESPQ_DECISION_EN_MASK  0x1 /* rule18en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESPQ_DECISION_EN_SHIFT 0
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE19EN_MASK           0x1 /* rule19en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE19EN_SHIFT          1
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED4_MASK       0x1 /* rule20en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED4_SHIFT      2
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED5_MASK       0x1 /* rule21en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED5_SHIFT      3
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED6_MASK       0x1 /* rule22en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED6_SHIFT      4
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED7_MASK       0x1 /* rule23en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED7_SHIFT      5
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED8_MASK       0x1 /* rule24en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED8_SHIFT      6
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED9_MASK       0x1 /* rule25en */
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED9_SHIFT      7
	u8 flags14;
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT16_MASK              0x1 /* bit16 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT16_SHIFT             0
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT17_MASK              0x1 /* bit17 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT17_SHIFT             1
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT18_MASK              0x1 /* bit18 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT18_SHIFT             2
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT19_MASK              0x1 /* bit19 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT19_SHIFT             3
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT20_MASK              0x1 /* bit20 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT20_SHIFT             4
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT21_MASK              0x1 /* bit21 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT21_SHIFT             5
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF23_MASK               0x3 /* cf23 */
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF23_SHIFT              6
	u8 byte2 /* byte2 */;
	__le16 physical_q0 /* physical_q0 */;
	__le16 word1 /* physical_q1 */;
	__le16 word2 /* physical_q2 */;
	__le16 sq_cons /* word3 */;
	__le16 sq_prod /* word4 */;
	__le16 xferq_prod /* word5 */;
	__le16 xferq_cons /* conn_dpi */;
	u8 byte3 /* byte3 */;
	u8 byte4 /* byte4 */;
	u8 byte5 /* byte5 */;
	u8 byte6 /* byte6 */;
	__le32 remain_io /* reg0 */;
	__le32 reg1 /* reg1 */;
	__le32 reg2 /* reg2 */;
	__le32 reg3 /* reg3 */;
	__le32 reg4 /* reg4 */;
	__le32 reg5 /* cf_array0 */;
	__le32 reg6 /* cf_array1 */;
	__le16 respq_prod /* word7 */;
	__le16 respq_cons /* word8 */;
	__le16 word9 /* word9 */;
	__le16 word10 /* word10 */;
	__le32 reg7 /* reg7 */;
	__le32 reg8 /* reg8 */;
};

/*
 * The fcoe storm context of Ustorm
 */
struct ustorm_fcoe_conn_st_ctx
{
	struct regpair respq_pbl_addr /* RespQ Pbl base address */;
	__le16 num_pages_in_pbl /* Number of RespQ pbl pages (both have same wqe size) */;
	u8 ptu_log_page_size /* 0-4K, 1-8K, 2-16K, 3-32K... */;
	u8 log_page_size;
	__le16 respq_prod /* RespQ producer */;
	u8 reserved[2];
};

struct e4_tstorm_fcoe_conn_ag_ctx
{
	u8 reserved0 /* cdu_validation */;
	u8 state /* state */;
	u8 flags0;
#define E4_TSTORM_FCOE_CONN_AG_CTX_EXIST_IN_QM0_MASK          0x1 /* exist_in_qm0 */
#define E4_TSTORM_FCOE_CONN_AG_CTX_EXIST_IN_QM0_SHIFT         0
#define E4_TSTORM_FCOE_CONN_AG_CTX_BIT1_MASK                  0x1 /* exist_in_qm1 */
#define E4_TSTORM_FCOE_CONN_AG_CTX_BIT1_SHIFT                 1
#define E4_TSTORM_FCOE_CONN_AG_CTX_BIT2_MASK                  0x1 /* bit2 */
#define E4_TSTORM_FCOE_CONN_AG_CTX_BIT2_SHIFT                 2
#define E4_TSTORM_FCOE_CONN_AG_CTX_BIT3_MASK                  0x1 /* bit3 */
#define E4_TSTORM_FCOE_CONN_AG_CTX_BIT3_SHIFT                 3
#define E4_TSTORM_FCOE_CONN_AG_CTX_BIT4_MASK                  0x1 /* bit4 */
#define E4_TSTORM_FCOE_CONN_AG_CTX_BIT4_SHIFT                 4
#define E4_TSTORM_FCOE_CONN_AG_CTX_BIT5_MASK                  0x1 /* bit5 */
#define E4_TSTORM_FCOE_CONN_AG_CTX_BIT5_SHIFT                 5
#define E4_TSTORM_FCOE_CONN_AG_CTX_DUMMY_TIMER_CF_MASK        0x3 /* timer0cf */
#define E4_TSTORM_FCOE_CONN_AG_CTX_DUMMY_TIMER_CF_SHIFT       6
	u8 flags1;
#define E4_TSTORM_FCOE_CONN_AG_CTX_FLUSH_Q0_CF_MASK           0x3 /* timer1cf */
#define E4_TSTORM_FCOE_CONN_AG_CTX_FLUSH_Q0_CF_SHIFT          0
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF2_MASK                   0x3 /* timer2cf */
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF2_SHIFT                  2
#define E4_TSTORM_FCOE_CONN_AG_CTX_TIMER_STOP_ALL_CF_MASK     0x3 /* timer_stop_all */
#define E4_TSTORM_FCOE_CONN_AG_CTX_TIMER_STOP_ALL_CF_SHIFT    4
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF4_MASK                   0x3 /* cf4 */
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF4_SHIFT                  6
	u8 flags2;
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF5_MASK                   0x3 /* cf5 */
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF5_SHIFT                  0
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF6_MASK                   0x3 /* cf6 */
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF6_SHIFT                  2
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF7_MASK                   0x3 /* cf7 */
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF7_SHIFT                  4
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF8_MASK                   0x3 /* cf8 */
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF8_SHIFT                  6
	u8 flags3;
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF9_MASK                   0x3 /* cf9 */
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF9_SHIFT                  0
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF10_MASK                  0x3 /* cf10 */
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF10_SHIFT                 2
#define E4_TSTORM_FCOE_CONN_AG_CTX_DUMMY_TIMER_CF_EN_MASK     0x1 /* cf0en */
#define E4_TSTORM_FCOE_CONN_AG_CTX_DUMMY_TIMER_CF_EN_SHIFT    4
#define E4_TSTORM_FCOE_CONN_AG_CTX_FLUSH_Q0_CF_EN_MASK        0x1 /* cf1en */
#define E4_TSTORM_FCOE_CONN_AG_CTX_FLUSH_Q0_CF_EN_SHIFT       5
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF2EN_MASK                 0x1 /* cf2en */
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF2EN_SHIFT                6
#define E4_TSTORM_FCOE_CONN_AG_CTX_TIMER_STOP_ALL_CF_EN_MASK  0x1 /* cf3en */
#define E4_TSTORM_FCOE_CONN_AG_CTX_TIMER_STOP_ALL_CF_EN_SHIFT 7
	u8 flags4;
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF4EN_MASK                 0x1 /* cf4en */
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF4EN_SHIFT                0
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF5EN_MASK                 0x1 /* cf5en */
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF5EN_SHIFT                1
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF6EN_MASK                 0x1 /* cf6en */
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF6EN_SHIFT                2
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF7EN_MASK                 0x1 /* cf7en */
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF7EN_SHIFT                3
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF8EN_MASK                 0x1 /* cf8en */
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF8EN_SHIFT                4
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF9EN_MASK                 0x1 /* cf9en */
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF9EN_SHIFT                5
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF10EN_MASK                0x1 /* cf10en */
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF10EN_SHIFT               6
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE0EN_MASK               0x1 /* rule0en */
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE0EN_SHIFT              7
	u8 flags5;
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE1EN_MASK               0x1 /* rule1en */
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE1EN_SHIFT              0
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE2EN_MASK               0x1 /* rule2en */
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE2EN_SHIFT              1
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE3EN_MASK               0x1 /* rule3en */
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE3EN_SHIFT              2
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE4EN_MASK               0x1 /* rule4en */
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE4EN_SHIFT              3
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE5EN_MASK               0x1 /* rule5en */
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE5EN_SHIFT              4
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE6EN_MASK               0x1 /* rule6en */
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE6EN_SHIFT              5
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE7EN_MASK               0x1 /* rule7en */
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE7EN_SHIFT              6
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE8EN_MASK               0x1 /* rule8en */
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE8EN_SHIFT              7
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
};

struct e4_ustorm_fcoe_conn_ag_ctx
{
	u8 byte0 /* cdu_validation */;
	u8 byte1 /* state */;
	u8 flags0;
#define E4_USTORM_FCOE_CONN_AG_CTX_BIT0_MASK     0x1 /* exist_in_qm0 */
#define E4_USTORM_FCOE_CONN_AG_CTX_BIT0_SHIFT    0
#define E4_USTORM_FCOE_CONN_AG_CTX_BIT1_MASK     0x1 /* exist_in_qm1 */
#define E4_USTORM_FCOE_CONN_AG_CTX_BIT1_SHIFT    1
#define E4_USTORM_FCOE_CONN_AG_CTX_CF0_MASK      0x3 /* timer0cf */
#define E4_USTORM_FCOE_CONN_AG_CTX_CF0_SHIFT     2
#define E4_USTORM_FCOE_CONN_AG_CTX_CF1_MASK      0x3 /* timer1cf */
#define E4_USTORM_FCOE_CONN_AG_CTX_CF1_SHIFT     4
#define E4_USTORM_FCOE_CONN_AG_CTX_CF2_MASK      0x3 /* timer2cf */
#define E4_USTORM_FCOE_CONN_AG_CTX_CF2_SHIFT     6
	u8 flags1;
#define E4_USTORM_FCOE_CONN_AG_CTX_CF3_MASK      0x3 /* timer_stop_all */
#define E4_USTORM_FCOE_CONN_AG_CTX_CF3_SHIFT     0
#define E4_USTORM_FCOE_CONN_AG_CTX_CF4_MASK      0x3 /* cf4 */
#define E4_USTORM_FCOE_CONN_AG_CTX_CF4_SHIFT     2
#define E4_USTORM_FCOE_CONN_AG_CTX_CF5_MASK      0x3 /* cf5 */
#define E4_USTORM_FCOE_CONN_AG_CTX_CF5_SHIFT     4
#define E4_USTORM_FCOE_CONN_AG_CTX_CF6_MASK      0x3 /* cf6 */
#define E4_USTORM_FCOE_CONN_AG_CTX_CF6_SHIFT     6
	u8 flags2;
#define E4_USTORM_FCOE_CONN_AG_CTX_CF0EN_MASK    0x1 /* cf0en */
#define E4_USTORM_FCOE_CONN_AG_CTX_CF0EN_SHIFT   0
#define E4_USTORM_FCOE_CONN_AG_CTX_CF1EN_MASK    0x1 /* cf1en */
#define E4_USTORM_FCOE_CONN_AG_CTX_CF1EN_SHIFT   1
#define E4_USTORM_FCOE_CONN_AG_CTX_CF2EN_MASK    0x1 /* cf2en */
#define E4_USTORM_FCOE_CONN_AG_CTX_CF2EN_SHIFT   2
#define E4_USTORM_FCOE_CONN_AG_CTX_CF3EN_MASK    0x1 /* cf3en */
#define E4_USTORM_FCOE_CONN_AG_CTX_CF3EN_SHIFT   3
#define E4_USTORM_FCOE_CONN_AG_CTX_CF4EN_MASK    0x1 /* cf4en */
#define E4_USTORM_FCOE_CONN_AG_CTX_CF4EN_SHIFT   4
#define E4_USTORM_FCOE_CONN_AG_CTX_CF5EN_MASK    0x1 /* cf5en */
#define E4_USTORM_FCOE_CONN_AG_CTX_CF5EN_SHIFT   5
#define E4_USTORM_FCOE_CONN_AG_CTX_CF6EN_MASK    0x1 /* cf6en */
#define E4_USTORM_FCOE_CONN_AG_CTX_CF6EN_SHIFT   6
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE0EN_MASK  0x1 /* rule0en */
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE0EN_SHIFT 7
	u8 flags3;
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE1EN_MASK  0x1 /* rule1en */
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE1EN_SHIFT 0
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE2EN_MASK  0x1 /* rule2en */
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE2EN_SHIFT 1
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE3EN_MASK  0x1 /* rule3en */
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE3EN_SHIFT 2
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE4EN_MASK  0x1 /* rule4en */
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE4EN_SHIFT 3
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE5EN_MASK  0x1 /* rule5en */
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE5EN_SHIFT 4
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE6EN_MASK  0x1 /* rule6en */
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE6EN_SHIFT 5
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE7EN_MASK  0x1 /* rule7en */
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE7EN_SHIFT 6
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE8EN_MASK  0x1 /* rule8en */
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE8EN_SHIFT 7
	u8 byte2 /* byte2 */;
	u8 byte3 /* byte3 */;
	__le16 word0 /* conn_dpi */;
	__le16 word1 /* word1 */;
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
	__le32 reg2 /* reg2 */;
	__le32 reg3 /* reg3 */;
	__le16 word2 /* word2 */;
	__le16 word3 /* word3 */;
};

/*
 * The fcoe storm context of Tstorm
 */
struct tstorm_fcoe_conn_st_ctx
{
	__le16 stat_ram_addr /* Statistics ram adderss */;
	__le16 rx_max_fc_payload_len /* Max rx fc payload length. provided in ramrod */;
	__le16 e_d_tov_val /* E_D_TOV value negotiated during PLOGI (in msec) */;
	u8 flags;
#define TSTORM_FCOE_CONN_ST_CTX_INC_SEQ_CNT_MASK   0x1 /* Does the target support increment sequence counter */
#define TSTORM_FCOE_CONN_ST_CTX_INC_SEQ_CNT_SHIFT  0
#define TSTORM_FCOE_CONN_ST_CTX_SUPPORT_CONF_MASK  0x1 /* Does the connection support CONF REQ transmission */
#define TSTORM_FCOE_CONN_ST_CTX_SUPPORT_CONF_SHIFT 1
#define TSTORM_FCOE_CONN_ST_CTX_DEF_Q_IDX_MASK     0x3F /* Default queue index the connection associated to */
#define TSTORM_FCOE_CONN_ST_CTX_DEF_Q_IDX_SHIFT    2
	u8 timers_cleanup_invocation_cnt /* This variable is incremented each time the tStorm handler for timers cleanup is invoked within the same timers cleanup flow */;
	__le32 reserved1[2];
	__le32 dstMacAddressBytes0To3 /* destination MAC address: Bytes 0-3. */;
	__le16 dstMacAddressBytes4To5 /* destination MAC address: Bytes 4-5. */;
	__le16 ramrodEcho /* Saved ramrod echo - needed for 2nd round of terminate_conn (flush Q0) */;
	u8 flags1;
#define TSTORM_FCOE_CONN_ST_CTX_MODE_MASK          0x3 /* Indicate the mode of the connection: Target or Initiator, use enum fcoe_mode_type */
#define TSTORM_FCOE_CONN_ST_CTX_MODE_SHIFT         0
#define TSTORM_FCOE_CONN_ST_CTX_RESERVED_MASK      0x3F
#define TSTORM_FCOE_CONN_ST_CTX_RESERVED_SHIFT     2
	u8 cq_relative_offset /* CQ relative offset for connection */;
	u8 cmdq_relative_offset /* CmdQ relative offset for connection */;
	u8 bdq_resource_id /* The BDQ resource ID to which this function is mapped */;
	u8 reserved0[4] /* Alignment to 128b */;
};

struct e4_mstorm_fcoe_conn_ag_ctx
{
	u8 byte0 /* cdu_validation */;
	u8 byte1 /* state */;
	u8 flags0;
#define E4_MSTORM_FCOE_CONN_AG_CTX_BIT0_MASK     0x1 /* exist_in_qm0 */
#define E4_MSTORM_FCOE_CONN_AG_CTX_BIT0_SHIFT    0
#define E4_MSTORM_FCOE_CONN_AG_CTX_BIT1_MASK     0x1 /* exist_in_qm1 */
#define E4_MSTORM_FCOE_CONN_AG_CTX_BIT1_SHIFT    1
#define E4_MSTORM_FCOE_CONN_AG_CTX_CF0_MASK      0x3 /* cf0 */
#define E4_MSTORM_FCOE_CONN_AG_CTX_CF0_SHIFT     2
#define E4_MSTORM_FCOE_CONN_AG_CTX_CF1_MASK      0x3 /* cf1 */
#define E4_MSTORM_FCOE_CONN_AG_CTX_CF1_SHIFT     4
#define E4_MSTORM_FCOE_CONN_AG_CTX_CF2_MASK      0x3 /* cf2 */
#define E4_MSTORM_FCOE_CONN_AG_CTX_CF2_SHIFT     6
	u8 flags1;
#define E4_MSTORM_FCOE_CONN_AG_CTX_CF0EN_MASK    0x1 /* cf0en */
#define E4_MSTORM_FCOE_CONN_AG_CTX_CF0EN_SHIFT   0
#define E4_MSTORM_FCOE_CONN_AG_CTX_CF1EN_MASK    0x1 /* cf1en */
#define E4_MSTORM_FCOE_CONN_AG_CTX_CF1EN_SHIFT   1
#define E4_MSTORM_FCOE_CONN_AG_CTX_CF2EN_MASK    0x1 /* cf2en */
#define E4_MSTORM_FCOE_CONN_AG_CTX_CF2EN_SHIFT   2
#define E4_MSTORM_FCOE_CONN_AG_CTX_RULE0EN_MASK  0x1 /* rule0en */
#define E4_MSTORM_FCOE_CONN_AG_CTX_RULE0EN_SHIFT 3
#define E4_MSTORM_FCOE_CONN_AG_CTX_RULE1EN_MASK  0x1 /* rule1en */
#define E4_MSTORM_FCOE_CONN_AG_CTX_RULE1EN_SHIFT 4
#define E4_MSTORM_FCOE_CONN_AG_CTX_RULE2EN_MASK  0x1 /* rule2en */
#define E4_MSTORM_FCOE_CONN_AG_CTX_RULE2EN_SHIFT 5
#define E4_MSTORM_FCOE_CONN_AG_CTX_RULE3EN_MASK  0x1 /* rule3en */
#define E4_MSTORM_FCOE_CONN_AG_CTX_RULE3EN_SHIFT 6
#define E4_MSTORM_FCOE_CONN_AG_CTX_RULE4EN_MASK  0x1 /* rule4en */
#define E4_MSTORM_FCOE_CONN_AG_CTX_RULE4EN_SHIFT 7
	__le16 word0 /* word0 */;
	__le16 word1 /* word1 */;
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
};

/*
 * Fast path part of the fcoe storm context of Mstorm
 */
struct fcoe_mstorm_fcoe_conn_st_ctx_fp
{
	__le16 xfer_prod /* XferQ producer */;
	u8 num_cqs /* Number of CQs per function (internal to FW) */;
	u8 reserved1;
	u8 protection_info;
#define FCOE_MSTORM_FCOE_CONN_ST_CTX_FP_SUPPORT_PROTECTION_MASK  0x1 /* Does this connection support protection (if couple of GOS share this connection it is enough that one of them support protection) */
#define FCOE_MSTORM_FCOE_CONN_ST_CTX_FP_SUPPORT_PROTECTION_SHIFT 0
#define FCOE_MSTORM_FCOE_CONN_ST_CTX_FP_VALID_MASK               0x1 /* Are we in protection perf mode (there is only one protection mode for this connection and we manage to create mss that contain fixed amount of protection segment and we are only restrict by the target limitation and not line mss â€  this is critical since if line mss restrict us we canâ€™t rely on this size â€  it depends on vlan num) */
#define FCOE_MSTORM_FCOE_CONN_ST_CTX_FP_VALID_SHIFT              1
#define FCOE_MSTORM_FCOE_CONN_ST_CTX_FP_RESERVED0_MASK           0x3F
#define FCOE_MSTORM_FCOE_CONN_ST_CTX_FP_RESERVED0_SHIFT          2
	u8 q_relative_offset /* CQ, RQ and CMDQ relative offset for connection */;
	u8 reserved2[2];
};

/*
 * Non fast path part of the fcoe storm context of Mstorm
 */
struct fcoe_mstorm_fcoe_conn_st_ctx_non_fp
{
	__le16 conn_id /* Driver connection ID. To be used by slowpaths to fill EQ placement params */;
	__le16 stat_ram_addr /* Statistics ram adderss */;
	__le16 num_pages_in_pbl /* Number of XferQ/RespQ pbl pages (both have same wqe size) */;
	u8 ptu_log_page_size /* 0-4K, 1-8K, 2-16K, 3-32K... */;
	u8 log_page_size;
	__le16 unsolicited_cq_count /* Counts number of CQs done due to unsolicited packets on this connection */;
	__le16 cmdq_count /* Counts number of CMDQs done on this connection */;
	u8 bdq_resource_id /* BDQ Resource ID */;
	u8 reserved0[3] /* Padding bytes for 2nd RegPair */;
	struct regpair xferq_pbl_addr /* XferQ Pbl base address */;
	struct regpair reserved1;
	struct regpair reserved2[3];
};

/*
 * The fcoe storm context of Mstorm
 */
struct mstorm_fcoe_conn_st_ctx
{
	struct fcoe_mstorm_fcoe_conn_st_ctx_fp fp /* Fast path part of the fcoe storm context of Mstorm */;
	struct fcoe_mstorm_fcoe_conn_st_ctx_non_fp non_fp /* Non fast path part of the fcoe storm context of Mstorm */;
};

/*
 * fcoe connection context
 */
struct e4_fcoe_conn_context
{
	struct ystorm_fcoe_conn_st_ctx ystorm_st_context /* ystorm storm context */;
	struct pstorm_fcoe_conn_st_ctx pstorm_st_context /* pstorm storm context */;
	struct regpair pstorm_st_padding[2] /* padding */;
	struct xstorm_fcoe_conn_st_ctx xstorm_st_context /* xstorm storm context */;
	struct e4_xstorm_fcoe_conn_ag_ctx xstorm_ag_context /* xstorm aggregative context */;
	struct regpair xstorm_ag_padding[6] /* padding */;
	struct ustorm_fcoe_conn_st_ctx ustorm_st_context /* ustorm storm context */;
	struct regpair ustorm_st_padding[2] /* padding */;
	struct e4_tstorm_fcoe_conn_ag_ctx tstorm_ag_context /* tstorm aggregative context */;
	struct regpair tstorm_ag_padding[2] /* padding */;
	struct timers_context timer_context /* timer context */;
	struct e4_ustorm_fcoe_conn_ag_ctx ustorm_ag_context /* ustorm aggregative context */;
	struct tstorm_fcoe_conn_st_ctx tstorm_st_context /* tstorm storm context */;
	struct e4_mstorm_fcoe_conn_ag_ctx mstorm_ag_context /* mstorm aggregative context */;
	struct mstorm_fcoe_conn_st_ctx mstorm_st_context /* mstorm storm context */;
};


struct e5_xstorm_fcoe_conn_ag_ctx
{
	u8 reserved0 /* cdu_validation */;
	u8 state_and_core_id /* state_and_core_id */;
	u8 flags0;
#define E5_XSTORM_FCOE_CONN_AG_CTX_EXIST_IN_QM0_MASK       0x1 /* exist_in_qm0 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_EXIST_IN_QM0_SHIFT      0
#define E5_XSTORM_FCOE_CONN_AG_CTX_RESERVED1_MASK          0x1 /* exist_in_qm1 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_RESERVED1_SHIFT         1
#define E5_XSTORM_FCOE_CONN_AG_CTX_RESERVED2_MASK          0x1 /* exist_in_qm2 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_RESERVED2_SHIFT         2
#define E5_XSTORM_FCOE_CONN_AG_CTX_EXIST_IN_QM3_MASK       0x1 /* exist_in_qm3 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_EXIST_IN_QM3_SHIFT      3
#define E5_XSTORM_FCOE_CONN_AG_CTX_RESERVED3_MASK          0x1 /* bit4 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_RESERVED3_SHIFT         4
#define E5_XSTORM_FCOE_CONN_AG_CTX_RESERVED4_MASK          0x1 /* cf_array_active */
#define E5_XSTORM_FCOE_CONN_AG_CTX_RESERVED4_SHIFT         5
#define E5_XSTORM_FCOE_CONN_AG_CTX_RESERVED5_MASK          0x1 /* bit6 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_RESERVED5_SHIFT         6
#define E5_XSTORM_FCOE_CONN_AG_CTX_RESERVED6_MASK          0x1 /* bit7 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_RESERVED6_SHIFT         7
	u8 flags1;
#define E5_XSTORM_FCOE_CONN_AG_CTX_RESERVED7_MASK          0x1 /* bit8 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_RESERVED7_SHIFT         0
#define E5_XSTORM_FCOE_CONN_AG_CTX_RESERVED8_MASK          0x1 /* bit9 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_RESERVED8_SHIFT         1
#define E5_XSTORM_FCOE_CONN_AG_CTX_RESERVED9_MASK          0x1 /* bit10 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_RESERVED9_SHIFT         2
#define E5_XSTORM_FCOE_CONN_AG_CTX_BIT11_MASK              0x1 /* bit11 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_BIT11_SHIFT             3
#define E5_XSTORM_FCOE_CONN_AG_CTX_BIT12_MASK              0x1 /* bit12 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_BIT12_SHIFT             4
#define E5_XSTORM_FCOE_CONN_AG_CTX_BIT13_MASK              0x1 /* bit13 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_BIT13_SHIFT             5
#define E5_XSTORM_FCOE_CONN_AG_CTX_BIT14_MASK              0x1 /* bit14 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_BIT14_SHIFT             6
#define E5_XSTORM_FCOE_CONN_AG_CTX_BIT15_MASK              0x1 /* bit15 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_BIT15_SHIFT             7
	u8 flags2;
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF0_MASK                0x3 /* timer0cf */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF0_SHIFT               0
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF1_MASK                0x3 /* timer1cf */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF1_SHIFT               2
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF2_MASK                0x3 /* timer2cf */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF2_SHIFT               4
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF3_MASK                0x3 /* timer_stop_all */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF3_SHIFT               6
	u8 flags3;
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF4_MASK                0x3 /* cf4 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF4_SHIFT               0
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF5_MASK                0x3 /* cf5 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF5_SHIFT               2
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF6_MASK                0x3 /* cf6 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF6_SHIFT               4
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF7_MASK                0x3 /* cf7 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF7_SHIFT               6
	u8 flags4;
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF8_MASK                0x3 /* cf8 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF8_SHIFT               0
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF9_MASK                0x3 /* cf9 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF9_SHIFT               2
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF10_MASK               0x3 /* cf10 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF10_SHIFT              4
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF11_MASK               0x3 /* cf11 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF11_SHIFT              6
	u8 flags5;
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF12_MASK               0x3 /* cf12 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF12_SHIFT              0
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF13_MASK               0x3 /* cf13 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF13_SHIFT              2
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF14_MASK               0x3 /* cf14 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF14_SHIFT              4
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF15_MASK               0x3 /* cf15 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF15_SHIFT              6
	u8 flags6;
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF16_MASK               0x3 /* cf16 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF16_SHIFT              0
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF17_MASK               0x3 /* cf_array_cf */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF17_SHIFT              2
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF18_MASK               0x3 /* cf18 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF18_SHIFT              4
#define E5_XSTORM_FCOE_CONN_AG_CTX_DQ_CF_MASK              0x3 /* cf19 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_DQ_CF_SHIFT             6
	u8 flags7;
#define E5_XSTORM_FCOE_CONN_AG_CTX_FLUSH_Q0_MASK           0x3 /* cf20 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_FLUSH_Q0_SHIFT          0
#define E5_XSTORM_FCOE_CONN_AG_CTX_RESERVED10_MASK         0x3 /* cf21 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_RESERVED10_SHIFT        2
#define E5_XSTORM_FCOE_CONN_AG_CTX_SLOW_PATH_MASK          0x3 /* cf22 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_SLOW_PATH_SHIFT         4
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF0EN_MASK              0x1 /* cf0en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF0EN_SHIFT             6
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF1EN_MASK              0x1 /* cf1en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF1EN_SHIFT             7
	u8 flags8;
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF2EN_MASK              0x1 /* cf2en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF2EN_SHIFT             0
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF3EN_MASK              0x1 /* cf3en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF3EN_SHIFT             1
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF4EN_MASK              0x1 /* cf4en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF4EN_SHIFT             2
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF5EN_MASK              0x1 /* cf5en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF5EN_SHIFT             3
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF6EN_MASK              0x1 /* cf6en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF6EN_SHIFT             4
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF7EN_MASK              0x1 /* cf7en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF7EN_SHIFT             5
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF8EN_MASK              0x1 /* cf8en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF8EN_SHIFT             6
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF9EN_MASK              0x1 /* cf9en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF9EN_SHIFT             7
	u8 flags9;
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF10EN_MASK             0x1 /* cf10en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF10EN_SHIFT            0
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF11EN_MASK             0x1 /* cf11en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF11EN_SHIFT            1
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF12EN_MASK             0x1 /* cf12en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF12EN_SHIFT            2
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF13EN_MASK             0x1 /* cf13en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF13EN_SHIFT            3
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF14EN_MASK             0x1 /* cf14en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF14EN_SHIFT            4
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF15EN_MASK             0x1 /* cf15en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF15EN_SHIFT            5
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF16EN_MASK             0x1 /* cf16en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF16EN_SHIFT            6
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF17EN_MASK             0x1 /* cf_array_cf_en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF17EN_SHIFT            7
	u8 flags10;
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF18EN_MASK             0x1 /* cf18en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF18EN_SHIFT            0
#define E5_XSTORM_FCOE_CONN_AG_CTX_DQ_CF_EN_MASK           0x1 /* cf19en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_DQ_CF_EN_SHIFT          1
#define E5_XSTORM_FCOE_CONN_AG_CTX_FLUSH_Q0_EN_MASK        0x1 /* cf20en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_FLUSH_Q0_EN_SHIFT       2
#define E5_XSTORM_FCOE_CONN_AG_CTX_RESERVED11_MASK         0x1 /* cf21en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_RESERVED11_SHIFT        3
#define E5_XSTORM_FCOE_CONN_AG_CTX_SLOW_PATH_EN_MASK       0x1 /* cf22en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_SLOW_PATH_EN_SHIFT      4
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF23EN_MASK             0x1 /* cf23en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF23EN_SHIFT            5
#define E5_XSTORM_FCOE_CONN_AG_CTX_RESERVED12_MASK         0x1 /* rule0en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_RESERVED12_SHIFT        6
#define E5_XSTORM_FCOE_CONN_AG_CTX_RESERVED13_MASK         0x1 /* rule1en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_RESERVED13_SHIFT        7
	u8 flags11;
#define E5_XSTORM_FCOE_CONN_AG_CTX_RESERVED14_MASK         0x1 /* rule2en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_RESERVED14_SHIFT        0
#define E5_XSTORM_FCOE_CONN_AG_CTX_RESERVED15_MASK         0x1 /* rule3en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_RESERVED15_SHIFT        1
#define E5_XSTORM_FCOE_CONN_AG_CTX_RESERVED16_MASK         0x1 /* rule4en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_RESERVED16_SHIFT        2
#define E5_XSTORM_FCOE_CONN_AG_CTX_RULE5EN_MASK            0x1 /* rule5en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_RULE5EN_SHIFT           3
#define E5_XSTORM_FCOE_CONN_AG_CTX_RULE6EN_MASK            0x1 /* rule6en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_RULE6EN_SHIFT           4
#define E5_XSTORM_FCOE_CONN_AG_CTX_RULE7EN_MASK            0x1 /* rule7en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_RULE7EN_SHIFT           5
#define E5_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED1_MASK       0x1 /* rule8en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED1_SHIFT      6
#define E5_XSTORM_FCOE_CONN_AG_CTX_XFERQ_DECISION_EN_MASK  0x1 /* rule9en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_XFERQ_DECISION_EN_SHIFT 7
	u8 flags12;
#define E5_XSTORM_FCOE_CONN_AG_CTX_SQ_DECISION_EN_MASK     0x1 /* rule10en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_SQ_DECISION_EN_SHIFT    0
#define E5_XSTORM_FCOE_CONN_AG_CTX_RULE11EN_MASK           0x1 /* rule11en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_RULE11EN_SHIFT          1
#define E5_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED2_MASK       0x1 /* rule12en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED2_SHIFT      2
#define E5_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED3_MASK       0x1 /* rule13en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED3_SHIFT      3
#define E5_XSTORM_FCOE_CONN_AG_CTX_RULE14EN_MASK           0x1 /* rule14en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_RULE14EN_SHIFT          4
#define E5_XSTORM_FCOE_CONN_AG_CTX_RULE15EN_MASK           0x1 /* rule15en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_RULE15EN_SHIFT          5
#define E5_XSTORM_FCOE_CONN_AG_CTX_RULE16EN_MASK           0x1 /* rule16en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_RULE16EN_SHIFT          6
#define E5_XSTORM_FCOE_CONN_AG_CTX_RULE17EN_MASK           0x1 /* rule17en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_RULE17EN_SHIFT          7
	u8 flags13;
#define E5_XSTORM_FCOE_CONN_AG_CTX_RESPQ_DECISION_EN_MASK  0x1 /* rule18en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_RESPQ_DECISION_EN_SHIFT 0
#define E5_XSTORM_FCOE_CONN_AG_CTX_RULE19EN_MASK           0x1 /* rule19en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_RULE19EN_SHIFT          1
#define E5_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED4_MASK       0x1 /* rule20en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED4_SHIFT      2
#define E5_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED5_MASK       0x1 /* rule21en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED5_SHIFT      3
#define E5_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED6_MASK       0x1 /* rule22en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED6_SHIFT      4
#define E5_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED7_MASK       0x1 /* rule23en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED7_SHIFT      5
#define E5_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED8_MASK       0x1 /* rule24en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED8_SHIFT      6
#define E5_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED9_MASK       0x1 /* rule25en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED9_SHIFT      7
	u8 flags14;
#define E5_XSTORM_FCOE_CONN_AG_CTX_BIT16_MASK              0x1 /* bit16 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_BIT16_SHIFT             0
#define E5_XSTORM_FCOE_CONN_AG_CTX_BIT17_MASK              0x1 /* bit17 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_BIT17_SHIFT             1
#define E5_XSTORM_FCOE_CONN_AG_CTX_BIT18_MASK              0x1 /* bit18 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_BIT18_SHIFT             2
#define E5_XSTORM_FCOE_CONN_AG_CTX_BIT19_MASK              0x1 /* bit19 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_BIT19_SHIFT             3
#define E5_XSTORM_FCOE_CONN_AG_CTX_BIT20_MASK              0x1 /* bit20 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_BIT20_SHIFT             4
#define E5_XSTORM_FCOE_CONN_AG_CTX_BIT21_MASK              0x1 /* bit21 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_BIT21_SHIFT             5
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF23_MASK               0x3 /* cf23 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_CF23_SHIFT              6
	u8 byte2 /* byte2 */;
	__le16 physical_q0 /* physical_q0 */;
	__le16 word1 /* physical_q1 */;
	__le16 word2 /* physical_q2 */;
	__le16 sq_cons /* word3 */;
	__le16 sq_prod /* word4 */;
	__le16 xferq_prod /* word5 */;
	__le16 xferq_cons /* conn_dpi */;
	u8 byte3 /* byte3 */;
	u8 byte4 /* byte4 */;
	u8 byte5 /* byte5 */;
	u8 byte6 /* byte6 */;
	__le32 remain_io /* reg0 */;
	__le32 reg1 /* reg1 */;
	__le32 reg2 /* reg2 */;
	__le32 reg3 /* reg3 */;
	__le32 reg4 /* reg4 */;
	__le32 reg5 /* cf_array0 */;
	__le32 reg6 /* cf_array1 */;
	u8 flags15;
#define E5_XSTORM_FCOE_CONN_AG_CTX_E4_RESERVED1_MASK       0x1 /* bit22 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_E4_RESERVED1_SHIFT      0
#define E5_XSTORM_FCOE_CONN_AG_CTX_E4_RESERVED2_MASK       0x1 /* bit23 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_E4_RESERVED2_SHIFT      1
#define E5_XSTORM_FCOE_CONN_AG_CTX_E4_RESERVED3_MASK       0x1 /* bit24 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_E4_RESERVED3_SHIFT      2
#define E5_XSTORM_FCOE_CONN_AG_CTX_E4_RESERVED4_MASK       0x3 /* cf24 */
#define E5_XSTORM_FCOE_CONN_AG_CTX_E4_RESERVED4_SHIFT      3
#define E5_XSTORM_FCOE_CONN_AG_CTX_E4_RESERVED5_MASK       0x1 /* cf24en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_E4_RESERVED5_SHIFT      5
#define E5_XSTORM_FCOE_CONN_AG_CTX_E4_RESERVED6_MASK       0x1 /* rule26en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_E4_RESERVED6_SHIFT      6
#define E5_XSTORM_FCOE_CONN_AG_CTX_E4_RESERVED7_MASK       0x1 /* rule27en */
#define E5_XSTORM_FCOE_CONN_AG_CTX_E4_RESERVED7_SHIFT      7
	u8 byte7 /* byte7 */;
	__le16 respq_prod /* word7 */;
	__le16 respq_cons /* word8 */;
	__le16 word9 /* word9 */;
	__le16 word10 /* word10 */;
	__le16 word11 /* word11 */;
	__le32 reg7 /* reg7 */;
};

struct e5_tstorm_fcoe_conn_ag_ctx
{
	u8 reserved0 /* cdu_validation */;
	u8 state_and_core_id /* state_and_core_id */;
	u8 flags0;
#define E5_TSTORM_FCOE_CONN_AG_CTX_EXIST_IN_QM0_MASK          0x1 /* exist_in_qm0 */
#define E5_TSTORM_FCOE_CONN_AG_CTX_EXIST_IN_QM0_SHIFT         0
#define E5_TSTORM_FCOE_CONN_AG_CTX_BIT1_MASK                  0x1 /* exist_in_qm1 */
#define E5_TSTORM_FCOE_CONN_AG_CTX_BIT1_SHIFT                 1
#define E5_TSTORM_FCOE_CONN_AG_CTX_BIT2_MASK                  0x1 /* bit2 */
#define E5_TSTORM_FCOE_CONN_AG_CTX_BIT2_SHIFT                 2
#define E5_TSTORM_FCOE_CONN_AG_CTX_BIT3_MASK                  0x1 /* bit3 */
#define E5_TSTORM_FCOE_CONN_AG_CTX_BIT3_SHIFT                 3
#define E5_TSTORM_FCOE_CONN_AG_CTX_BIT4_MASK                  0x1 /* bit4 */
#define E5_TSTORM_FCOE_CONN_AG_CTX_BIT4_SHIFT                 4
#define E5_TSTORM_FCOE_CONN_AG_CTX_BIT5_MASK                  0x1 /* bit5 */
#define E5_TSTORM_FCOE_CONN_AG_CTX_BIT5_SHIFT                 5
#define E5_TSTORM_FCOE_CONN_AG_CTX_DUMMY_TIMER_CF_MASK        0x3 /* timer0cf */
#define E5_TSTORM_FCOE_CONN_AG_CTX_DUMMY_TIMER_CF_SHIFT       6
	u8 flags1;
#define E5_TSTORM_FCOE_CONN_AG_CTX_FLUSH_Q0_CF_MASK           0x3 /* timer1cf */
#define E5_TSTORM_FCOE_CONN_AG_CTX_FLUSH_Q0_CF_SHIFT          0
#define E5_TSTORM_FCOE_CONN_AG_CTX_CF2_MASK                   0x3 /* timer2cf */
#define E5_TSTORM_FCOE_CONN_AG_CTX_CF2_SHIFT                  2
#define E5_TSTORM_FCOE_CONN_AG_CTX_TIMER_STOP_ALL_CF_MASK     0x3 /* timer_stop_all */
#define E5_TSTORM_FCOE_CONN_AG_CTX_TIMER_STOP_ALL_CF_SHIFT    4
#define E5_TSTORM_FCOE_CONN_AG_CTX_CF4_MASK                   0x3 /* cf4 */
#define E5_TSTORM_FCOE_CONN_AG_CTX_CF4_SHIFT                  6
	u8 flags2;
#define E5_TSTORM_FCOE_CONN_AG_CTX_CF5_MASK                   0x3 /* cf5 */
#define E5_TSTORM_FCOE_CONN_AG_CTX_CF5_SHIFT                  0
#define E5_TSTORM_FCOE_CONN_AG_CTX_CF6_MASK                   0x3 /* cf6 */
#define E5_TSTORM_FCOE_CONN_AG_CTX_CF6_SHIFT                  2
#define E5_TSTORM_FCOE_CONN_AG_CTX_CF7_MASK                   0x3 /* cf7 */
#define E5_TSTORM_FCOE_CONN_AG_CTX_CF7_SHIFT                  4
#define E5_TSTORM_FCOE_CONN_AG_CTX_CF8_MASK                   0x3 /* cf8 */
#define E5_TSTORM_FCOE_CONN_AG_CTX_CF8_SHIFT                  6
	u8 flags3;
#define E5_TSTORM_FCOE_CONN_AG_CTX_CF9_MASK                   0x3 /* cf9 */
#define E5_TSTORM_FCOE_CONN_AG_CTX_CF9_SHIFT                  0
#define E5_TSTORM_FCOE_CONN_AG_CTX_CF10_MASK                  0x3 /* cf10 */
#define E5_TSTORM_FCOE_CONN_AG_CTX_CF10_SHIFT                 2
#define E5_TSTORM_FCOE_CONN_AG_CTX_DUMMY_TIMER_CF_EN_MASK     0x1 /* cf0en */
#define E5_TSTORM_FCOE_CONN_AG_CTX_DUMMY_TIMER_CF_EN_SHIFT    4
#define E5_TSTORM_FCOE_CONN_AG_CTX_FLUSH_Q0_CF_EN_MASK        0x1 /* cf1en */
#define E5_TSTORM_FCOE_CONN_AG_CTX_FLUSH_Q0_CF_EN_SHIFT       5
#define E5_TSTORM_FCOE_CONN_AG_CTX_CF2EN_MASK                 0x1 /* cf2en */
#define E5_TSTORM_FCOE_CONN_AG_CTX_CF2EN_SHIFT                6
#define E5_TSTORM_FCOE_CONN_AG_CTX_TIMER_STOP_ALL_CF_EN_MASK  0x1 /* cf3en */
#define E5_TSTORM_FCOE_CONN_AG_CTX_TIMER_STOP_ALL_CF_EN_SHIFT 7
	u8 flags4;
#define E5_TSTORM_FCOE_CONN_AG_CTX_CF4EN_MASK                 0x1 /* cf4en */
#define E5_TSTORM_FCOE_CONN_AG_CTX_CF4EN_SHIFT                0
#define E5_TSTORM_FCOE_CONN_AG_CTX_CF5EN_MASK                 0x1 /* cf5en */
#define E5_TSTORM_FCOE_CONN_AG_CTX_CF5EN_SHIFT                1
#define E5_TSTORM_FCOE_CONN_AG_CTX_CF6EN_MASK                 0x1 /* cf6en */
#define E5_TSTORM_FCOE_CONN_AG_CTX_CF6EN_SHIFT                2
#define E5_TSTORM_FCOE_CONN_AG_CTX_CF7EN_MASK                 0x1 /* cf7en */
#define E5_TSTORM_FCOE_CONN_AG_CTX_CF7EN_SHIFT                3
#define E5_TSTORM_FCOE_CONN_AG_CTX_CF8EN_MASK                 0x1 /* cf8en */
#define E5_TSTORM_FCOE_CONN_AG_CTX_CF8EN_SHIFT                4
#define E5_TSTORM_FCOE_CONN_AG_CTX_CF9EN_MASK                 0x1 /* cf9en */
#define E5_TSTORM_FCOE_CONN_AG_CTX_CF9EN_SHIFT                5
#define E5_TSTORM_FCOE_CONN_AG_CTX_CF10EN_MASK                0x1 /* cf10en */
#define E5_TSTORM_FCOE_CONN_AG_CTX_CF10EN_SHIFT               6
#define E5_TSTORM_FCOE_CONN_AG_CTX_RULE0EN_MASK               0x1 /* rule0en */
#define E5_TSTORM_FCOE_CONN_AG_CTX_RULE0EN_SHIFT              7
	u8 flags5;
#define E5_TSTORM_FCOE_CONN_AG_CTX_RULE1EN_MASK               0x1 /* rule1en */
#define E5_TSTORM_FCOE_CONN_AG_CTX_RULE1EN_SHIFT              0
#define E5_TSTORM_FCOE_CONN_AG_CTX_RULE2EN_MASK               0x1 /* rule2en */
#define E5_TSTORM_FCOE_CONN_AG_CTX_RULE2EN_SHIFT              1
#define E5_TSTORM_FCOE_CONN_AG_CTX_RULE3EN_MASK               0x1 /* rule3en */
#define E5_TSTORM_FCOE_CONN_AG_CTX_RULE3EN_SHIFT              2
#define E5_TSTORM_FCOE_CONN_AG_CTX_RULE4EN_MASK               0x1 /* rule4en */
#define E5_TSTORM_FCOE_CONN_AG_CTX_RULE4EN_SHIFT              3
#define E5_TSTORM_FCOE_CONN_AG_CTX_RULE5EN_MASK               0x1 /* rule5en */
#define E5_TSTORM_FCOE_CONN_AG_CTX_RULE5EN_SHIFT              4
#define E5_TSTORM_FCOE_CONN_AG_CTX_RULE6EN_MASK               0x1 /* rule6en */
#define E5_TSTORM_FCOE_CONN_AG_CTX_RULE6EN_SHIFT              5
#define E5_TSTORM_FCOE_CONN_AG_CTX_RULE7EN_MASK               0x1 /* rule7en */
#define E5_TSTORM_FCOE_CONN_AG_CTX_RULE7EN_SHIFT              6
#define E5_TSTORM_FCOE_CONN_AG_CTX_RULE8EN_MASK               0x1 /* rule8en */
#define E5_TSTORM_FCOE_CONN_AG_CTX_RULE8EN_SHIFT              7
	u8 flags6;
#define E5_TSTORM_FCOE_CONN_AG_CTX_E4_RESERVED1_MASK          0x1 /* bit6 */
#define E5_TSTORM_FCOE_CONN_AG_CTX_E4_RESERVED1_SHIFT         0
#define E5_TSTORM_FCOE_CONN_AG_CTX_E4_RESERVED2_MASK          0x1 /* bit7 */
#define E5_TSTORM_FCOE_CONN_AG_CTX_E4_RESERVED2_SHIFT         1
#define E5_TSTORM_FCOE_CONN_AG_CTX_E4_RESERVED3_MASK          0x1 /* bit8 */
#define E5_TSTORM_FCOE_CONN_AG_CTX_E4_RESERVED3_SHIFT         2
#define E5_TSTORM_FCOE_CONN_AG_CTX_E4_RESERVED4_MASK          0x3 /* cf11 */
#define E5_TSTORM_FCOE_CONN_AG_CTX_E4_RESERVED4_SHIFT         3
#define E5_TSTORM_FCOE_CONN_AG_CTX_E4_RESERVED5_MASK          0x1 /* cf11en */
#define E5_TSTORM_FCOE_CONN_AG_CTX_E4_RESERVED5_SHIFT         5
#define E5_TSTORM_FCOE_CONN_AG_CTX_E4_RESERVED6_MASK          0x1 /* rule9en */
#define E5_TSTORM_FCOE_CONN_AG_CTX_E4_RESERVED6_SHIFT         6
#define E5_TSTORM_FCOE_CONN_AG_CTX_E4_RESERVED7_MASK          0x1 /* rule10en */
#define E5_TSTORM_FCOE_CONN_AG_CTX_E4_RESERVED7_SHIFT         7
	u8 byte2 /* byte2 */;
	__le16 word0 /* word0 */;
	__le32 reg0 /* reg0 */;
};

struct e5_ustorm_fcoe_conn_ag_ctx
{
	u8 byte0 /* cdu_validation */;
	u8 byte1 /* state_and_core_id */;
	u8 flags0;
#define E5_USTORM_FCOE_CONN_AG_CTX_BIT0_MASK          0x1 /* exist_in_qm0 */
#define E5_USTORM_FCOE_CONN_AG_CTX_BIT0_SHIFT         0
#define E5_USTORM_FCOE_CONN_AG_CTX_BIT1_MASK          0x1 /* exist_in_qm1 */
#define E5_USTORM_FCOE_CONN_AG_CTX_BIT1_SHIFT         1
#define E5_USTORM_FCOE_CONN_AG_CTX_CF0_MASK           0x3 /* timer0cf */
#define E5_USTORM_FCOE_CONN_AG_CTX_CF0_SHIFT          2
#define E5_USTORM_FCOE_CONN_AG_CTX_CF1_MASK           0x3 /* timer1cf */
#define E5_USTORM_FCOE_CONN_AG_CTX_CF1_SHIFT          4
#define E5_USTORM_FCOE_CONN_AG_CTX_CF2_MASK           0x3 /* timer2cf */
#define E5_USTORM_FCOE_CONN_AG_CTX_CF2_SHIFT          6
	u8 flags1;
#define E5_USTORM_FCOE_CONN_AG_CTX_CF3_MASK           0x3 /* timer_stop_all */
#define E5_USTORM_FCOE_CONN_AG_CTX_CF3_SHIFT          0
#define E5_USTORM_FCOE_CONN_AG_CTX_CF4_MASK           0x3 /* cf4 */
#define E5_USTORM_FCOE_CONN_AG_CTX_CF4_SHIFT          2
#define E5_USTORM_FCOE_CONN_AG_CTX_CF5_MASK           0x3 /* cf5 */
#define E5_USTORM_FCOE_CONN_AG_CTX_CF5_SHIFT          4
#define E5_USTORM_FCOE_CONN_AG_CTX_CF6_MASK           0x3 /* cf6 */
#define E5_USTORM_FCOE_CONN_AG_CTX_CF6_SHIFT          6
	u8 flags2;
#define E5_USTORM_FCOE_CONN_AG_CTX_CF0EN_MASK         0x1 /* cf0en */
#define E5_USTORM_FCOE_CONN_AG_CTX_CF0EN_SHIFT        0
#define E5_USTORM_FCOE_CONN_AG_CTX_CF1EN_MASK         0x1 /* cf1en */
#define E5_USTORM_FCOE_CONN_AG_CTX_CF1EN_SHIFT        1
#define E5_USTORM_FCOE_CONN_AG_CTX_CF2EN_MASK         0x1 /* cf2en */
#define E5_USTORM_FCOE_CONN_AG_CTX_CF2EN_SHIFT        2
#define E5_USTORM_FCOE_CONN_AG_CTX_CF3EN_MASK         0x1 /* cf3en */
#define E5_USTORM_FCOE_CONN_AG_CTX_CF3EN_SHIFT        3
#define E5_USTORM_FCOE_CONN_AG_CTX_CF4EN_MASK         0x1 /* cf4en */
#define E5_USTORM_FCOE_CONN_AG_CTX_CF4EN_SHIFT        4
#define E5_USTORM_FCOE_CONN_AG_CTX_CF5EN_MASK         0x1 /* cf5en */
#define E5_USTORM_FCOE_CONN_AG_CTX_CF5EN_SHIFT        5
#define E5_USTORM_FCOE_CONN_AG_CTX_CF6EN_MASK         0x1 /* cf6en */
#define E5_USTORM_FCOE_CONN_AG_CTX_CF6EN_SHIFT        6
#define E5_USTORM_FCOE_CONN_AG_CTX_RULE0EN_MASK       0x1 /* rule0en */
#define E5_USTORM_FCOE_CONN_AG_CTX_RULE0EN_SHIFT      7
	u8 flags3;
#define E5_USTORM_FCOE_CONN_AG_CTX_RULE1EN_MASK       0x1 /* rule1en */
#define E5_USTORM_FCOE_CONN_AG_CTX_RULE1EN_SHIFT      0
#define E5_USTORM_FCOE_CONN_AG_CTX_RULE2EN_MASK       0x1 /* rule2en */
#define E5_USTORM_FCOE_CONN_AG_CTX_RULE2EN_SHIFT      1
#define E5_USTORM_FCOE_CONN_AG_CTX_RULE3EN_MASK       0x1 /* rule3en */
#define E5_USTORM_FCOE_CONN_AG_CTX_RULE3EN_SHIFT      2
#define E5_USTORM_FCOE_CONN_AG_CTX_RULE4EN_MASK       0x1 /* rule4en */
#define E5_USTORM_FCOE_CONN_AG_CTX_RULE4EN_SHIFT      3
#define E5_USTORM_FCOE_CONN_AG_CTX_RULE5EN_MASK       0x1 /* rule5en */
#define E5_USTORM_FCOE_CONN_AG_CTX_RULE5EN_SHIFT      4
#define E5_USTORM_FCOE_CONN_AG_CTX_RULE6EN_MASK       0x1 /* rule6en */
#define E5_USTORM_FCOE_CONN_AG_CTX_RULE6EN_SHIFT      5
#define E5_USTORM_FCOE_CONN_AG_CTX_RULE7EN_MASK       0x1 /* rule7en */
#define E5_USTORM_FCOE_CONN_AG_CTX_RULE7EN_SHIFT      6
#define E5_USTORM_FCOE_CONN_AG_CTX_RULE8EN_MASK       0x1 /* rule8en */
#define E5_USTORM_FCOE_CONN_AG_CTX_RULE8EN_SHIFT      7
	u8 flags4;
#define E5_USTORM_FCOE_CONN_AG_CTX_E4_RESERVED1_MASK  0x1 /* bit2 */
#define E5_USTORM_FCOE_CONN_AG_CTX_E4_RESERVED1_SHIFT 0
#define E5_USTORM_FCOE_CONN_AG_CTX_E4_RESERVED2_MASK  0x1 /* bit3 */
#define E5_USTORM_FCOE_CONN_AG_CTX_E4_RESERVED2_SHIFT 1
#define E5_USTORM_FCOE_CONN_AG_CTX_E4_RESERVED3_MASK  0x3 /* cf7 */
#define E5_USTORM_FCOE_CONN_AG_CTX_E4_RESERVED3_SHIFT 2
#define E5_USTORM_FCOE_CONN_AG_CTX_E4_RESERVED4_MASK  0x3 /* cf8 */
#define E5_USTORM_FCOE_CONN_AG_CTX_E4_RESERVED4_SHIFT 4
#define E5_USTORM_FCOE_CONN_AG_CTX_E4_RESERVED5_MASK  0x1 /* cf7en */
#define E5_USTORM_FCOE_CONN_AG_CTX_E4_RESERVED5_SHIFT 6
#define E5_USTORM_FCOE_CONN_AG_CTX_E4_RESERVED6_MASK  0x1 /* cf8en */
#define E5_USTORM_FCOE_CONN_AG_CTX_E4_RESERVED6_SHIFT 7
	u8 byte2 /* byte2 */;
	__le16 word0 /* conn_dpi */;
	__le16 word1 /* word1 */;
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
	__le32 reg2 /* reg2 */;
	__le32 reg3 /* reg3 */;
	__le16 word2 /* word2 */;
	__le16 word3 /* word3 */;
};

struct e5_mstorm_fcoe_conn_ag_ctx
{
	u8 byte0 /* cdu_validation */;
	u8 byte1 /* state_and_core_id */;
	u8 flags0;
#define E5_MSTORM_FCOE_CONN_AG_CTX_BIT0_MASK     0x1 /* exist_in_qm0 */
#define E5_MSTORM_FCOE_CONN_AG_CTX_BIT0_SHIFT    0
#define E5_MSTORM_FCOE_CONN_AG_CTX_BIT1_MASK     0x1 /* exist_in_qm1 */
#define E5_MSTORM_FCOE_CONN_AG_CTX_BIT1_SHIFT    1
#define E5_MSTORM_FCOE_CONN_AG_CTX_CF0_MASK      0x3 /* cf0 */
#define E5_MSTORM_FCOE_CONN_AG_CTX_CF0_SHIFT     2
#define E5_MSTORM_FCOE_CONN_AG_CTX_CF1_MASK      0x3 /* cf1 */
#define E5_MSTORM_FCOE_CONN_AG_CTX_CF1_SHIFT     4
#define E5_MSTORM_FCOE_CONN_AG_CTX_CF2_MASK      0x3 /* cf2 */
#define E5_MSTORM_FCOE_CONN_AG_CTX_CF2_SHIFT     6
	u8 flags1;
#define E5_MSTORM_FCOE_CONN_AG_CTX_CF0EN_MASK    0x1 /* cf0en */
#define E5_MSTORM_FCOE_CONN_AG_CTX_CF0EN_SHIFT   0
#define E5_MSTORM_FCOE_CONN_AG_CTX_CF1EN_MASK    0x1 /* cf1en */
#define E5_MSTORM_FCOE_CONN_AG_CTX_CF1EN_SHIFT   1
#define E5_MSTORM_FCOE_CONN_AG_CTX_CF2EN_MASK    0x1 /* cf2en */
#define E5_MSTORM_FCOE_CONN_AG_CTX_CF2EN_SHIFT   2
#define E5_MSTORM_FCOE_CONN_AG_CTX_RULE0EN_MASK  0x1 /* rule0en */
#define E5_MSTORM_FCOE_CONN_AG_CTX_RULE0EN_SHIFT 3
#define E5_MSTORM_FCOE_CONN_AG_CTX_RULE1EN_MASK  0x1 /* rule1en */
#define E5_MSTORM_FCOE_CONN_AG_CTX_RULE1EN_SHIFT 4
#define E5_MSTORM_FCOE_CONN_AG_CTX_RULE2EN_MASK  0x1 /* rule2en */
#define E5_MSTORM_FCOE_CONN_AG_CTX_RULE2EN_SHIFT 5
#define E5_MSTORM_FCOE_CONN_AG_CTX_RULE3EN_MASK  0x1 /* rule3en */
#define E5_MSTORM_FCOE_CONN_AG_CTX_RULE3EN_SHIFT 6
#define E5_MSTORM_FCOE_CONN_AG_CTX_RULE4EN_MASK  0x1 /* rule4en */
#define E5_MSTORM_FCOE_CONN_AG_CTX_RULE4EN_SHIFT 7
	__le16 word0 /* word0 */;
	__le16 word1 /* word1 */;
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
};

/*
 * fcoe connection context
 */
struct e5_fcoe_conn_context
{
	struct ystorm_fcoe_conn_st_ctx ystorm_st_context /* ystorm storm context */;
	struct pstorm_fcoe_conn_st_ctx pstorm_st_context /* pstorm storm context */;
	struct regpair pstorm_st_padding[2] /* padding */;
	struct xstorm_fcoe_conn_st_ctx xstorm_st_context /* xstorm storm context */;
	struct e5_xstorm_fcoe_conn_ag_ctx xstorm_ag_context /* xstorm aggregative context */;
	struct ustorm_fcoe_conn_st_ctx ustorm_st_context /* ustorm storm context */;
	struct regpair ustorm_st_padding[2] /* padding */;
	struct e5_tstorm_fcoe_conn_ag_ctx tstorm_ag_context /* tstorm aggregative context */;
	struct regpair tstorm_ag_padding[2] /* padding */;
	struct timers_context timer_context /* timer context */;
	struct e5_ustorm_fcoe_conn_ag_ctx ustorm_ag_context /* ustorm aggregative context */;
	struct tstorm_fcoe_conn_st_ctx tstorm_st_context /* tstorm storm context */;
	struct e5_mstorm_fcoe_conn_ag_ctx mstorm_ag_context /* mstorm aggregative context */;
	struct mstorm_fcoe_conn_st_ctx mstorm_st_context /* mstorm storm context */;
};


/*
 * FCoE connection offload params passed by driver to FW in FCoE offload ramrod 
 */
struct fcoe_conn_offload_ramrod_params
{
	struct fcoe_conn_offload_ramrod_data offload_ramrod_data;
};


/*
 * FCoE connection terminate params passed by driver to FW in FCoE terminate conn ramrod 
 */
struct fcoe_conn_terminate_ramrod_params
{
	struct fcoe_conn_terminate_ramrod_data terminate_ramrod_data;
};


/*
 * FCoE event type
 */
enum fcoe_event_type
{
	FCOE_EVENT_INIT_FUNC /* Slowpath completion on INIT_FUNC ramrod */,
	FCOE_EVENT_DESTROY_FUNC /* Slowpath completion on DESTROY_FUNC ramrod */,
	FCOE_EVENT_STAT_FUNC /* Slowpath completion on STAT_FUNC ramrod */,
	FCOE_EVENT_OFFLOAD_CONN /* Slowpath completion on OFFLOAD_CONN ramrod */,
	FCOE_EVENT_TERMINATE_CONN /* Slowpath completion on TERMINATE_CONN ramrod */,
	FCOE_EVENT_ERROR /* Error event */,
	MAX_FCOE_EVENT_TYPE
};


/*
 * FCoE init params passed by driver to FW in FCoE init ramrod 
 */
struct fcoe_init_ramrod_params
{
	struct fcoe_init_func_ramrod_data init_ramrod_data;
};




/*
 * FCoE ramrod Command IDs 
 */
enum fcoe_ramrod_cmd_id
{
	FCOE_RAMROD_CMD_ID_INIT_FUNC /* FCoE function init ramrod */,
	FCOE_RAMROD_CMD_ID_DESTROY_FUNC /* FCoE function destroy ramrod */,
	FCOE_RAMROD_CMD_ID_STAT_FUNC /* FCoE statistics ramrod */,
	FCOE_RAMROD_CMD_ID_OFFLOAD_CONN /* FCoE connection offload ramrod */,
	FCOE_RAMROD_CMD_ID_TERMINATE_CONN /* FCoE connection offload ramrod. Command ID known only to FW and VBD */,
	MAX_FCOE_RAMROD_CMD_ID
};


/*
 * FCoE statistics params buffer passed by driver to FW in FCoE statistics ramrod 
 */
struct fcoe_stat_ramrod_params
{
	struct fcoe_stat_ramrod_data stat_ramrod_data;
};
















struct e4_ystorm_fcoe_conn_ag_ctx
{
	u8 byte0 /* cdu_validation */;
	u8 byte1 /* state */;
	u8 flags0;
#define E4_YSTORM_FCOE_CONN_AG_CTX_BIT0_MASK     0x1 /* exist_in_qm0 */
#define E4_YSTORM_FCOE_CONN_AG_CTX_BIT0_SHIFT    0
#define E4_YSTORM_FCOE_CONN_AG_CTX_BIT1_MASK     0x1 /* exist_in_qm1 */
#define E4_YSTORM_FCOE_CONN_AG_CTX_BIT1_SHIFT    1
#define E4_YSTORM_FCOE_CONN_AG_CTX_CF0_MASK      0x3 /* cf0 */
#define E4_YSTORM_FCOE_CONN_AG_CTX_CF0_SHIFT     2
#define E4_YSTORM_FCOE_CONN_AG_CTX_CF1_MASK      0x3 /* cf1 */
#define E4_YSTORM_FCOE_CONN_AG_CTX_CF1_SHIFT     4
#define E4_YSTORM_FCOE_CONN_AG_CTX_CF2_MASK      0x3 /* cf2 */
#define E4_YSTORM_FCOE_CONN_AG_CTX_CF2_SHIFT     6
	u8 flags1;
#define E4_YSTORM_FCOE_CONN_AG_CTX_CF0EN_MASK    0x1 /* cf0en */
#define E4_YSTORM_FCOE_CONN_AG_CTX_CF0EN_SHIFT   0
#define E4_YSTORM_FCOE_CONN_AG_CTX_CF1EN_MASK    0x1 /* cf1en */
#define E4_YSTORM_FCOE_CONN_AG_CTX_CF1EN_SHIFT   1
#define E4_YSTORM_FCOE_CONN_AG_CTX_CF2EN_MASK    0x1 /* cf2en */
#define E4_YSTORM_FCOE_CONN_AG_CTX_CF2EN_SHIFT   2
#define E4_YSTORM_FCOE_CONN_AG_CTX_RULE0EN_MASK  0x1 /* rule0en */
#define E4_YSTORM_FCOE_CONN_AG_CTX_RULE0EN_SHIFT 3
#define E4_YSTORM_FCOE_CONN_AG_CTX_RULE1EN_MASK  0x1 /* rule1en */
#define E4_YSTORM_FCOE_CONN_AG_CTX_RULE1EN_SHIFT 4
#define E4_YSTORM_FCOE_CONN_AG_CTX_RULE2EN_MASK  0x1 /* rule2en */
#define E4_YSTORM_FCOE_CONN_AG_CTX_RULE2EN_SHIFT 5
#define E4_YSTORM_FCOE_CONN_AG_CTX_RULE3EN_MASK  0x1 /* rule3en */
#define E4_YSTORM_FCOE_CONN_AG_CTX_RULE3EN_SHIFT 6
#define E4_YSTORM_FCOE_CONN_AG_CTX_RULE4EN_MASK  0x1 /* rule4en */
#define E4_YSTORM_FCOE_CONN_AG_CTX_RULE4EN_SHIFT 7
	u8 byte2 /* byte2 */;
	u8 byte3 /* byte3 */;
	__le16 word0 /* word0 */;
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
	__le16 word1 /* word1 */;
	__le16 word2 /* word2 */;
	__le16 word3 /* word3 */;
	__le16 word4 /* word4 */;
	__le32 reg2 /* reg2 */;
	__le32 reg3 /* reg3 */;
};






struct e5_ystorm_fcoe_conn_ag_ctx
{
	u8 byte0 /* cdu_validation */;
	u8 byte1 /* state_and_core_id */;
	u8 flags0;
#define E5_YSTORM_FCOE_CONN_AG_CTX_BIT0_MASK     0x1 /* exist_in_qm0 */
#define E5_YSTORM_FCOE_CONN_AG_CTX_BIT0_SHIFT    0
#define E5_YSTORM_FCOE_CONN_AG_CTX_BIT1_MASK     0x1 /* exist_in_qm1 */
#define E5_YSTORM_FCOE_CONN_AG_CTX_BIT1_SHIFT    1
#define E5_YSTORM_FCOE_CONN_AG_CTX_CF0_MASK      0x3 /* cf0 */
#define E5_YSTORM_FCOE_CONN_AG_CTX_CF0_SHIFT     2
#define E5_YSTORM_FCOE_CONN_AG_CTX_CF1_MASK      0x3 /* cf1 */
#define E5_YSTORM_FCOE_CONN_AG_CTX_CF1_SHIFT     4
#define E5_YSTORM_FCOE_CONN_AG_CTX_CF2_MASK      0x3 /* cf2 */
#define E5_YSTORM_FCOE_CONN_AG_CTX_CF2_SHIFT     6
	u8 flags1;
#define E5_YSTORM_FCOE_CONN_AG_CTX_CF0EN_MASK    0x1 /* cf0en */
#define E5_YSTORM_FCOE_CONN_AG_CTX_CF0EN_SHIFT   0
#define E5_YSTORM_FCOE_CONN_AG_CTX_CF1EN_MASK    0x1 /* cf1en */
#define E5_YSTORM_FCOE_CONN_AG_CTX_CF1EN_SHIFT   1
#define E5_YSTORM_FCOE_CONN_AG_CTX_CF2EN_MASK    0x1 /* cf2en */
#define E5_YSTORM_FCOE_CONN_AG_CTX_CF2EN_SHIFT   2
#define E5_YSTORM_FCOE_CONN_AG_CTX_RULE0EN_MASK  0x1 /* rule0en */
#define E5_YSTORM_FCOE_CONN_AG_CTX_RULE0EN_SHIFT 3
#define E5_YSTORM_FCOE_CONN_AG_CTX_RULE1EN_MASK  0x1 /* rule1en */
#define E5_YSTORM_FCOE_CONN_AG_CTX_RULE1EN_SHIFT 4
#define E5_YSTORM_FCOE_CONN_AG_CTX_RULE2EN_MASK  0x1 /* rule2en */
#define E5_YSTORM_FCOE_CONN_AG_CTX_RULE2EN_SHIFT 5
#define E5_YSTORM_FCOE_CONN_AG_CTX_RULE3EN_MASK  0x1 /* rule3en */
#define E5_YSTORM_FCOE_CONN_AG_CTX_RULE3EN_SHIFT 6
#define E5_YSTORM_FCOE_CONN_AG_CTX_RULE4EN_MASK  0x1 /* rule4en */
#define E5_YSTORM_FCOE_CONN_AG_CTX_RULE4EN_SHIFT 7
	u8 byte2 /* byte2 */;
	u8 byte3 /* byte3 */;
	__le16 word0 /* word0 */;
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
	__le16 word1 /* word1 */;
	__le16 word2 /* word2 */;
	__le16 word3 /* word3 */;
	__le16 word4 /* word4 */;
	__le32 reg2 /* reg2 */;
	__le32 reg3 /* reg3 */;
};

#endif /* __ECORE_HSI_FCOE__ */
