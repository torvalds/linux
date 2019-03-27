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

#ifndef __ECORE_SRIOV_API_H__
#define __ECORE_SRIOV_API_H__

#include "common_hsi.h"
#include "ecore_status.h"

#define ECORE_ETH_VF_NUM_MAC_FILTERS 1
#define ECORE_ETH_VF_NUM_VLAN_FILTERS 2
#define ECORE_VF_ARRAY_LENGTH (3)

#define IS_VF(p_dev)		((p_dev)->b_is_vf)
#define IS_PF(p_dev)		(!((p_dev)->b_is_vf))
#ifdef CONFIG_ECORE_SRIOV
#define IS_PF_SRIOV(p_hwfn)	(!!((p_hwfn)->p_dev->p_iov_info))
#else
#define IS_PF_SRIOV(p_hwfn)	(0)
#endif
#define IS_PF_SRIOV_ALLOC(p_hwfn)	(!!((p_hwfn)->pf_iov_info))
#define IS_PF_PDA(p_hwfn)	0 /* @@TBD Michalk */

/* @@@ TBD MichalK - what should this number be*/
#define ECORE_MAX_VF_CHAINS_PER_PF 16

/* vport update extended feature tlvs flags */
enum ecore_iov_vport_update_flag {
	ECORE_IOV_VP_UPDATE_ACTIVATE		= 0,
	ECORE_IOV_VP_UPDATE_VLAN_STRIP		= 1,
	ECORE_IOV_VP_UPDATE_TX_SWITCH		= 2,
	ECORE_IOV_VP_UPDATE_MCAST		= 3,
	ECORE_IOV_VP_UPDATE_ACCEPT_PARAM	= 4,
	ECORE_IOV_VP_UPDATE_RSS			= 5,
	ECORE_IOV_VP_UPDATE_ACCEPT_ANY_VLAN	= 6,
	ECORE_IOV_VP_UPDATE_SGE_TPA		= 7,
	ECORE_IOV_VP_UPDATE_MAX			= 8,
};

/*PF to VF STATUS is part of vfpf-channel API
and must be forward compatible */
enum ecore_iov_pf_to_vf_status {
        PFVF_STATUS_WAITING = 0,
        PFVF_STATUS_SUCCESS,
        PFVF_STATUS_FAILURE,
        PFVF_STATUS_NOT_SUPPORTED,
        PFVF_STATUS_NO_RESOURCE,
        PFVF_STATUS_FORCED,
	PFVF_STATUS_MALICIOUS,
};

struct ecore_mcp_link_params;
struct ecore_mcp_link_state;
struct ecore_mcp_link_capabilities;

/* These defines are used by the hw-channel; should never change order */
#define VFPF_ACQUIRE_OS_LINUX (0)
#define VFPF_ACQUIRE_OS_WINDOWS (1)
#define VFPF_ACQUIRE_OS_ESX (2)
#define VFPF_ACQUIRE_OS_SOLARIS (3)
#define VFPF_ACQUIRE_OS_LINUX_USERSPACE (4)
#define VFPF_ACQUIRE_OS_FREEBSD (5)

struct ecore_vf_acquire_sw_info {
	u32 driver_version;
	u8 os_type;
};

struct ecore_public_vf_info {
	/* These copies will later be reflected in the bulletin board,
	 * but this copy should be newer.
	 */
	u8 forced_mac[ETH_ALEN];
	u16 forced_vlan;
};

struct ecore_iov_vf_init_params {
	u16 rel_vf_id;

	/* Number of requested Queues; Currently, don't support different
	 * number of Rx/Tx queues.
	 */
	/* TODO - remove this limitation */
	u16 num_queues;

	/* Allow the client to choose which qzones to use for Rx/Tx,
	 * and which queue_base to use for Tx queues on a per-queue basis.
	 * Notice values should be relative to the PF resources.
	 */
	u16 req_rx_queue[ECORE_MAX_VF_CHAINS_PER_PF];
	u16 req_tx_queue[ECORE_MAX_VF_CHAINS_PER_PF];

	u8 vport_id;

	/* Should be set in case RSS is going to be used for VF */
	u8 rss_eng_id;
};

#ifdef CONFIG_ECORE_SW_CHANNEL
/* This is SW channel related only... */
enum mbx_state {
	VF_PF_UNKNOWN_STATE			= 0,
	VF_PF_WAIT_FOR_START_REQUEST		= 1,
	VF_PF_WAIT_FOR_NEXT_CHUNK_OF_REQUEST	= 2,
	VF_PF_REQUEST_IN_PROCESSING		= 3,
	VF_PF_RESPONSE_READY			= 4,
};

