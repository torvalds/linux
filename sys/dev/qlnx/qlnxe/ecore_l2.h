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

#ifndef __ECORE_L2_H__
#define __ECORE_L2_H__


#include "ecore.h"
#include "ecore_hw.h"
#include "ecore_spq.h"
#include "ecore_l2_api.h"

#define MAX_QUEUES_PER_QZONE	(sizeof(unsigned long) * 8)
#define ECORE_QUEUE_CID_PF	(0xff)

/* Almost identical to the ecore_queue_start_common_params,
 * but here we maintain the SB index in IGU CAM.
 */
struct ecore_queue_cid_params {
	u8 vport_id;
	u16 queue_id;
	u8 stats_id;
};

 /* Additional parameters required for initialization of the queue_cid
 * and are relevant only for a PF initializing one for its VFs.
 */
struct ecore_queue_cid_vf_params {
	/* Should match the VF's relative index */
	u8 vfid;

	/* 0-based queue index. Should reflect the relative qzone the
	 * VF thinks is associated with it [in its range].
	 */
	u8 vf_qid;

	/* Indicates a VF is legacy, making it differ in several things:
	 *  - Producers would be placed in a different place.
	 *  - Makes assumptions regarding the CIDs.
	 */
	u8 vf_legacy;

	/* For VFs, this index arrives via TLV to diffrentiate between
	 * different queues opened on the same qzone, and is passed
	 * [where the PF would have allocated it internally for its own].
	 */
	u8 qid_usage_idx;
};

struct ecore_queue_cid {
	/* For stats-id, the `rel' is actually absolute as well */
	struct ecore_queue_cid_params rel;
	struct ecore_queue_cid_params abs;

	/* These have no 'relative' meaning */
	u16 sb_igu_id;
	u8 sb_idx;

	u32 cid;
	u16 opaque_fid;

	bool b_is_rx;

	/* VFs queues are mapped differently, so we need to know the
	 * relative queue associated with them [0-based].
	 * Notice this is relevant on the *PF* queue-cid of its VF's queues,
	 * and not on the VF itself.
	 */
	u8 vfid;
	u8 vf_qid;

	/* We need an additional index to diffrentiate between queues opened
	 * for same queue-zone, as VFs would have to communicate the info
	 * to the PF [otherwise PF has no way to diffrentiate].
	 */
	u8 qid_usage_idx;

	/* Legacy VFs might have Rx producer located elsewhere */
	u8 vf_legacy;
#define ECORE_QCID_LEGACY_VF_RX_PROD	(1 << 0)
#define ECORE_QCID_LEGACY_VF_CID	(1 << 1)

	struct ecore_hwfn *p_owner;
};

enum _ecore_status_t ecore_l2_alloc(struct ecore_hwfn *p_hwfn);
void ecore_l2_setup(struct ecore_hwfn *p_hwfn);
void ecore_l2_free(struct ecore_hwfn *p_hwfn);

void ecore_eth_queue_cid_release(struct ecore_hwfn *p_hwfn,
				 struct ecore_queue_cid *p_cid);

struct ecore_queue_cid *
ecore_eth_queue_to_cid(struct ecore_hwfn *p_hwfn, u16 opaque_fid,
		       struct ecore_queue_start_common_params *p_params,
		       bool b_is_rx,
		       struct ecore_queue_cid_vf_params *p_vf_params);

enum _ecore_status_t
ecore_sp_eth_vport_start(struct ecore_hwfn *p_hwfn,
			 struct ecore_sp_vport_start_params *p_params);

/**
 * @brief - Starts an Rx queue, when queue_cid is already prepared
 *
 * @param p_hwfn
 * @param p_cid
 * @param bd_max_bytes
 * @param bd_chain_phys_addr
 * @param cqe_pbl_addr
 * @param cqe_pbl_size
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t
ecore_eth_rxq_start_ramrod(struct ecore_hwfn *p_hwfn,
			   struct ecore_queue_cid *p_cid,
			   u16 bd_max_bytes,
			   dma_addr_t bd_chain_phys_addr,
			   dma_addr_t cqe_pbl_addr,
			   u16 cqe_pbl_size);

/**
 * @brief - Starts a Tx queue, where queue_cid is already prepared
 *
 * @param p_hwfn
 * @param p_cid
 * @param pbl_addr 
 * @param pbl_size
 * @param p_pq_params - parameters for choosing the PQ for this Tx queue
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t
ecore_eth_txq_start_ramrod(struct ecore_hwfn *p_hwfn,
			   struct ecore_queue_cid *p_cid,
			   dma_addr_t pbl_addr, u16 pbl_size,
			   u16 pq_id);

u8 ecore_mcast_bin_from_mac(u8 *mac);

enum _ecore_status_t ecore_set_rxq_coalesce(struct ecore_hwfn *p_hwfn,
					    struct ecore_ptt *p_ptt,
					    u16 coalesce,
					    struct ecore_queue_cid *p_cid);

enum _ecore_status_t ecore_set_txq_coalesce(struct ecore_hwfn *p_hwfn,
					    struct ecore_ptt *p_ptt,
					    u16 coalesce,
					    struct ecore_queue_cid *p_cid);

enum _ecore_status_t ecore_get_rxq_coalesce(struct ecore_hwfn *p_hwfn,
					    struct ecore_ptt *p_ptt,
					    struct ecore_queue_cid *p_cid,
					    u16 *p_hw_coal);

enum _ecore_status_t ecore_get_txq_coalesce(struct ecore_hwfn *p_hwfn,
					    struct ecore_ptt *p_ptt,
					    struct ecore_queue_cid *p_cid,
					    u16 *p_hw_coal);

#endif
