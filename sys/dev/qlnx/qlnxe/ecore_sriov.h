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

#ifndef __ECORE_SRIOV_H__
#define __ECORE_SRIOV_H__

#include "ecore_status.h"
#include "ecore_vfpf_if.h"
#include "ecore_iov_api.h"
#include "ecore_hsi_common.h"
#include "ecore_l2.h"

#define ECORE_ETH_MAX_VF_NUM_VLAN_FILTERS \
	(MAX_NUM_VFS_E4 * ECORE_ETH_VF_NUM_VLAN_FILTERS)

/* Represents a full message. Both the request filled by VF
 * and the response filled by the PF. The VF needs one copy
 * of this message, it fills the request part and sends it to
 * the PF. The PF will copy the response to the response part for
 * the VF to later read it. The PF needs to hold a message like this
 * per VF, the request that is copied to the PF is placed in the
 * request size, and the response is filled by the PF before sending
 * it to the VF.
 */
struct ecore_vf_mbx_msg {
	union vfpf_tlvs req;
	union pfvf_tlvs resp;
};

/* This mailbox is maintained per VF in its PF
 * contains all information required for sending / receiving
 * a message
 */
struct ecore_iov_vf_mbx {
	union vfpf_tlvs		*req_virt;
	dma_addr_t		req_phys;
	union pfvf_tlvs		*reply_virt;
	dma_addr_t		reply_phys;

	/* Address in VF where a pending message is located */
	dma_addr_t		pending_req;

	/* Message from VF awaits handling */
	bool			b_pending_msg;

	u8 *offset;

#ifdef CONFIG_ECORE_SW_CHANNEL
	struct ecore_iov_sw_mbx sw_mbx;
#endif

	/* VF GPA address */
	u32			vf_addr_lo;
	u32			vf_addr_hi;

	struct vfpf_first_tlv	first_tlv;	/* saved VF request header */

	u8			flags;
#define VF_MSG_INPROCESS	0x1	/* failsafe - the FW should prevent
					 * more then one pending msg
					 */
};

#define ECORE_IOV_LEGACY_QID_RX (0)
#define ECORE_IOV_LEGACY_QID_TX (1)
#define ECORE_IOV_QID_INVALID (0xFE)

struct ecore_vf_queue_cid {
	bool b_is_tx;
	struct ecore_queue_cid *p_cid;
};

/* Describes a qzone associated with the VF */
struct ecore_vf_queue {
	/* Input from upper-layer, mapping relateive queue to queue-zone */
	u16 fw_rx_qid;
	u16 fw_tx_qid;

	struct ecore_vf_queue_cid cids[MAX_QUEUES_PER_QZONE];
};

enum vf_state {
	VF_FREE		= 0,	/* VF ready to be acquired holds no resc */
	VF_ACQUIRED	= 1,	/* VF, aquired, but not initalized */
	VF_ENABLED	= 2,	/* VF, Enabled */
	VF_RESET	= 3,	/* VF, FLR'd, pending cleanup */
	VF_STOPPED      = 4     /* VF, Stopped */
};

struct ecore_vf_vlan_shadow {
	bool used;
	u16 vid;
};

struct ecore_vf_shadow_config {
	/* Shadow copy of all guest vlans */
	struct ecore_vf_vlan_shadow vlans[ECORE_ETH_VF_NUM_VLAN_FILTERS + 1];

	/* Shadow copy of all configured MACs; Empty if forcing MACs */
	u8 macs[ECORE_ETH_VF_NUM_MAC_FILTERS][ETH_ALEN];
	u8 inner_vlan_removal;
};

/* PFs maintain an array of this structure, per VF */
struct ecore_vf_info {
	struct ecore_iov_vf_mbx vf_mbx;
	enum vf_state state;
	bool b_init;
	bool b_malicious;
	u8			to_disable;

	struct ecore_bulletin	bulletin;
	dma_addr_t		vf_bulletin;

#ifdef CONFIG_ECORE_SW_CHANNEL
	/* Determine whether PF communicate with VF using HW/SW channel */
	bool	b_hw_channel;
#endif