struct ecore_iov_sw_mbx {
	enum mbx_state		mbx_state;

	u32			request_size;
	u32			request_offset;

	u32			response_size;
	u32			response_offset;
};

/**
 * @brief Get the vf sw mailbox params
 *
 * @param p_hwfn
 * @param rel_vf_id
 *
 * @return struct ecore_iov_sw_mbx*
 */
struct ecore_iov_sw_mbx*
ecore_iov_get_vf_sw_mbx(struct ecore_hwfn *p_hwfn,
			u16 rel_vf_id);
#endif

/* This struct is part of ecore_dev and contains data relevant to all hwfns;
 * Initialized only if SR-IOV cpabability is exposed in PCIe config space.
 */
struct ecore_hw_sriov_info {
	/* standard SRIOV capability fields, mostly for debugging */
	int	pos;		/* capability position */
	int	nres;		/* number of resources */
	u32	cap;		/* SR-IOV Capabilities */
	u16	ctrl;		/* SR-IOV Control */
	u16	total_vfs;	/* total VFs associated with the PF */
	u16	num_vfs;        /* number of vfs that have been started */
	u16	initial_vfs;    /* initial VFs associated with the PF */
	u16	nr_virtfn;	/* number of VFs available */
	u16	offset;		/* first VF Routing ID offset */
	u16	stride;		/* following VF stride */
	u16	vf_device_id;	/* VF device id */
	u32	pgsz;		/* page size for BAR alignment */
	u8	link;		/* Function Dependency Link */

	u32	first_vf_in_pf;
};

#ifdef CONFIG_ECORE_SRIOV
#ifndef LINUX_REMOVE
/**
 * @brief mark/clear all VFs before/after an incoming PCIe sriov
 *        disable.
 *
 * @param p_dev
 * @param to_disable
 */
void ecore_iov_set_vfs_to_disable(struct ecore_dev *p_dev,
				  u8 to_disable);

/**
 * @brief mark/clear chosen VF before/after an incoming PCIe
 *        sriov disable.
 *
 * @param p_dev
 * @param rel_vf_id
 * @param to_disable
 */
void ecore_iov_set_vf_to_disable(struct ecore_dev *p_dev,
				 u16 rel_vf_id,
				 u8 to_disable);

/**
 * @brief ecore_iov_init_hw_for_vf - initialize the HW for
 *        enabling access of a VF. Also includes preparing the
 *        IGU for VF access. This needs to be called AFTER hw is
 *        initialized and BEFORE VF is loaded inside the VM.
 *
 * @param p_hwfn
 * @param p_ptt
 * @param p_params
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t
ecore_iov_init_hw_for_vf(struct ecore_hwfn *p_hwfn,
			 struct ecore_ptt *p_ptt,
			 struct ecore_iov_vf_init_params *p_params);

/**
 * @brief ecore_iov_process_mbx_req - process a request received
 *        from the VF
 *
 * @param p_hwfn
 * @param p_ptt
 * @param vfid
 */
void ecore_iov_process_mbx_req(struct ecore_hwfn *p_hwfn,
			       struct ecore_ptt *p_ptt,
			       int vfid);

/**
 * @brief ecore_iov_release_hw_for_vf - called once upper layer
 *        knows VF is done with - can release any resources
 *        allocated for VF at this point. this must be done once
 *        we know VF is no longer loaded in VM.
 *
 * @param p_hwfn
 * @param p_ptt
 * @param rel_vf_id
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t ecore_iov_release_hw_for_vf(struct ecore_hwfn *p_hwfn,
						 struct ecore_ptt *p_ptt,
						 u16 rel_vf_id);

/**
 * @brief ecore_iov_set_vf_ctx - set a context for a given VF
 *
 * @param p_hwfn
 * @param vf_id
 * @param ctx
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t ecore_iov_set_vf_ctx(struct ecore_hwfn *p_hwfn,
					  u16 vf_id,
					  void *ctx);

/**
 * @brief FLR cleanup for all VFs
 *
 * @param p_hwfn
 * @param p_ptt
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t ecore_iov_vf_flr_cleanup(struct ecore_hwfn *p_hwfn,
					      struct ecore_ptt *p_ptt);

/**
 * @brief FLR cleanup for single VF
 *
 * @param p_hwfn
 * @param p_ptt
 * @param rel_vf_id
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t
ecore_iov_single_vf_flr_cleanup(struct ecore_hwfn *p_hwfn,
				struct ecore_ptt *p_ptt,
				u16 rel_vf_id);

/**
 * @brief Update the bulletin with link information. Notice this does NOT
 *        send a bulletin update, only updates the PF's bulletin.
 *
 * @param p_hwfn
 * @param p_vf
 * @param params - the link params to use for the VF link configuration
 * @param link - the link output to use for the VF link configuration
 * @param p_caps - the link default capabilities.
 */
