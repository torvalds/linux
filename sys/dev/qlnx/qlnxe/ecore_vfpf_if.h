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


#ifndef __ECORE_VF_PF_IF_H__
#define __ECORE_VF_PF_IF_H__

#define T_ETH_INDIRECTION_TABLE_SIZE 128 /* @@@ TBD MichalK this should be HSI? */
#define T_ETH_RSS_KEY_SIZE 10 /* @@@ TBD this should be HSI? */
#ifndef LINUX_REMOVE
#ifndef ETH_ALEN
#define ETH_ALEN 6 /* @@@ TBD MichalK - should this be defined here?*/
#endif
#endif

/***********************************************
 *
 * Common definitions for all HVs
 *
 **/
struct vf_pf_resc_request {
	u8 num_rxqs;
	u8 num_txqs;
	u8 num_sbs;
	u8 num_mac_filters;
	u8 num_vlan_filters;
	u8 num_mc_filters; /* No limit  so superfluous */
	u8 num_cids;
	u8 padding;
};

struct hw_sb_info {
	u16 hw_sb_id;    /* aka absolute igu id, used to ack the sb */
	u8 sb_qid;      /* used to update DHC for sb */
	u8 padding[5];
};

/***********************************************
 *
 * HW VF-PF channel definitions
 *
 * A.K.A VF-PF mailbox
 *
 **/
#define TLV_BUFFER_SIZE 		1024

/* vf pf channel tlvs */
/* general tlv header (used for both vf->pf request and pf->vf response) */
struct channel_tlv {
	u16 type;
	u16 length;
};

/* header of first vf->pf tlv carries the offset used to calculate reponse
 * buffer address
 */
struct vfpf_first_tlv {
	struct channel_tlv tl;
	u32 padding;
	u64 reply_address;
};

/* header of pf->vf tlvs, carries the status of handling the request */
struct pfvf_tlv {
	struct channel_tlv tl;
	u8 status;
	u8 padding[3];
};

/* response tlv used for most tlvs */
struct pfvf_def_resp_tlv {
	struct pfvf_tlv hdr;
};

/* used to terminate and pad a tlv list */
struct channel_list_end_tlv {
	struct channel_tlv tl;
	u8 padding[4];
};

/* Acquire */
struct vfpf_acquire_tlv {
	struct vfpf_first_tlv first_tlv;

	struct vf_pf_vfdev_info {
#ifndef LINUX_REMOVE
	/* First bit was used on 8.7.x and 8.8.x versions, which had different
	 * FWs used but with the same faspath HSI. As this was prior to the
	 * fastpath versioning, wanted to have ability to override fw matching
	 * and allow them to interact.
	 */
#endif
#define VFPF_ACQUIRE_CAP_PRE_FP_HSI	(1 << 0) /* VF pre-FP hsi version */
#define VFPF_ACQUIRE_CAP_100G		(1 << 1) /* VF can support 100g */

	/* A requirement for supporting multi-Tx queues on a single queue-zone,
	 * VF would pass qids as additional information whenever passing queue
	 * references.
	 * TODO - due to the CID limitations in Bar0, VFs currently don't pass
	 * this, and use the legacy CID scheme.
	 */
#define VFPF_ACQUIRE_CAP_QUEUE_QIDS	(1 << 2)

	/* The VF is using the physical bar. While this is mostly internal
	 * to the VF, might affect the number of CIDs supported assuming
	 * QUEUE_QIDS is set.
	 */
#define VFPF_ACQUIRE_CAP_PHYSICAL_BAR	(1 << 3)
		u64 capabilities;
		u8 fw_major;
		u8 fw_minor;
		u8 fw_revision;
		u8 fw_engineering;
		u32 driver_version;
		u16 opaque_fid; /* ME register value */
		u8 os_type; /* VFPF_ACQUIRE_OS_* value */
		u8 eth_fp_hsi_major;
		u8 eth_fp_hsi_minor;
		u8 padding[3];
	} vfdev_info;

