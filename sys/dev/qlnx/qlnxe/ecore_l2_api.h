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

#ifndef __ECORE_L2_API_H__
#define __ECORE_L2_API_H__

#include "ecore_status.h"
#include "ecore_sp_api.h"
#include "ecore_int_api.h"

#ifndef __EXTRACT__LINUX__
enum ecore_rss_caps {
	ECORE_RSS_IPV4		= 0x1,
	ECORE_RSS_IPV6		= 0x2,
	ECORE_RSS_IPV4_TCP	= 0x4,
	ECORE_RSS_IPV6_TCP	= 0x8,
	ECORE_RSS_IPV4_UDP	= 0x10,
	ECORE_RSS_IPV6_UDP	= 0x20,
};

/* Should be the same as ETH_RSS_IND_TABLE_ENTRIES_NUM */
#define ECORE_RSS_IND_TABLE_SIZE 128
#define ECORE_RSS_KEY_SIZE 10 /* size in 32b chunks */

#define ECORE_MAX_PHC_DRIFT_PPB	291666666

enum ecore_ptp_filter_type {
	ECORE_PTP_FILTER_NONE,
	ECORE_PTP_FILTER_ALL,
	ECORE_PTP_FILTER_V1_L4_EVENT,
	ECORE_PTP_FILTER_V1_L4_GEN,
	ECORE_PTP_FILTER_V2_L4_EVENT,
	ECORE_PTP_FILTER_V2_L4_GEN,
	ECORE_PTP_FILTER_V2_L2_EVENT,
	ECORE_PTP_FILTER_V2_L2_GEN,
	ECORE_PTP_FILTER_V2_EVENT,
	ECORE_PTP_FILTER_V2_GEN
};

enum ecore_ptp_hwtstamp_tx_type {
	ECORE_PTP_HWTSTAMP_TX_OFF,
	ECORE_PTP_HWTSTAMP_TX_ON,
};
#endif

#ifndef __EXTRACT__LINUX__
struct ecore_queue_start_common_params {
	/* Should always be relative to entity sending this. */
	u8 vport_id;
	u16 queue_id;

	/* Relative, but relevant only for PFs */
	u8 stats_id;

	struct ecore_sb_info *p_sb;
	u8 sb_idx;

	u8 tc;
};

struct ecore_rxq_start_ret_params {
	void OSAL_IOMEM *p_prod;
	void *p_handle;
};

struct ecore_txq_start_ret_params {
	void OSAL_IOMEM *p_doorbell;
	void *p_handle;
};
#endif

struct ecore_rss_params {
	u8 update_rss_config;
	u8 rss_enable;
	u8 rss_eng_id;
	u8 update_rss_capabilities;
	u8 update_rss_ind_table;
	u8 update_rss_key;
	u8 rss_caps;
	u8 rss_table_size_log; /* The table size is 2 ^ rss_table_size_log */

	/* Indirection table consist of rx queue handles */
	void *rss_ind_table[ECORE_RSS_IND_TABLE_SIZE];
	u32 rss_key[ECORE_RSS_KEY_SIZE];
};

struct ecore_sge_tpa_params {
	u8 max_buffers_per_cqe;

	u8 update_tpa_en_flg;
	u8 tpa_ipv4_en_flg;
	u8 tpa_ipv6_en_flg;
	u8 tpa_ipv4_tunn_en_flg;
	u8 tpa_ipv6_tunn_en_flg;

	u8 update_tpa_param_flg;
	u8 tpa_pkt_split_flg;
	u8 tpa_hdr_data_split_flg;
	u8 tpa_gro_consistent_flg;
	u8 tpa_max_aggs_num;
	u16 tpa_max_size;
	u16 tpa_min_size_to_start;
	u16 tpa_min_size_to_cont;
};

enum ecore_filter_opcode {
	ECORE_FILTER_ADD,
	ECORE_FILTER_REMOVE,
	ECORE_FILTER_MOVE,
	ECORE_FILTER_REPLACE, /* Delete all MACs and add new one instead */
	ECORE_FILTER_FLUSH, /* Removes all filters */
};

enum ecore_filter_ucast_type {
	ECORE_FILTER_MAC,
	ECORE_FILTER_VLAN,
	ECORE_FILTER_MAC_VLAN,
	ECORE_FILTER_INNER_MAC,
	ECORE_FILTER_INNER_VLAN,
	ECORE_FILTER_INNER_PAIR,
	ECORE_FILTER_INNER_MAC_VNI_PAIR,
	ECORE_FILTER_MAC_VNI_PAIR,
	ECORE_FILTER_VNI,
};