	/* PF saves a copy of the last VF acquire message */
	struct vfpf_acquire_tlv acquire;

	u32			concrete_fid;
	u16			opaque_fid;
	u16			mtu;

	u8			vport_id;
	u8			rss_eng_id;
	u8			relative_vf_id;
	u8			abs_vf_id;
#define ECORE_VF_ABS_ID(p_hwfn, p_vf)	(ECORE_PATH_ID(p_hwfn) ? \
					 (p_vf)->abs_vf_id + MAX_NUM_VFS_BB : \
					 (p_vf)->abs_vf_id)

	u8			vport_instance; /* Number of active vports */
	u8			num_rxqs;
	u8			num_txqs;

	u16			rx_coal;
	u16			tx_coal;

	u8			num_sbs;

	u8			num_mac_filters;
	u8			num_vlan_filters;

	struct ecore_vf_queue	vf_queues[ECORE_MAX_VF_CHAINS_PER_PF];
	u16			igu_sbs[ECORE_MAX_VF_CHAINS_PER_PF];

	/* TODO - Only windows is using it - should be removed */
	u8 was_malicious;
	u8 num_active_rxqs;
	void *ctx;
	struct ecore_public_vf_info p_vf_info;
	bool spoof_chk;		/* Current configured on HW */
	bool req_spoofchk_val;  /* Requested value */

	/* Stores the configuration requested by VF */
	struct ecore_vf_shadow_config shadow_config;

	/* A bitfield using bulletin's valid-map bits, used to indicate
	 * which of the bulletin board features have been configured.
	 */
	u64 configured_features;
#define ECORE_IOV_CONFIGURED_FEATURES_MASK	((1 << MAC_ADDR_FORCED) | \
						 (1 << VLAN_ADDR_FORCED))
};

/* This structure is part of ecore_hwfn and used only for PFs that have sriov
 * capability enabled.
 */
struct ecore_pf_iov {
	struct ecore_vf_info	vfs_array[MAX_NUM_VFS_E4];
	u64			pending_flr[ECORE_VF_ARRAY_LENGTH];

#ifndef REMOVE_DBG
	/* This doesn't serve anything functionally, but it makes windows
	 * debugging of IOV related issues easier.
	 */
	u64			active_vfs[ECORE_VF_ARRAY_LENGTH];
#endif

	/* Allocate message address continuosuly and split to each VF */
	void			*mbx_msg_virt_addr;
	dma_addr_t		mbx_msg_phys_addr;
	u32			mbx_msg_size;
	void			*mbx_reply_virt_addr;
	dma_addr_t		mbx_reply_phys_addr;
	u32			mbx_reply_size;
	void			*p_bulletins;
	dma_addr_t		bulletins_phys;
	u32			bulletins_size;
};

#ifdef CONFIG_ECORE_SRIOV
/**
 * @brief Read sriov related information and allocated resources
 *  reads from configuraiton space, shmem, etc.
 *
 * @param p_hwfn
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t ecore_iov_hw_info(struct ecore_hwfn *p_hwfn);

/**
 * @brief ecore_add_tlv - place a given tlv on the tlv buffer at next offset
 *
 * @param offset
 * @param type
 * @param length
 *
 * @return pointer to the newly placed tlv
 */
void *ecore_add_tlv(u8 **offset, u16 type, u16 length);

/**
 * @brief list the types and lengths of the tlvs on the buffer
 *
 * @param p_hwfn
 * @param tlvs_list
 */
void ecore_dp_tlv_list(struct ecore_hwfn *p_hwfn,
		       void *tlvs_list);

/**
 * @brief ecore_iov_alloc - allocate sriov related resources
 *
 * @param p_hwfn
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t ecore_iov_alloc(struct ecore_hwfn *p_hwfn);

/**
 * @brief ecore_iov_setup - setup sriov related resources
 *
 * @param p_hwfn
 */