void ecore_iov_set_link(struct ecore_hwfn *p_hwfn,
			u16 vfid,
			struct ecore_mcp_link_params *params,
			struct ecore_mcp_link_state *link,
			struct ecore_mcp_link_capabilities *p_caps);

/**
 * @brief Returns link information as perceived by VF.
 *
 * @param p_hwfn
 * @param p_vf
 * @param p_params - the link params visible to vf.
 * @param p_link - the link state visible to vf.
 * @param p_caps - the link default capabilities visible to vf.
 */
void ecore_iov_get_link(struct ecore_hwfn *p_hwfn,
			u16 vfid,
			struct ecore_mcp_link_params *params,
			struct ecore_mcp_link_state *link,
			struct ecore_mcp_link_capabilities *p_caps);

/**
 * @brief return if the VF is pending FLR
 *
 * @param p_hwfn
 * @param rel_vf_id
 *
 * @return bool
 */
bool ecore_iov_is_vf_pending_flr(struct ecore_hwfn *p_hwfn,
				 u16 rel_vf_id);
#endif

/**
 * @brief Check if given VF ID @vfid is valid
 *        w.r.t. @b_enabled_only value
 *        if b_enabled_only = true - only enabled VF id is valid
 *        else any VF id less than max_vfs is valid
 *
 * @param p_hwfn
 * @param rel_vf_id - Relative VF ID
 * @param b_enabled_only - consider only enabled VF
 * @param b_non_malicious - true iff we want to validate vf isn't malicious.
 *
 * @return bool - true for valid VF ID
 */
bool ecore_iov_is_valid_vfid(struct ecore_hwfn *p_hwfn,
			     int rel_vf_id,
			     bool b_enabled_only, bool b_non_malicious);

#ifndef LINUX_REMOVE
/**
 * @brief Get VF's public info structure
 *
 * @param p_hwfn
 * @param vfid - Relative VF ID
 * @param b_enabled_only - false if want to access even if vf is disabled
 *
 * @return struct ecore_public_vf_info *
 */
struct ecore_public_vf_info*
ecore_iov_get_public_vf_info(struct ecore_hwfn *p_hwfn,
			     u16 vfid, bool b_enabled_only);

/**
 * @brief fills a bitmask of all VFs which have pending unhandled
 *        messages.
 *
 * @param p_hwfn
 */
void ecore_iov_pf_get_pending_events(struct ecore_hwfn *p_hwfn,
				     u64 *events);

/**
 * @brief Copy VF's message to PF's buffer
 *
 * @param p_hwfn
 * @param ptt
 * @param vfid
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t ecore_iov_copy_vf_msg(struct ecore_hwfn *p_hwfn,
					   struct ecore_ptt *ptt,
					   int vfid);
/**
 * @brief Set forced MAC address in PFs copy of bulletin board
 *        and configures FW/HW to support the configuration.
 *
 * @param p_hwfn
 * @param mac
 * @param vfid
 */
void ecore_iov_bulletin_set_forced_mac(struct ecore_hwfn *p_hwfn,
				       u8 *mac, int vfid);

/**
 * @brief Set MAC address in PFs copy of bulletin board without 
 *        configuring FW/HW.
 *
 * @param p_hwfn
 * @param mac
 * @param vfid
 */
enum _ecore_status_t ecore_iov_bulletin_set_mac(struct ecore_hwfn *p_hwfn,
						u8 *mac, int vfid);

/**
 * @brief Set default behaviour of VF in case no vlans are configured for it
 *        whether to accept only untagged traffic or all.
 *        Must be called prior to the VF vport-start.
 *
 * @param p_hwfn
 * @param b_untagged_only
 * @param vfid
 *
 * @return ECORE_SUCCESS if configuration would stick.
 */
enum _ecore_status_t
ecore_iov_bulletin_set_forced_untagged_default(struct ecore_hwfn *p_hwfn,
					       bool b_untagged_only,
					       int vfid);

/**
 * @brief Get VFs opaque fid.
 *
 * @param p_hwfn
 * @param vfid
 * @param opaque_fid
 */
