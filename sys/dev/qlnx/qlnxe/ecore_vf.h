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

#ifndef __ECORE_VF_H__
#define __ECORE_VF_H__

#include "ecore_status.h"
#include "ecore_vf_api.h"
#include "ecore_l2_api.h"
#include "ecore_vfpf_if.h"

/* Default number of CIDs [total of both Rx and Tx] to be requested
 * by default, and maximum possible number.
 */
#define ECORE_ETH_VF_DEFAULT_NUM_CIDS	(32)
#define ECORE_ETH_VF_MAX_NUM_CIDS	(255)

/* This data is held in the ecore_hwfn structure for VFs only. */
struct ecore_vf_iov {
	union vfpf_tlvs			*vf2pf_request;
	dma_addr_t			vf2pf_request_phys;
	union pfvf_tlvs			*pf2vf_reply;
	dma_addr_t			pf2vf_reply_phys;

	/* Should be taken whenever the mailbox buffers are accessed */
	osal_mutex_t			mutex;
	u8				*offset;

	/* Bulletin Board */
	struct ecore_bulletin		bulletin;
	struct ecore_bulletin_content	bulletin_shadow;

	/* we set aside a copy of the acquire response */
	struct pfvf_acquire_resp_tlv	acquire_resp;

	/* In case PF originates prior to the fp-hsi version comparison,
	 * this has to be propagated as it affects the fastpath.
	 */
	bool b_pre_fp_hsi;

	/* Current day VFs are passing the SBs physical address on vport
	 * start, and as they lack an IGU mapping they need to store the
	 * addresses of previously registered SBs.
	 * Even if we were to change configuration flow, due to backward
	 * compatability [with older PFs] we'd still need to store these.
	 */
	struct ecore_sb_info *sbs_info[PFVF_MAX_SBS_PER_VF];

#ifdef CONFIG_ECORE_SW_CHANNEL
	/* Would be set if the VF is to try communicating with it PF
	 * using a hw channel.
	 */
	bool b_hw_channel;
#endif

	/* Determines whether VF utilizes doorbells via limited register
	 * bar or via the doorbell bar.
	 */
	bool b_doorbell_bar;
};

/**
 * @brief VF - Get coalesce per VF's relative queue.
 *
 * @param p_hwfn
 * @param p_coal - coalesce value in micro second for VF queues.
 * @param p_cid  - queue cid
 *
 **/
enum _ecore_status_t ecore_vf_pf_get_coalesce(struct ecore_hwfn *p_hwfn,
					      u16 *p_coal,
					      struct ecore_queue_cid *p_cid);
/**
 * @brief VF - Set Rx/Tx coalesce per VF's relative queue.
 *             Coalesce value '0' will omit the configuration.
 *
 * @param p_hwfn
 * @param rx_coal - coalesce value in micro second for rx queue
 * @param tx_coal - coalesce value in micro second for tx queue
 * @param p_cid   - queue cid
 *
 **/
enum _ecore_status_t ecore_vf_pf_set_coalesce(struct ecore_hwfn *p_hwfn,
					      u16 rx_coal, u16 tx_coal,
					      struct ecore_queue_cid *p_cid);