	struct vf_pf_resc_request resc_request;

	u64 bulletin_addr;
	u32 bulletin_size;
	u32 padding;
};

/* receive side scaling tlv */
struct vfpf_vport_update_rss_tlv {
	struct channel_tlv	tl;

	u8 update_rss_flags;
	#define VFPF_UPDATE_RSS_CONFIG_FLAG	  (1 << 0)
	#define VFPF_UPDATE_RSS_CAPS_FLAG	  (1 << 1)
	#define VFPF_UPDATE_RSS_IND_TABLE_FLAG	  (1 << 2)
	#define VFPF_UPDATE_RSS_KEY_FLAG	  (1 << 3)

	u8 rss_enable;
	u8 rss_caps;
	u8 rss_table_size_log; /* The table size is 2 ^ rss_table_size_log */
	u16 rss_ind_table[T_ETH_INDIRECTION_TABLE_SIZE];
	u32 rss_key[T_ETH_RSS_KEY_SIZE];
};

struct pfvf_storm_stats {
	u32 address;
	u32 len;
};

struct pfvf_stats_info {
	struct pfvf_storm_stats mstats;
	struct pfvf_storm_stats pstats;
	struct pfvf_storm_stats tstats;
	struct pfvf_storm_stats ustats;
};

/* acquire response tlv - carries the allocated resources */
struct pfvf_acquire_resp_tlv {
	struct pfvf_tlv hdr;

	struct pf_vf_pfdev_info {
		u32 chip_num;
		u32 mfw_ver;

		u16 fw_major;
		u16 fw_minor;
		u16 fw_rev;
		u16 fw_eng;

		u64 capabilities;
#define PFVF_ACQUIRE_CAP_DEFAULT_UNTAGGED	(1 << 0)
#define PFVF_ACQUIRE_CAP_100G			(1 << 1) /* If set, 100g PF */
/* There are old PF versions where the PF might mistakenly override the sanity
 * mechanism [version-based] and allow a VF that can't be supported to pass
 * the acquisition phase.
 * To overcome this, PFs now indicate that they're past that point and the new
 * VFs would fail probe on the older PFs that fail to do so.
 */
#ifndef LINUX_REMOVE
/* Said bug was in quest/serpens; Can't be certain no official release included
 * the bug since the fix arrived very late in the programs.
 */
#endif
#define PFVF_ACQUIRE_CAP_POST_FW_OVERRIDE	(1 << 2)

	/* PF expects queues to be received with additional qids */
#define PFVF_ACQUIRE_CAP_QUEUE_QIDS		(1 << 3)

		u16 db_size;
		u8  indices_per_sb;
		u8 os_type;

		/* These should match the PF's ecore_dev values */
		u16 chip_rev;
		u8 dev_type;

		/* Doorbell bar size configured in HW: log(size) or 0 */
		u8 bar_size;

		struct pfvf_stats_info stats_info;

		u8 port_mac[ETH_ALEN];

		/* It's possible PF had to configure an older fastpath HSI
		 * [in case VF is newer than PF]. This is communicated back
		 * to the VF. It can also be used in case of error due to
		 * non-matching versions to shed light in VF about failure.
		 */
		u8 major_fp_hsi;
		u8 minor_fp_hsi;
	} pfdev_info;

	struct pf_vf_resc {
		/* in case of status NO_RESOURCE in message hdr, pf will fill
		 * this struct with suggested amount of resources for next
		 * acquire request
		 */
		#define PFVF_MAX_QUEUES_PER_VF         16
		#define PFVF_MAX_SBS_PER_VF            16
		struct hw_sb_info hw_sbs[PFVF_MAX_SBS_PER_VF];
		u8      hw_qid[PFVF_MAX_QUEUES_PER_VF];
		u8      cid[PFVF_MAX_QUEUES_PER_VF];