void ecore_iov_get_vfs_opaque_fid(struct ecore_hwfn *p_hwfn, int vfid,
				  u16 *opaque_fid);

/**
 * @brief Set forced VLAN [pvid] in PFs copy of bulletin board
 *        and configures FW/HW to support the configuration.
 *        Setting of pvid 0 would clear the feature.
 * @param p_hwfn
 * @param pvid
 * @param vfid
 */
void ecore_iov_bulletin_set_forced_vlan(struct ecore_hwfn *p_hwfn,
					u16 pvid, int vfid);

/**
 * @brief Check if VF has VPORT instance. This can be used
 *	  to check if VPORT is active.
 *
 * @param p_hwfn
 */
bool ecore_iov_vf_has_vport_instance(struct ecore_hwfn *p_hwfn, int vfid);

/**
 * @brief PF posts the bulletin to the VF
 *
 * @param p_hwfn
 * @param p_vf
 * @param p_ptt
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t ecore_iov_post_vf_bulletin(struct ecore_hwfn *p_hwfn,
						int vfid,
						struct ecore_ptt *p_ptt);

/**
 * @brief Check if given VF (@vfid) is marked as stopped
 *
 * @param p_hwfn
 * @param vfid
 *
 * @return bool : true if stopped
 */
bool ecore_iov_is_vf_stopped(struct ecore_hwfn *p_hwfn, int vfid);

/**
 * @brief Configure VF anti spoofing
 *
 * @param p_hwfn
 * @param vfid
 * @param val - spoofchk value - true/false
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t ecore_iov_spoofchk_set(struct ecore_hwfn *p_hwfn,
					    int vfid, bool val);

/**
 * @brief Get VF's configured spoof value.
 *
 * @param p_hwfn
 * @param vfid
 *
 * @return bool - spoofchk value - true/false
 */
bool ecore_iov_spoofchk_get(struct ecore_hwfn *p_hwfn, int vfid);

/**
 * @brief Check for SRIOV sanity by PF.
 *
 * @param p_hwfn
 * @param vfid
 *
 * @return bool - true if sanity checks passes, else false
 */
bool ecore_iov_pf_sanity_check(struct ecore_hwfn *p_hwfn, int vfid);

/**
 * @brief Get the num of VF chains.
 *
 * @param p_hwfn
 *
 * @return u8
 */
u8 ecore_iov_vf_chains_per_pf(struct ecore_hwfn *p_hwfn);

/**
 * @brief Get vf request mailbox params
 *
 * @param p_hwfn
 * @param rel_vf_id
 * @param pp_req_virt_addr
 * @param p_req_virt_size
 */
void ecore_iov_get_vf_req_virt_mbx_params(struct ecore_hwfn *p_hwfn,
					  u16 rel_vf_id,
					  void **pp_req_virt_addr,
					  u16 *p_req_virt_size);

/**
 * @brief Get vf mailbox params
 *
 * @param p_hwfn
 * @param rel_vf_id
 * @param pp_reply_virt_addr
 * @param p_reply_virt_size
 */
void ecore_iov_get_vf_reply_virt_mbx_params(struct ecore_hwfn *p_hwfn,
					    u16	rel_vf_id,
					    void **pp_reply_virt_addr,
					    u16	*p_reply_virt_size);

/**
 * @brief Validate if the given length is a valid vfpf message
 *        length
 *
 * @param length
 *
 * @return bool
 */
bool ecore_iov_is_valid_vfpf_msg_length(u32 length);

/**
 * @brief Return the max pfvf message length
 *
 * @return u32
 */
u32 ecore_iov_pfvf_msg_length(void);

/**
 * @brief Returns forced MAC address if one is configured
 *
 * @parm p_hwfn
 * @parm rel_vf_id
 *
 * @return OSAL_NULL if mac isn't forced; Otherwise, returns MAC.
 */
u8 *ecore_iov_bulletin_get_forced_mac(struct ecore_hwfn *p_hwfn,
				      u16 rel_vf_id);

/**
 * @brief Returns pvid if one is configured
 *
 * @parm p_hwfn
 * @parm rel_vf_id
 *
 * @return 0 if no pvid is configured, otherwise the pvid.
 */
u16 ecore_iov_bulletin_get_forced_vlan(struct ecore_hwfn *p_hwfn,
				       u16 rel_vf_id);