#ifdef CONFIG_ECORE_SRIOV
/**
 * @brief hw preparation for VF
 *	sends ACQUIRE message
 *
 * @param p_hwfn
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t ecore_vf_hw_prepare(struct ecore_hwfn *p_hwfn);

/**
 * @brief VF - start the RX Queue by sending a message to the PF
 *
 * @param p_hwfn
 * @param p_cid			- Only relative fields are relevant
 * @param bd_max_bytes 		- maximum number of bytes per bd
 * @param bd_chain_phys_addr 	- physical address of bd chain
 * @param cqe_pbl_addr 		- physical address of pbl
 * @param cqe_pbl_size 		- pbl size
 * @param pp_prod 		- pointer to the producer to be
 *				  used in fasthpath
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t ecore_vf_pf_rxq_start(struct ecore_hwfn *p_hwfn,
					   struct ecore_queue_cid *p_cid,
					   u16 bd_max_bytes,
					   dma_addr_t bd_chain_phys_addr,
					   dma_addr_t cqe_pbl_addr,
					   u16 cqe_pbl_size,
					   void OSAL_IOMEM **pp_prod);

/**
 * @brief VF - start the TX queue by sending a message to the
 *        PF.
 *
 * @param p_hwfn
 * @param p_cid
 * @param bd_chain_phys_addr 	- physical address of tx chain
 * @param pp_doorbell 		- pointer to address to which to
 *      		write the doorbell too..
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t
ecore_vf_pf_txq_start(struct ecore_hwfn *p_hwfn,
		      struct ecore_queue_cid *p_cid,
		      dma_addr_t pbl_addr, u16 pbl_size,
		      void OSAL_IOMEM **pp_doorbell);

/**
 * @brief VF - stop the RX queue by sending a message to the PF
 *
 * @param p_hwfn
 * @param p_cid
 * @param cqe_completion
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t ecore_vf_pf_rxq_stop(struct ecore_hwfn *p_hwfn,
					  struct ecore_queue_cid *p_cid,
					  bool cqe_completion);

/**
 * @brief VF - stop the TX queue by sending a message to the PF
 *
 * @param p_hwfn
 * @param p_cid
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t ecore_vf_pf_txq_stop(struct ecore_hwfn *p_hwfn,
					  struct ecore_queue_cid *p_cid);

/* TODO - fix all the !SRIOV prototypes */

#ifndef LINUX_REMOVE
/**
 * @brief VF - update the RX queue by sending a message to the
 *        PF
 *
 * @param p_hwfn
 * @param pp_cid - list of queue-cids which we want to update
 * @param num_rxqs
 * @param comp_cqe_flg
 * @param comp_event_flg
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t ecore_vf_pf_rxqs_update(struct ecore_hwfn *p_hwfn,
					     struct ecore_queue_cid **pp_cid,
					     u8 num_rxqs,
					     u8 comp_cqe_flg,
					     u8 comp_event_flg);
#endif

/**
 * @brief VF - send a vport update command
 *
 * @param p_hwfn
 * @param params
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t ecore_vf_pf_vport_update(struct ecore_hwfn *p_hwfn,
					      struct ecore_sp_vport_update_params *p_params);

/**
 * @brief VF - send a close message to PF
 *
 * @param p_hwfn
 *
 * @return enum _ecore_status
 */
enum _ecore_status_t ecore_vf_pf_reset(struct ecore_hwfn *p_hwfn);

/**
 * @brief VF - free vf`s memories
 *
 * @param p_hwfn
 *
 * @return enum _ecore_status
 */
enum _ecore_status_t ecore_vf_pf_release(struct ecore_hwfn *p_hwfn);

/**
 * @brief ecore_vf_get_igu_sb_id - Get the IGU SB ID for a given
 *        sb_id. For VFs igu sbs don't have to be contiguous
 *
 * @param p_hwfn
 * @param sb_id
 *
 * @return INLINE u16
 */
u16 ecore_vf_get_igu_sb_id(struct ecore_hwfn *p_hwfn,
			   u16               sb_id);

/**
 * @brief Stores [or removes] a configured sb_info.
 *
 * @param p_hwfn
 * @param sb_id - zero-based SB index [for fastpath]
 * @param sb_info - may be OSAL_NULL [during removal].
 */
void ecore_vf_set_sb_info(struct ecore_hwfn *p_hwfn,
			  u16 sb_id, struct ecore_sb_info *p_sb);

/**
 * @brief ecore_vf_pf_vport_start - perform vport start for VF.
 *
 * @param p_hwfn
 * @param vport_id
 * @param mtu
 * @param inner_vlan_removal
 * @param tpa_mode
 * @param max_buffers_per_cqe,
 * @param only_untagged - default behavior regarding vlan acceptance
 * @param zero_placement_offset - if set, zero padding will be inserted
 *
 * @return enum _ecore_status
 */
enum _ecore_status_t ecore_vf_pf_vport_start(
			struct ecore_hwfn *p_hwfn,
			u8 vport_id,
			u16 mtu,
			u8 inner_vlan_removal,
			enum ecore_tpa_mode tpa_mode,
			u8 max_buffers_per_cqe,
			u8 only_untagged,
			u8 zero_placement_offset);