		u8      num_rxqs;
		u8      num_txqs;
		u8      num_sbs;
		u8      num_mac_filters;
		u8      num_vlan_filters;
		u8      num_mc_filters;
		u8	num_cids;
		u8      padding;
	} resc;

	u32 bulletin_size;
	u32 padding;
};

struct pfvf_start_queue_resp_tlv {
	struct pfvf_tlv hdr;
	u32 offset; /* offset to consumer/producer of queue */
	u8 padding[4];
};

/* Extended queue information - additional index for reference inside qzone.
 * If commmunicated between VF/PF, each TLV relating to queues should be
 * extended by one such [or have a future base TLV that already contains info].
 */
struct vfpf_qid_tlv {
	struct channel_tlv	tl;
	u8			qid;
	u8			padding[3];
};

/* Setup Queue */
struct vfpf_start_rxq_tlv {
	struct vfpf_first_tlv	first_tlv;

	/* physical addresses */
	u64		rxq_addr;
	u64		deprecated_sge_addr;
	u64		cqe_pbl_addr;

	u16			cqe_pbl_size;
	u16			hw_sb;
	u16			rx_qid;
	u16			hc_rate; /* desired interrupts per sec. */

	u16			bd_max_bytes;
	u16			stat_id;
	u8			sb_index;
	u8			padding[3];

};

struct vfpf_start_txq_tlv {
	struct vfpf_first_tlv	first_tlv;

	/* physical addresses */
	u64		pbl_addr;
	u16			pbl_size;
	u16			stat_id;
	u16			tx_qid;
	u16			hw_sb;

	u32			flags; /* VFPF_QUEUE_FLG_X flags */
	u16			hc_rate; /* desired interrupts per sec. */
	u8			sb_index;
	u8			padding[3];
};

/* Stop RX Queue */
struct vfpf_stop_rxqs_tlv {
	struct vfpf_first_tlv	first_tlv;

	u16			rx_qid;

	/* While the API supports multiple Rx-queues on a single TLV
	 * message, in practice older VFs always used it as one [ecore].
	 * And there are PFs [starting with the CHANNEL_TLV_QID] which
	 * would start assuming this is always a '1'. So in practice this
	 * field should be considered deprecated and *Always* set to '1'.
	 */
	u8			num_rxqs;

	u8			cqe_completion;
	u8			padding[4];
};

/* Stop TX Queues */
struct vfpf_stop_txqs_tlv {
	struct vfpf_first_tlv	first_tlv;

	u16			tx_qid;

	/* While the API supports multiple Tx-queues on a single TLV
	 * message, in practice older VFs always used it as one [ecore].
	 * And there are PFs [starting with the CHANNEL_TLV_QID] which
	 * would start assuming this is always a '1'. So in practice this
	 * field should be considered deprecated and *Always* set to '1'.
	 */
	u8			num_txqs;
	u8			padding[5];
};

struct vfpf_update_rxq_tlv {
	struct vfpf_first_tlv	first_tlv;

	u64		deprecated_sge_addr[PFVF_MAX_QUEUES_PER_VF];

	u16			rx_qid;
	u8			num_rxqs;
	u8			flags;
	#define VFPF_RXQ_UPD_INIT_SGE_DEPRECATE_FLAG	(1 << 0)
	#define VFPF_RXQ_UPD_COMPLETE_CQE_FLAG		(1 << 1)
	#define VFPF_RXQ_UPD_COMPLETE_EVENT_FLAG	(1 << 2)

	u8			padding[4];
};

/* Set Queue Filters */
struct vfpf_q_mac_vlan_filter {
	u32 flags;
	#define VFPF_Q_FILTER_DEST_MAC_VALID    0x01
	#define VFPF_Q_FILTER_VLAN_TAG_VALID    0x02
	#define VFPF_Q_FILTER_SET_MAC   	0x100   /* set/clear */

	u8  mac[ETH_ALEN];
	u16 vlan_tag;

	u8	padding[4];
};