void ecore_iov_setup(struct ecore_hwfn	*p_hwfn);

/**
 * @brief ecore_iov_free - free sriov related resources
 *
 * @param p_hwfn
 */
void ecore_iov_free(struct ecore_hwfn *p_hwfn);

/**
 * @brief free sriov related memory that was allocated during hw_prepare
 *
 * @param p_dev
 */
void ecore_iov_free_hw_info(struct ecore_dev *p_dev);

/**
 * @brief Mark structs of vfs that have been FLR-ed.
 *
 * @param p_hwfn
 * @param disabled_vfs - bitmask of all VFs on path that were FLRed
 *
 * @return true iff one of the PF's vfs got FLRed. false otherwise.
 */
bool ecore_iov_mark_vf_flr(struct ecore_hwfn *p_hwfn,
			  u32 *disabled_vfs);

/**
 * @brief Search extended TLVs in request/reply buffer.
 *
 * @param p_hwfn
 * @param p_tlvs_list - Pointer to tlvs list
 * @param req_type - Type of TLV
 *
 * @return pointer to tlv type if found, otherwise returns NULL.
 */
void *ecore_iov_search_list_tlvs(struct ecore_hwfn *p_hwfn,
				 void *p_tlvs_list, u16 req_type);

/**
 * @brief ecore_iov_get_vf_info - return the database of a
 *        specific VF
 *
 * @param p_hwfn
 * @param relative_vf_id - relative id of the VF for which info
 *			 is requested
 * @param b_enabled_only - false iff want to access even if vf is disabled
 *
 * @return struct ecore_vf_info*
 */
struct ecore_vf_info *ecore_iov_get_vf_info(struct ecore_hwfn *p_hwfn,
					    u16 relative_vf_id,
					    bool b_enabled_only);
#else
static OSAL_INLINE enum _ecore_status_t ecore_iov_hw_info(struct ecore_hwfn OSAL_UNUSED *p_hwfn) {return ECORE_SUCCESS;}
static OSAL_INLINE void *ecore_add_tlv(u8 OSAL_UNUSED **offset, OSAL_UNUSED u16 type, OSAL_UNUSED u16 length) {return OSAL_NULL;}
static OSAL_INLINE void ecore_dp_tlv_list(struct ecore_hwfn OSAL_UNUSED *p_hwfn, void OSAL_UNUSED *tlvs_list) {}
static OSAL_INLINE enum _ecore_status_t ecore_iov_alloc(struct ecore_hwfn OSAL_UNUSED *p_hwfn) {return ECORE_SUCCESS;}
static OSAL_INLINE void ecore_iov_setup(struct ecore_hwfn OSAL_UNUSED *p_hwfn) {}
static OSAL_INLINE void ecore_iov_free(struct ecore_hwfn OSAL_UNUSED *p_hwfn) {}
static OSAL_INLINE void ecore_iov_free_hw_info(struct ecore_dev OSAL_UNUSED *p_dev) {}
static OSAL_INLINE u32 ecore_crc32(u32 OSAL_UNUSED crc, u8 OSAL_UNUSED *ptr, u32 OSAL_UNUSED length) {return 0;}
static OSAL_INLINE bool ecore_iov_mark_vf_flr(struct ecore_hwfn OSAL_UNUSED *p_hwfn, u32 OSAL_UNUSED *disabled_vfs) {return false;}
static OSAL_INLINE void *ecore_iov_search_list_tlvs(struct ecore_hwfn OSAL_UNUSED *p_hwfn, void OSAL_UNUSED *p_tlvs_list, u16 OSAL_UNUSED req_type) {return OSAL_NULL;}
static OSAL_INLINE struct ecore_vf_info *ecore_iov_get_vf_info(struct ecore_hwfn OSAL_UNUSED *p_hwfn, u16 OSAL_UNUSED relative_vf_id, bool OSAL_UNUSED b_enabled_only) {return OSAL_NULL;}

#endif
#endif /* __ECORE_SRIOV_H__ */