/**
 * @brief Configure VFs tx rate
 *
 * @param p_hwfn
 * @param p_ptt
 * @param vfid
 * @param val - tx rate value in Mb/sec.
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t ecore_iov_configure_tx_rate(struct ecore_hwfn *p_hwfn,
						 struct ecore_ptt *p_ptt,
						 int vfid, int val);

/**
 * @brief - Retrieves the statistics associated with a VF
 *
 * @param p_hwfn
 * @param p_ptt
 * @param vfid
 * @param p_stats - this will be filled with the VF statistics
 *
 * @return ECORE_SUCCESS iff statistics were retrieved. Error otherwise.
 */
enum _ecore_status_t ecore_iov_get_vf_stats(struct ecore_hwfn *p_hwfn,
					    struct ecore_ptt *p_ptt,
					    int vfid,
					    struct ecore_eth_stats *p_stats);

/**
 * @brief - Retrieves num of rxqs chains
 *
 * @param p_hwfn
 * @param rel_vf_id
 *
 * @return num of rxqs chains.
 */
u8 ecore_iov_get_vf_num_rxqs(struct ecore_hwfn *p_hwfn,
			     u16 rel_vf_id);

/**
 * @brief - Retrieves num of active rxqs chains
 *
 * @param p_hwfn
 * @param rel_vf_id
 *
 * @return
 */
u8 ecore_iov_get_vf_num_active_rxqs(struct ecore_hwfn *p_hwfn,
				    u16 rel_vf_id);

/**
 * @brief - Retrieves ctx pointer
 *
 * @param p_hwfn
 * @param rel_vf_id
 *
 * @return
 */
void *ecore_iov_get_vf_ctx(struct ecore_hwfn *p_hwfn,
			   u16 rel_vf_id);

/**
 * @brief - Retrieves VF`s num sbs
 *
 * @param p_hwfn
 * @param rel_vf_id
 *
 * @return
 */
u8 ecore_iov_get_vf_num_sbs(struct ecore_hwfn *p_hwfn,
			    u16 rel_vf_id);

/**
 * @brief - Returm true if VF is waiting for acquire
 *
 * @param p_hwfn
 * @param rel_vf_id
 *
 * @return
 */
bool ecore_iov_is_vf_wait_for_acquire(struct ecore_hwfn *p_hwfn,
				      u16 rel_vf_id);

/**
 * @brief - Returm true if VF is acquired but not initialized
 *
 * @param p_hwfn
 * @param rel_vf_id
 *
 * @return
 */
bool ecore_iov_is_vf_acquired_not_initialized(struct ecore_hwfn *p_hwfn,
					      u16 rel_vf_id);

/**
 * @brief - Returm true if VF is acquired and initialized
 *
 * @param p_hwfn
 * @param rel_vf_id
 *
 * @return
 */
bool ecore_iov_is_vf_initialized(struct ecore_hwfn *p_hwfn,
				 u16 rel_vf_id);

/**
 * @brief - Returm true if VF has started in FW
 *
 * @param p_hwfn
 * @param rel_vf_id
 *
 * @return
 */
bool ecore_iov_is_vf_started(struct ecore_hwfn *p_hwfn,
			     u16 rel_vf_id);

/**
 * @brief - Get VF's vport min rate configured.
 * @param p_hwfn
 * @param rel_vf_id
 *
 * @return - rate in Mbps
 */
int ecore_iov_get_vf_min_rate(struct ecore_hwfn *p_hwfn, int vfid);

/**
 * @brief - Configure min rate for VF's vport.
 * @param p_dev
 * @param vfid
 * @param - rate in Mbps
 *
 * @return
 */
enum _ecore_status_t ecore_iov_configure_min_tx_rate(struct ecore_dev *p_dev,
						     int vfid, u32 rate);

#endif

/**
 * @brief ecore_pf_configure_vf_queue_coalesce - PF configure coalesce parameters
 *    of VFs for Rx and Tx queue.
 *    While the API allows setting coalescing per-qid, all queues sharing a SB
 *    should be in same range [i.e., either 0-0x7f, 0x80-0xff or 0x100-0x1ff]
 *    otherwise configuration would break.
 *
 * @param p_hwfn
 * @param rx_coal - Rx Coalesce value in micro seconds.
 * @param tx_coal - TX Coalesce value in micro seconds.
 * @param vf_id
 * @param qid
 *
 * @return int
 **/
enum _ecore_status_t
ecore_iov_pf_configure_vf_queue_coalesce(struct ecore_hwfn *p_hwfn,
					 u16 rx_coal, u16 tx_coal,
					 u16 vf_id, u16 qid);

/**
 * @brief - Given a VF index, return index of next [including that] active VF.
 *
 * @param p_hwfn
 * @param rel_vf_id
 *
 * @return MAX_NUM_VFS_E4 in case no further active VFs, otherwise index.
 */