/* Start a vport */
struct vfpf_vport_start_tlv {
	struct vfpf_first_tlv	first_tlv;

	u64		sb_addr[PFVF_MAX_SBS_PER_VF];

	u32			tpa_mode;
	u16			dep1;
	u16			mtu;

	u8			vport_id;
	u8			inner_vlan_removal;

	u8			only_untagged;
	u8			max_buffers_per_cqe;

	u8			zero_placement_offset;
	u8			padding[3];
};

/* Extended tlvs - need to add rss, mcast, accept mode tlvs */
struct vfpf_vport_update_activate_tlv {
	struct channel_tlv	tl;
	u8			update_rx;
	u8			update_tx;
	u8			active_rx;
	u8			active_tx;
};

struct vfpf_vport_update_tx_switch_tlv {
	struct channel_tlv	tl;
	u8			tx_switching;
	u8			padding[3];
};

struct vfpf_vport_update_vlan_strip_tlv {
	struct channel_tlv	tl;
	u8			remove_vlan;
	u8			padding[3];
};

struct vfpf_vport_update_mcast_bin_tlv {
	struct channel_tlv	tl;
	u8			padding[4];

	/* This was a mistake; There are only 256 approx bins,
	 * and in HSI they're divided into 32-bit values.
	 * As old VFs used to set-bit to the values on its side,
	 * the upper half of the array is never expected to contain any data.
	 */
	u64		bins[4];
	u64		obsolete_bins[4];
};

struct vfpf_vport_update_accept_param_tlv {
	struct channel_tlv tl;
	u8	update_rx_mode;
	u8	update_tx_mode;
	u8	rx_accept_filter;
	u8	tx_accept_filter;
};

struct vfpf_vport_update_accept_any_vlan_tlv {
	struct channel_tlv tl;
	u8 update_accept_any_vlan_flg;
	u8 accept_any_vlan;

	u8 padding[2];
};

struct vfpf_vport_update_sge_tpa_tlv {
	struct channel_tlv	tl;

	u16			sge_tpa_flags;
	#define VFPF_TPA_IPV4_EN_FLAG	     (1 << 0)
	#define VFPF_TPA_IPV6_EN_FLAG        (1 << 1)
	#define VFPF_TPA_PKT_SPLIT_FLAG      (1 << 2)
	#define VFPF_TPA_HDR_DATA_SPLIT_FLAG (1 << 3)
	#define VFPF_TPA_GRO_CONSIST_FLAG    (1 << 4)

	u8			update_sge_tpa_flags;
	#define VFPF_UPDATE_SGE_DEPRECATED_FLAG	   (1 << 0)
	#define VFPF_UPDATE_TPA_EN_FLAG    (1 << 1)
	#define VFPF_UPDATE_TPA_PARAM_FLAG (1 << 2)

	u8			max_buffers_per_cqe;

	u16			deprecated_sge_buff_size;
	u16			tpa_max_size;
	u16			tpa_min_size_to_start;
	u16			tpa_min_size_to_cont;

	u8			tpa_max_aggs_num;
	u8			padding[7];

};

/* Primary tlv as a header for various extended tlvs for
 * various functionalities in vport update ramrod.
 */
struct vfpf_vport_update_tlv {
	struct vfpf_first_tlv first_tlv;
};

struct vfpf_ucast_filter_tlv {
	struct vfpf_first_tlv	first_tlv;

	u8			opcode;
	u8			type;

	u8			mac[ETH_ALEN];

	u16			vlan;
	u16			padding[3];
};

/* tunnel update param tlv */
struct vfpf_update_tunn_param_tlv {
	struct vfpf_first_tlv   first_tlv;

	u8			tun_mode_update_mask;
	u8			tunn_mode;
	u8			update_tun_cls;
	u8			vxlan_clss;
	u8			l2gre_clss;
	u8			ipgre_clss;
	u8			l2geneve_clss;
	u8			ipgeneve_clss;
	u8			update_geneve_port;
	u8			update_vxlan_port;
	u16			geneve_port;
	u16			vxlan_port;
	u8			padding[2];
};