struct ecore_filter_ucast {
	enum ecore_filter_opcode opcode;
	enum ecore_filter_ucast_type type;
	u8 is_rx_filter;
	u8 is_tx_filter;
	u8 vport_to_add_to;
	u8 vport_to_remove_from;
	unsigned char mac[ETH_ALEN];
	u8 assert_on_error;
	u16 vlan;
	u32 vni;
};

struct ecore_filter_mcast {
	/* MOVE is not supported for multicast */
	enum ecore_filter_opcode opcode;
	u8 vport_to_add_to;
	u8 vport_to_remove_from;
	u8	num_mc_addrs;
#define ECORE_MAX_MC_ADDRS	64
	unsigned char mac[ECORE_MAX_MC_ADDRS][ETH_ALEN];
};

struct ecore_filter_accept_flags {
	u8 update_rx_mode_config;
	u8 update_tx_mode_config;
	u8 rx_accept_filter;
	u8 tx_accept_filter;
#define	ECORE_ACCEPT_NONE		0x01
#define ECORE_ACCEPT_UCAST_MATCHED	0x02
#define ECORE_ACCEPT_UCAST_UNMATCHED	0x04
#define ECORE_ACCEPT_MCAST_MATCHED	0x08
#define ECORE_ACCEPT_MCAST_UNMATCHED	0x10
#define ECORE_ACCEPT_BCAST		0x20
};

#ifndef __EXTRACT__LINUX__
enum ecore_filter_config_mode {
	ECORE_FILTER_CONFIG_MODE_DISABLE,
	ECORE_FILTER_CONFIG_MODE_5_TUPLE,
	ECORE_FILTER_CONFIG_MODE_L4_PORT,
	ECORE_FILTER_CONFIG_MODE_IP_DEST,
};
#endif

struct ecore_arfs_config_params {
	bool tcp;
	bool udp;
	bool ipv4;
	bool ipv6;
	enum ecore_filter_config_mode mode;
};

/* Add / remove / move / remove-all unicast MAC-VLAN filters.
 * FW will assert in the following cases, so driver should take care...:
 * 1. Adding a filter to a full table.
 * 2. Adding a filter which already exists on that vport.
 * 3. Removing a filter which doesn't exist.
 */

enum _ecore_status_t
ecore_filter_ucast_cmd(struct ecore_dev *p_dev,
		       struct ecore_filter_ucast *p_filter_cmd,
		       enum spq_mode comp_mode,
		       struct ecore_spq_comp_cb *p_comp_data);

/* Add / remove / move multicast MAC filters. */
enum _ecore_status_t
ecore_filter_mcast_cmd(struct ecore_dev *p_dev,
		       struct ecore_filter_mcast *p_filter_cmd,
		       enum spq_mode comp_mode,
		       struct ecore_spq_comp_cb *p_comp_data);

/* Set "accept" filters */
enum _ecore_status_t
ecore_filter_accept_cmd(
	struct ecore_dev		 *p_dev,
	u8				 vport,
	struct ecore_filter_accept_flags accept_flags,
	u8				 update_accept_any_vlan,
	u8				 accept_any_vlan,
	enum spq_mode			 comp_mode,
	struct ecore_spq_comp_cb	 *p_comp_data);

/**
 * @brief ecore_eth_rx_queue_start - RX Queue Start Ramrod
 *
 * This ramrod initializes an RX Queue for a VPort. An Assert is generated if
 * the VPort ID is not currently initialized.
 *
 * @param p_hwfn
 * @param opaque_fid
 * @p_params			Inputs; Relative for PF [SB being an exception]
 * @param bd_max_bytes 		Maximum bytes that can be placed on a BD
 * @param bd_chain_phys_addr	Physical address of BDs for receive.
 * @param cqe_pbl_addr		Physical address of the CQE PBL Table.
 * @param cqe_pbl_size 		Size of the CQE PBL Table
 * @param p_ret_params		Pointed struct to be filled with outputs.
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t
ecore_eth_rx_queue_start(struct ecore_hwfn *p_hwfn,
			 u16 opaque_fid,
			 struct ecore_queue_start_common_params *p_params,
			 u16 bd_max_bytes,
			 dma_addr_t bd_chain_phys_addr,
			 dma_addr_t cqe_pbl_addr,
			 u16 cqe_pbl_size,
			 struct ecore_rxq_start_ret_params *p_ret_params);

/**
 * @brief ecore_eth_rx_queue_stop - This ramrod closes an Rx queue
 *
 * @param p_hwfn
 * @param p_rxq			Handler of queue to close
 * @param eq_completion_only	If True completion will be on
 *				EQe, if False completion will be
 *				on EQe if p_hwfn opaque
 *				different from the RXQ opaque
 *				otherwise on CQe.
 * @param cqe_completion	If True completion will be
 *				recieve on CQe.
 * @return enum _ecore_status_t
 */