u16 ecore_iov_get_next_active_vf(struct ecore_hwfn *p_hwfn, u16 rel_vf_id);
void ecore_iov_bulletin_set_udp_ports(struct ecore_hwfn *p_hwfn, int vfid,
				      u16 vxlan_port, u16 geneve_port);

#ifdef CONFIG_ECORE_SW_CHANNEL
/**
 * @brief Set whether PF should communicate with VF using SW/HW channel
 *        Needs to be called for an enabled VF before acquire is over
 *        [latest good point for doing that is OSAL_IOV_VF_ACQUIRE()]
 *
 * @param p_hwfn
 * @param vfid - relative vf index
 * @param b_is_hw - true iff PF is to use HW channel for communication
 */
void ecore_iov_set_vf_hw_channel(struct ecore_hwfn *p_hwfn, int vfid,
				 bool b_is_hw);
#endif
#else
#ifndef LINUX_REMOVE
static OSAL_INLINE void ecore_iov_set_vfs_to_disable(struct ecore_dev OSAL_UNUSED *p_dev, u8 OSAL_UNUSED to_disable) {}
static OSAL_INLINE void ecore_iov_set_vf_to_disable(struct ecore_dev OSAL_UNUSED *p_dev, u16 OSAL_UNUSED rel_vf_id, u8 OSAL_UNUSED to_disable) {}
static OSAL_INLINE enum _ecore_status_t ecore_iov_init_hw_for_vf(struct ecore_hwfn OSAL_UNUSED *p_hwfn, struct ecore_ptt OSAL_UNUSED *p_ptt, struct ecore_iov_vf_init_params OSAL_UNUSED *p_params) {return ECORE_INVAL;}
static OSAL_INLINE void ecore_iov_process_mbx_req(struct ecore_hwfn OSAL_UNUSED *p_hwfn, struct ecore_ptt OSAL_UNUSED *p_ptt, int OSAL_UNUSED vfid) {}
static OSAL_INLINE enum _ecore_status_t ecore_iov_release_hw_for_vf(struct ecore_hwfn OSAL_UNUSED *p_hwfn, struct ecore_ptt OSAL_UNUSED *p_ptt, u16 OSAL_UNUSED rel_vf_id) {return ECORE_SUCCESS;}
static OSAL_INLINE enum _ecore_status_t ecore_iov_set_vf_ctx(struct ecore_hwfn OSAL_UNUSED *p_hwfn, u16 OSAL_UNUSED vf_id, OSAL_UNUSED void *ctx) {return ECORE_INVAL;}
static OSAL_INLINE enum _ecore_status_t ecore_iov_vf_flr_cleanup(struct ecore_hwfn OSAL_UNUSED *p_hwfn, struct ecore_ptt OSAL_UNUSED *p_ptt) {return ECORE_INVAL;}
static OSAL_INLINE enum _ecore_status_t ecore_iov_single_vf_flr_cleanup(struct ecore_hwfn OSAL_UNUSED *p_hwfn, struct ecore_ptt OSAL_UNUSED *p_ptt, u16 OSAL_UNUSED rel_vf_id) {return ECORE_INVAL;}
static OSAL_INLINE void ecore_iov_set_link(struct ecore_hwfn OSAL_UNUSED *p_hwfn, u16 OSAL_UNUSED vfid, struct ecore_mcp_link_params OSAL_UNUSED *params, struct ecore_mcp_link_state OSAL_UNUSED *link, struct ecore_mcp_link_capabilities OSAL_UNUSED *p_caps) {}
static OSAL_INLINE void ecore_iov_get_link(struct ecore_hwfn OSAL_UNUSED *p_hwfn, u16 OSAL_UNUSED vfid, struct ecore_mcp_link_params OSAL_UNUSED *params, struct ecore_mcp_link_state OSAL_UNUSED *link, struct ecore_mcp_link_capabilities OSAL_UNUSED *p_caps) {}
static OSAL_INLINE bool ecore_iov_is_vf_pending_flr(struct ecore_hwfn OSAL_UNUSED *p_hwfn, u16 OSAL_UNUSED rel_vf_id) {return false;}
#endif
static OSAL_INLINE bool
ecore_iov_is_valid_vfid(struct ecore_hwfn OSAL_UNUSED *p_hwfn, int OSAL_UNUSED rel_vf_id,
			bool OSAL_UNUSED b_enabled_only,
			bool OSAL_UNUSED b_non_malicious)
{
	return false;
}
#ifndef LINUX_REMOVE
static OSAL_INLINE struct ecore_public_vf_info* ecore_iov_get_public_vf_info(struct ecore_hwfn OSAL_UNUSED *p_hwfn, u16 OSAL_UNUSED vfid, bool OSAL_UNUSED b_enabled_only) {return OSAL_NULL;}
static OSAL_INLINE void ecore_iov_pf_add_pending_events(struct ecore_hwfn OSAL_UNUSED *p_hwfn, u8 OSAL_UNUSED vfid) {}
static OSAL_INLINE void ecore_iov_pf_get_and_clear_pending_events(struct ecore_hwfn OSAL_UNUSED *p_hwfn, u64 OSAL_UNUSED *events) {}
static OSAL_INLINE enum _ecore_status_t ecore_iov_copy_vf_msg(struct ecore_hwfn OSAL_UNUSED *p_hwfn, struct ecore_ptt OSAL_UNUSED *ptt, int OSAL_UNUSED vfid) {return ECORE_INVAL;}
static OSAL_INLINE void ecore_iov_bulletin_set_forced_mac(struct ecore_hwfn OSAL_UNUSED *p_hwfn, u8 OSAL_UNUSED *mac, int OSAL_UNUSED vfid) {}
static OSAL_INLINE enum _ecore_status_t ecore_iov_bulletin_set_mac(struct ecore_hwfn OSAL_UNUSED *p_hwfn, u8 OSAL_UNUSED *mac, OSAL_UNUSED int vfid) {return ECORE_INVAL;}
static OSAL_INLINE enum _ecore_status_t ecore_iov_bulletin_set_forced_untagged_default(struct ecore_hwfn OSAL_UNUSED *p_hwfn, bool OSAL_UNUSED b_untagged_only, int OSAL_UNUSED vfid) {return ECORE_INVAL;}
static OSAL_INLINE void ecore_iov_get_vfs_opaque_fid(struct ecore_hwfn OSAL_UNUSED *p_hwfn, int OSAL_UNUSED vfid, u16 OSAL_UNUSED *opaque_fid) {}
static OSAL_INLINE void ecore_iov_bulletin_set_forced_vlan(struct ecore_hwfn OSAL_UNUSED p_hwfn, u16 OSAL_UNUSED pvid, int OSAL_UNUSED vfid) {}