struct pfvf_update_tunn_param_tlv {
	struct pfvf_tlv hdr;

	u16			tunn_feature_mask;
	u8			vxlan_mode;
	u8			l2geneve_mode;
	u8			ipgeneve_mode;
	u8			l2gre_mode;
	u8			ipgre_mode;
	u8			vxlan_clss;
	u8			l2gre_clss;
	u8			ipgre_clss;
	u8			l2geneve_clss;
	u8			ipgeneve_clss;
	u16			vxlan_udp_port;
	u16			geneve_udp_port;
};

struct tlv_buffer_size {
	u8 tlv_buffer[TLV_BUFFER_SIZE];
};

struct vfpf_update_coalesce {
	struct vfpf_first_tlv first_tlv;
	u16 rx_coal;
	u16 tx_coal;
	u16 qid;
	u8 padding[2];
};

struct vfpf_read_coal_req_tlv {
	struct vfpf_first_tlv first_tlv;
	u16 qid;
	u8 is_rx;
	u8 padding[5];
};

struct pfvf_read_coal_resp_tlv {
	struct pfvf_tlv hdr;
	u16 coal;
	u8 padding[6];
};

union vfpf_tlvs {
	struct vfpf_first_tlv			first_tlv;
	struct vfpf_acquire_tlv			acquire;
	struct vfpf_start_rxq_tlv		start_rxq;
	struct vfpf_start_txq_tlv		start_txq;
	struct vfpf_stop_rxqs_tlv		stop_rxqs;
	struct vfpf_stop_txqs_tlv		stop_txqs;
	struct vfpf_update_rxq_tlv		update_rxq;
	struct vfpf_vport_start_tlv		start_vport;
	struct vfpf_vport_update_tlv		vport_update;
	struct vfpf_ucast_filter_tlv		ucast_filter;
	struct vfpf_update_tunn_param_tlv	tunn_param_update;
	struct vfpf_update_coalesce		update_coalesce;
	struct vfpf_read_coal_req_tlv		read_coal_req;
	struct tlv_buffer_size			tlv_buf_size;
};

union pfvf_tlvs {
	struct pfvf_def_resp_tlv		default_resp;
	struct pfvf_acquire_resp_tlv		acquire_resp;
	struct tlv_buffer_size			tlv_buf_size;
	struct pfvf_start_queue_resp_tlv	queue_start;
	struct pfvf_update_tunn_param_tlv	tunn_param_resp;
	struct pfvf_read_coal_resp_tlv		read_coal_resp;
};

/* This is a structure which is allocated in the VF, which the PF may update
 * when it deems it necessary to do so. The bulletin board is sampled
 * periodically by the VF. A copy per VF is maintained in the PF (to prevent
 * loss of data upon multiple updates (or the need for read modify write)).
 */
enum ecore_bulletin_bit {
	/* Alert the VF that a forced MAC was set by the PF */
	MAC_ADDR_FORCED = 0,

	/* The VF should not access the vfpf channel */
	VFPF_CHANNEL_INVALID = 1,

	/* Alert the VF that a forced VLAN was set by the PF */
	VLAN_ADDR_FORCED = 2,

	/* Indicate that `default_only_untagged' contains actual data */
	VFPF_BULLETIN_UNTAGGED_DEFAULT = 3,
	VFPF_BULLETIN_UNTAGGED_DEFAULT_FORCED = 4,

	/* Alert the VF that suggested mac was sent by the PF.
	 * MAC_ADDR will be disabled in case MAC_ADDR_FORCED is set
	 */
	VFPF_BULLETIN_MAC_ADDR = 5
};

struct ecore_bulletin_content {
	/* crc of structure to ensure is not in mid-update */
	u32 crc;

	u32 version;