enum _ecore_status_t
ecore_eth_rx_queue_stop(struct ecore_hwfn *p_hwfn,
			void *p_rxq,
			bool eq_completion_only,
			bool cqe_completion);

/**
 * @brief - TX Queue Start Ramrod
 *
 * This ramrod initializes a TX Queue for a VPort. An Assert is generated if
 * the VPort is not currently initialized.
 *
 * @param p_hwfn
 * @param opaque_fid
 * @p_params
 * @param tc			traffic class to use with this L2 txq
 * @param pbl_addr		address of the pbl array
 * @param pbl_size 		number of entries in pbl
 * @oaram p_ret_params		Pointer to fill the return parameters in.
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t
ecore_eth_tx_queue_start(struct ecore_hwfn *p_hwfn,
			 u16 opaque_fid,
			 struct ecore_queue_start_common_params *p_params,
			 u8 tc,
			 dma_addr_t pbl_addr,
			 u16 pbl_size,
			 struct ecore_txq_start_ret_params *p_ret_params);

/**
 * @brief ecore_eth_tx_queue_stop - closes a Tx queue
 *
 * @param p_hwfn
 * @param p_txq - handle to Tx queue needed to be closed
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t ecore_eth_tx_queue_stop(struct ecore_hwfn *p_hwfn,
					     void *p_txq);

enum ecore_tpa_mode	{
	ECORE_TPA_MODE_NONE,
	ECORE_TPA_MODE_RSC,
	ECORE_TPA_MODE_GRO,
	ECORE_TPA_MODE_MAX
};

struct ecore_sp_vport_start_params {
	enum ecore_tpa_mode tpa_mode;
	bool remove_inner_vlan;	/* Inner VLAN removal is enabled */
	bool tx_switching;	/* Vport supports tx-switching */
	bool handle_ptp_pkts;	/* Handle PTP packets */
	bool only_untagged;	/* Untagged pkt control */
	bool drop_ttl0;		/* Drop packets with TTL = 0 */
	u8 max_buffers_per_cqe;
	u32 concrete_fid;
	u16 opaque_fid;
	u8 vport_id;		/* VPORT ID */
	u16 mtu;		/* VPORT MTU */
	bool zero_placement_offset;
	bool check_mac;
	bool check_ethtype;

	/* Strict behavior on transmission errors */
	bool b_err_illegal_vlan_mode;
	bool b_err_illegal_inband_mode;
	bool b_err_vlan_insert_with_inband;
	bool b_err_small_pkt;
	bool b_err_big_pkt;
	bool b_err_anti_spoof;
	bool b_err_ctrl_frame;
};

/**
 * @brief ecore_sp_vport_start -
 *
 * This ramrod initializes a VPort. An Assert if generated if the Function ID
 * of the VPort is not enabled.
 *
 * @param p_hwfn
 * @param p_params		VPORT start params
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t
ecore_sp_vport_start(struct ecore_hwfn *p_hwfn,
		     struct ecore_sp_vport_start_params *p_params);

struct ecore_sp_vport_update_params {
	u16			opaque_fid;
	u8			vport_id;
	u8			update_vport_active_rx_flg;
	u8			vport_active_rx_flg;
	u8			update_vport_active_tx_flg;
	u8			vport_active_tx_flg;
	u8			update_inner_vlan_removal_flg;
	u8			inner_vlan_removal_flg;
	u8			silent_vlan_removal_flg;
	u8			update_default_vlan_enable_flg;
	u8			default_vlan_enable_flg;
	u8			update_default_vlan_flg;
	u16			default_vlan;
	u8			update_tx_switching_flg;
	u8			tx_switching_flg;
	u8			update_approx_mcast_flg;
	u8			update_anti_spoofing_en_flg;
	u8			anti_spoofing_en;
	u8			update_accept_any_vlan_flg;
	u8			accept_any_vlan;
	u32			bins[8];
	struct ecore_rss_params	*rss_params;
	struct ecore_filter_accept_flags accept_flags;
	struct ecore_sge_tpa_params *sge_tpa_params;
};

/**
 * @brief ecore_sp_vport_update -
 *
 * This ramrod updates the parameters of the VPort. Every field can be updated
 * independently, according to flags.
 *
 * This ramrod is also used to set the VPort state to active after creation.
 * An Assert is generated if the VPort does not contain an RX queue.
 *
 * @param p_hwfn
 * @param p_params
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t
ecore_sp_vport_update(struct ecore_hwfn *p_hwfn,
		      struct ecore_sp_vport_update_params *p_params,
		      enum spq_mode comp_mode,
		      struct ecore_spq_comp_cb *p_comp_data);
/**
 * @brief ecore_sp_vport_stop -
 *
 * This ramrod closes a VPort after all its RX and TX queues are terminated.
 * An Assert is generated if any queues are left open.
 *
 * @param p_hwfn
 * @param opaque_fid
 * @param vport_id VPort ID
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t ecore_sp_vport_stop(struct ecore_hwfn *p_hwfn,
					 u16 opaque_fid,
					 u8 vport_id);

enum _ecore_status_t
ecore_sp_eth_filter_ucast(struct ecore_hwfn *p_hwfn,
			  u16 opaque_fid,
			  struct ecore_filter_ucast *p_filter_cmd,
			  enum spq_mode comp_mode,
			  struct ecore_spq_comp_cb *p_comp_data);

/**
 * @brief ecore_sp_rx_eth_queues_update -
 *
 * This ramrod updates an RX queue. It is used for setting the active state
 * of the queue and updating the TPA and SGE parameters.
 *
 * @note Final phase API.
 *
 * @param p_hwfn
 * @param pp_rxq_handlers	An array of queue handlers to be updated.
 * @param num_rxqs              number of queues to update.
 * @param complete_cqe_flg	Post completion to the CQE Ring if set
 * @param complete_event_flg	Post completion to the Event Ring if set
 * @param comp_mode
 * @param p_comp_data
 *
 * @return enum _ecore_status_t
 */