/**
 * @brief ecore_vf_pf_vport_stop - stop the VF's vport
 *
 * @param p_hwfn
 *
 * @return enum _ecore_status
 */
enum _ecore_status_t ecore_vf_pf_vport_stop(struct ecore_hwfn *p_hwfn);

enum _ecore_status_t ecore_vf_pf_filter_ucast(
			struct ecore_hwfn *p_hwfn,
			struct ecore_filter_ucast *p_param);

void ecore_vf_pf_filter_mcast(struct ecore_hwfn *p_hwfn,
			      struct ecore_filter_mcast *p_filter_cmd);

/**
 * @brief ecore_vf_pf_int_cleanup - clean the SB of the VF
 *
 * @param p_hwfn
 *
 * @return enum _ecore_status
 */
enum _ecore_status_t ecore_vf_pf_int_cleanup(struct ecore_hwfn *p_hwfn);

/**
 * @brief - return the link params in a given bulletin board
 *
 * @param p_params - pointer to a struct to fill with link params
 * @param p_bulletin
 */
void __ecore_vf_get_link_params(struct ecore_mcp_link_params *p_params,
				struct ecore_bulletin_content *p_bulletin);

/**
 * @brief - return the link state in a given bulletin board
 *
 * @param p_link - pointer to a struct to fill with link state
 * @param p_bulletin
 */
void __ecore_vf_get_link_state(struct ecore_mcp_link_state *p_link,
			       struct ecore_bulletin_content *p_bulletin);

/**
 * @brief - return the link capabilities in a given bulletin board
 *
 * @param p_link - pointer to a struct to fill with link capabilities
 * @param p_bulletin
 */
void __ecore_vf_get_link_caps(struct ecore_mcp_link_capabilities *p_link_caps,
			      struct ecore_bulletin_content *p_bulletin);
enum _ecore_status_t
ecore_vf_pf_tunnel_param_update(struct ecore_hwfn *p_hwfn,
				struct ecore_tunnel_info *p_tunn);
void ecore_vf_set_vf_start_tunn_update_param(struct ecore_tunnel_info *p_tun);

u32 ecore_vf_hw_bar_size(struct ecore_hwfn *p_hwfn, enum BAR_ID bar_id);
#else
static OSAL_INLINE enum _ecore_status_t ecore_vf_hw_prepare(struct ecore_hwfn OSAL_UNUSED *p_hwfn) {return ECORE_INVAL;}
static OSAL_INLINE enum _ecore_status_t ecore_vf_pf_rxq_start(struct ecore_hwfn OSAL_UNUSED *p_hwfn, struct ecore_queue_cid OSAL_UNUSED *p_cid, u16 OSAL_UNUSED bd_max_bytes, dma_addr_t OSAL_UNUSED bd_chain_phys_addr, dma_addr_t OSAL_UNUSED cqe_pbl_addr, u16 OSAL_UNUSED cqe_pbl_size, void OSAL_IOMEM OSAL_UNUSED **pp_prod) {return ECORE_INVAL;}
static OSAL_INLINE enum _ecore_status_t ecore_vf_pf_txq_start(struct ecore_hwfn OSAL_UNUSED *p_hwfn, struct ecore_queue_cid OSAL_UNUSED *p_cid, dma_addr_t OSAL_UNUSED pbl_addr, u16 OSAL_UNUSED pbl_size, void OSAL_IOMEM OSAL_UNUSED **pp_doorbell) {return ECORE_INVAL;}