static OSAL_INLINE bool ecore_iov_vf_has_vport_instance(struct ecore_hwfn OSAL_UNUSED *p_hwfn, int OSAL_UNUSED vfid) {return false;}
static OSAL_INLINE enum _ecore_status_t ecore_iov_post_vf_bulletin(struct ecore_hwfn OSAL_UNUSED *p_hwfn, int OSAL_UNUSED vfid, struct ecore_ptt OSAL_UNUSED *p_ptt) {return ECORE_INVAL;}
static OSAL_INLINE bool ecore_iov_is_vf_stopped(struct ecore_hwfn OSAL_UNUSED *p_hwfn, int OSAL_UNUSED vfid) {return false;}
static OSAL_INLINE enum _ecore_status_t ecore_iov_spoofchk_set(struct ecore_hwfn OSAL_UNUSED *p_hwfn, int OSAL_UNUSED vfid, bool OSAL_UNUSED val) {return ECORE_INVAL;}
static OSAL_INLINE bool ecore_iov_spoofchk_get(struct ecore_hwfn OSAL_UNUSED *p_hwfn, int OSAL_UNUSED vfid) {return false;}
static OSAL_INLINE bool ecore_iov_pf_sanity_check(struct ecore_hwfn OSAL_UNUSED *p_hwfn, int OSAL_UNUSED vfid) {return false;}
static OSAL_INLINE u8 ecore_iov_vf_chains_per_pf(struct ecore_hwfn OSAL_UNUSED *p_hwfn) {return 0;}
static OSAL_INLINE void ecore_iov_get_vf_req_virt_mbx_params(struct ecore_hwfn OSAL_UNUSED *p_hwfn, u16 OSAL_UNUSED rel_vf_id, void OSAL_UNUSED **pp_req_virt_addr, u16 OSAL_UNUSED *p_req_virt_size) {}
static OSAL_INLINE void ecore_iov_get_vf_reply_virt_mbx_params(struct ecore_hwfn OSAL_UNUSED *p_hwfn, u16 OSAL_UNUSED rel_vf_id, void OSAL_UNUSED **pp_reply_virt_addr, u16 OSAL_UNUSED *p_reply_virt_size) {}
static OSAL_INLINE bool ecore_iov_is_valid_vfpf_msg_length(u32 OSAL_UNUSED length) {return false;}
static OSAL_INLINE u32 ecore_iov_pfvf_msg_length(void) {return 0;}
static OSAL_INLINE u8 *ecore_iov_bulletin_get_forced_mac(struct ecore_hwfn OSAL_UNUSED *p_hwfn, u16 OSAL_UNUSED rel_vf_id) {return OSAL_NULL;}
static OSAL_INLINE u16 ecore_iov_bulletin_get_forced_vlan(struct ecore_hwfn OSAL_UNUSED *p_hwfn, u16 OSAL_UNUSED rel_vf_id) {return 0;}
static OSAL_INLINE enum _ecore_status_t ecore_iov_configure_tx_rate(struct ecore_hwfn OSAL_UNUSED *p_hwfn, struct ecore_ptt OSAL_UNUSED *p_ptt, int OSAL_UNUSED vfid, int OSAL_UNUSED val) { return ECORE_INVAL; }
static OSAL_INLINE enum _ecore_status_t ecore_iov_get_vf_stats(struct ecore_hwfn OSAL_UNUSED *p_hwfn, struct ecore_ptt OSAL_UNUSED *p_ptt, int OSAL_UNUSED vfid, struct ecore_eth_stats OSAL_UNUSED *p_stats) { return ECORE_INVAL; }