	/* bitmap indicating which fields hold valid values */
	u64 valid_bitmap;

	/* used for MAC_ADDR or MAC_ADDR_FORCED */
	u8 mac[ETH_ALEN];

	/* If valid, 1 => only untagged Rx if no vlan is configured */
	u8 default_only_untagged;
	u8 padding;

	/* The following is a 'copy' of ecore_mcp_link_state,
	 * ecore_mcp_link_params and ecore_mcp_link_capabilities. Since it's
	 * possible the structs will increase further along the road we cannot
	 * have it here; Instead we need to have all of its fields.
	 */
	u8 req_autoneg;
	u8 req_autoneg_pause;
	u8 req_forced_rx;
	u8 req_forced_tx;
	u8 padding2[4];

	u32 req_adv_speed;
	u32 req_forced_speed;
	u32 req_loopback;
	u32 padding3;

	u8 link_up;
	u8 full_duplex;
	u8 autoneg;
	u8 autoneg_complete;
	u8 parallel_detection;
	u8 pfc_enabled;
	u8 partner_tx_flow_ctrl_en;
	u8 partner_rx_flow_ctrl_en;

	u8 partner_adv_pause;
	u8 sfp_tx_fault;
	u16 vxlan_udp_port;
	u16 geneve_udp_port;
	u8 padding4[2];

	u32 speed;
	u32 partner_adv_speed;

	u32 capability_speed;

	/* Forced vlan */
	u16 pvid;
	u16 padding5;
};

struct ecore_bulletin {
	dma_addr_t phys;
	struct ecore_bulletin_content *p_virt;
	u32 size;
};

enum {
/*!!!!! Make sure to update STRINGS structure accordingly !!!!!*/

	CHANNEL_TLV_NONE, /* ends tlv sequence */
	CHANNEL_TLV_ACQUIRE,
	CHANNEL_TLV_VPORT_START,
	CHANNEL_TLV_VPORT_UPDATE,
	CHANNEL_TLV_VPORT_TEARDOWN,
	CHANNEL_TLV_START_RXQ,
	CHANNEL_TLV_START_TXQ,
	CHANNEL_TLV_STOP_RXQS,
	CHANNEL_TLV_STOP_TXQS,
	CHANNEL_TLV_UPDATE_RXQ,
	CHANNEL_TLV_INT_CLEANUP,
	CHANNEL_TLV_CLOSE,
	CHANNEL_TLV_RELEASE,
	CHANNEL_TLV_LIST_END,
	CHANNEL_TLV_UCAST_FILTER,
	CHANNEL_TLV_VPORT_UPDATE_ACTIVATE,
	CHANNEL_TLV_VPORT_UPDATE_TX_SWITCH,
	CHANNEL_TLV_VPORT_UPDATE_VLAN_STRIP,
	CHANNEL_TLV_VPORT_UPDATE_MCAST,
	CHANNEL_TLV_VPORT_UPDATE_ACCEPT_PARAM,
	CHANNEL_TLV_VPORT_UPDATE_RSS,
	CHANNEL_TLV_VPORT_UPDATE_ACCEPT_ANY_VLAN,
	CHANNEL_TLV_VPORT_UPDATE_SGE_TPA,
	CHANNEL_TLV_UPDATE_TUNN_PARAM,
	CHANNEL_TLV_COALESCE_UPDATE,
	CHANNEL_TLV_QID,
	CHANNEL_TLV_COALESCE_READ,
	CHANNEL_TLV_MAX,

	/* Required for iterating over vport-update tlvs.
	 * Will break in case non-sequential vport-update tlvs.
	 */
	CHANNEL_TLV_VPORT_UPDATE_MAX = CHANNEL_TLV_VPORT_UPDATE_SGE_TPA + 1,

/*!!!!! Make sure to update STRINGS structure accordingly !!!!!*/
};
extern const char *ecore_channel_tlvs_string[];

#endif /* __ECORE_VF_PF_IF_H__ */