static OSAL_INLINE enum _ecore_status_t ecore_vf_pf_rxq_stop(struct ecore_hwfn OSAL_UNUSED *p_hwfn, struct ecore_queue_cid OSAL_UNUSED *p_cid, bool OSAL_UNUSED cqe_completion) {return ECORE_INVAL;}
static OSAL_INLINE enum _ecore_status_t ecore_vf_pf_txq_stop(struct ecore_hwfn OSAL_UNUSED *p_hwfn, struct ecore_queue_cid OSAL_UNUSED *p_cid) {return ECORE_INVAL;}
#ifndef LINUX_REMOVE
static OSAL_INLINE enum _ecore_status_t ecore_vf_pf_rxqs_update(struct ecore_hwfn OSAL_UNUSED *p_hwfn, struct ecore_queue_cid OSAL_UNUSED **pp_cid, u8 OSAL_UNUSED num_rxqs, u8 OSAL_UNUSED comp_cqe_flg, u8 OSAL_UNUSED comp_event_flg) {return ECORE_INVAL;}
#endif
static OSAL_INLINE enum _ecore_status_t ecore_vf_pf_vport_update(struct ecore_hwfn OSAL_UNUSED *p_hwfn, struct ecore_sp_vport_update_params OSAL_UNUSED *p_params) {return ECORE_INVAL;}
static OSAL_INLINE enum _ecore_status_t ecore_vf_pf_reset(struct ecore_hwfn OSAL_UNUSED *p_hwfn) {return ECORE_INVAL;}
static OSAL_INLINE enum _ecore_status_t ecore_vf_pf_release(struct ecore_hwfn OSAL_UNUSED *p_hwfn) {return ECORE_INVAL;}
static OSAL_INLINE u16 ecore_vf_get_igu_sb_id(struct ecore_hwfn OSAL_UNUSED *p_hwfn, u16 OSAL_UNUSED sb_id) {return 0;}
static OSAL_INLINE void ecore_vf_set_sb_info(struct ecore_hwfn OSAL_UNUSED *p_hwfn, u16 OSAL_UNUSED sb_id, struct ecore_sb_info OSAL_UNUSED *p_sb) {}

static OSAL_INLINE enum _ecore_status_t ecore_vf_pf_vport_start(struct ecore_hwfn OSAL_UNUSED *p_hwfn, u8 OSAL_UNUSED vport_id, u16 OSAL_UNUSED mtu, u8 OSAL_UNUSED inner_vlan_removal, enum ecore_tpa_mode OSAL_UNUSED tpa_mode, u8 OSAL_UNUSED max_buffers_per_cqe, u8 OSAL_UNUSED only_untagged, u8 OSAL_UNUSED zero_placement_offset) {return ECORE_INVAL;}
static OSAL_INLINE enum _ecore_status_t ecore_vf_pf_vport_stop(struct ecore_hwfn OSAL_UNUSED *p_hwfn) {return ECORE_INVAL;}
static OSAL_INLINE enum _ecore_status_t ecore_vf_pf_filter_ucast(struct ecore_hwfn OSAL_UNUSED *p_hwfn, struct ecore_filter_ucast OSAL_UNUSED *p_param) {return ECORE_INVAL;}
static OSAL_INLINE void ecore_vf_pf_filter_mcast(struct ecore_hwfn OSAL_UNUSED *p_hwfn, struct ecore_filter_mcast OSAL_UNUSED *p_filter_cmd) {}
static OSAL_INLINE enum _ecore_status_t ecore_vf_pf_int_cleanup(struct ecore_hwfn OSAL_UNUSED *p_hwfn) {return ECORE_INVAL;}
static OSAL_INLINE void __ecore_vf_get_link_params(struct ecore_mcp_link_params OSAL_UNUSED *p_params, struct ecore_bulletin_content OSAL_UNUSED *p_bulletin) {}
static OSAL_INLINE void __ecore_vf_get_link_state(struct ecore_mcp_link_state OSAL_UNUSED *p_link, struct ecore_bulletin_content OSAL_UNUSED *p_bulletin) {}
static OSAL_INLINE void __ecore_vf_get_link_caps(struct ecore_mcp_link_capabilities OSAL_UNUSED *p_link_caps, struct ecore_bulletin_content OSAL_UNUSED *p_bulletin) {}
static OSAL_INLINE enum _ecore_status_t ecore_vf_pf_tunnel_param_update(struct ecore_hwfn OSAL_UNUSED *p_hwfn, struct ecore_tunnel_info OSAL_UNUSED *p_tunn) { return ECORE_INVAL; }
static OSAL_INLINE void ecore_vf_set_vf_start_tunn_update_param(struct ecore_tunnel_info OSAL_UNUSED *p_tun) { return; }

static OSAL_INLINE u32
ecore_vf_hw_bar_size(struct ecore_hwfn OSAL_UNUSED *p_hwfn,
		     enum BAR_ID OSAL_UNUSED bar_id)
{
	return 0;
}
#endif

#endif /* __ECORE_VF_H__ */