static OSAL_INLINE u8 ecore_iov_get_vf_num_rxqs(struct ecore_hwfn OSAL_UNUSED *p_hwfn, u16 OSAL_UNUSED rel_vf_id) {return 0;}
static OSAL_INLINE u8 ecore_iov_get_vf_num_active_rxqs(struct ecore_hwfn OSAL_UNUSED *p_hwfn, u16 OSAL_UNUSED rel_vf_id) {return 0;}
static OSAL_INLINE void *ecore_iov_get_vf_ctx(struct ecore_hwfn OSAL_UNUSED *p_hwfn, u16 OSAL_UNUSED rel_vf_id) {return OSAL_NULL;}
static OSAL_INLINE u8 ecore_iov_get_vf_num_sbs(struct ecore_hwfn OSAL_UNUSED *p_hwfn, u16 OSAL_UNUSED rel_vf_id) {return 0;}
static OSAL_INLINE bool ecore_iov_is_vf_wait_for_acquire(struct ecore_hwfn OSAL_UNUSED *p_hwfn, u16 OSAL_UNUSED rel_vf_id) {return false;}
static OSAL_INLINE bool ecore_iov_is_vf_acquired_not_initialized(struct ecore_hwfn OSAL_UNUSED *p_hwfn, u16 OSAL_UNUSED rel_vf_id) {return false;}
static OSAL_INLINE bool ecore_iov_is_vf_initialized(struct ecore_hwfn OSAL_UNUSED *p_hwfn, u16 OSAL_UNUSED rel_vf_id) {return false;}
static OSAL_INLINE int ecore_iov_get_vf_min_rate(struct ecore_hwfn OSAL_UNUSED *p_hwfn, int OSAL_UNUSED vfid) { return 0; }
static OSAL_INLINE enum _ecore_status_t ecore_iov_configure_min_tx_rate(struct ecore_dev OSAL_UNUSED *p_dev, int OSAL_UNUSED vfid, OSAL_UNUSED u32 rate) { return ECORE_INVAL; }
#endif
static OSAL_INLINE void ecore_iov_bulletin_set_udp_ports(struct ecore_hwfn OSAL_UNUSED *p_hwfn, int OSAL_UNUSED vfid, u16 OSAL_UNUSED vxlan_port, u16 OSAL_UNUSED geneve_port) { return; }
static OSAL_INLINE u16 ecore_iov_get_next_active_vf(struct ecore_hwfn OSAL_UNUSED *p_hwfn, u16 OSAL_UNUSED rel_vf_id) { return MAX_NUM_VFS_E4; }

#ifdef CONFIG_ECORE_SW_CHANNEL
static OSAL_INLINE void
ecore_iov_set_vf_hw_channel(struct ecore_hwfn OSAL_UNUSED *p_hwfn,
			    int OSAL_UNUSED vfid, bool OSAL_UNUSED b_is_hw) {}
#endif
#endif

#define ecore_for_each_vf(_p_hwfn, _i)					\
	for (_i = ecore_iov_get_next_active_vf(_p_hwfn, 0);		\
	     _i < MAX_NUM_VFS_E4;					\
	     _i = ecore_iov_get_next_active_vf(_p_hwfn, _i + 1))

#endif