enum _ecore_status_t
ecore_sp_eth_rx_queues_update(struct ecore_hwfn *p_hwfn,
			      void **pp_rxq_handlers,
			      u8 num_rxqs,
			      u8 complete_cqe_flg,
			      u8 complete_event_flg,
			      enum spq_mode comp_mode,
			      struct ecore_spq_comp_cb *p_comp_data);

/**
 * @brief ecore_sp_eth_rx_queues_set_default -
 *
 * This ramrod sets RSS RX queue as default one.
 *
 * @note Final phase API.
 *
 * @param p_hwfn
 * @param p_rxq_handlers	queue handlers to be updated.
 * @param comp_mode
 * @param p_comp_data
 *
 * @return enum _ecore_status_t
 */

enum _ecore_status_t
ecore_sp_eth_rx_queues_set_default(struct ecore_hwfn *p_hwfn,
				   void *p_rxq_handler,
				   enum spq_mode comp_mode,
				   struct ecore_spq_comp_cb *p_comp_data);

void __ecore_get_vport_stats(struct ecore_hwfn *p_hwfn,
			     struct ecore_ptt *p_ptt,
			     struct ecore_eth_stats *stats,
			     u16 statistics_bin, bool b_get_port_stats);

void ecore_get_vport_stats(struct ecore_dev *p_dev,
			   struct ecore_eth_stats *stats);

void ecore_reset_vport_stats(struct ecore_dev *p_dev);

/**
 *@brief ecore_arfs_mode_configure -
 *
 *Enable or disable rfs mode. It must accept atleast one of tcp or udp true
 *and atleast one of ipv4 or ipv6 true to enable rfs mode.
 *
 *@param p_hwfn
 *@param p_ptt
 *@param p_cfg_params		arfs mode configuration parameters.
 *
 */
void ecore_arfs_mode_configure(struct ecore_hwfn *p_hwfn,
			       struct ecore_ptt *p_ptt,
			       struct ecore_arfs_config_params *p_cfg_params);

#ifndef __EXTRACT__LINUX__
struct ecore_ntuple_filter_params {
	/* Physically mapped address containing header of buffer to be used
	 * as filter.
	 */
	dma_addr_t addr;

	/* Length of header in bytes */
	u16 length;

	/* Relative queue-id to receive classified packet */
#define ECORE_RFS_NTUPLE_QID_RSS ((u16)-1)
	u16 qid;

	/* Identifier can either be according to vport-id or vfid */
	bool b_is_vf;
	u8 vport_id;
	u8 vf_id;

	/* true iff this filter is to be added. Else to be removed */
	bool b_is_add;
};
#endif

/**
 * @brief - ecore_configure_rfs_ntuple_filter
 *
 * This ramrod should be used to add or remove arfs hw filter
 *
 * @params p_hwfn
 * @params p_cb		Used for ECORE_SPQ_MODE_CB,where client would initialize
 *			it with cookie and callback function address, if not
 *			using this mode then client must pass NULL.
 * @params p_params
 */
enum _ecore_status_t
ecore_configure_rfs_ntuple_filter(struct ecore_hwfn *p_hwfn,
				  struct ecore_spq_comp_cb *p_cb,
				  struct ecore_ntuple_filter_params *p_params);
#endif
