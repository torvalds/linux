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

#ifndef __ECORE_VF_API_H__
#define __ECORE_VF_API_H__

#include "ecore_sp_api.h"
#include "ecore_mcp_api.h"

#ifdef CONFIG_ECORE_SRIOV
/**
 * @brief Read the VF bulletin and act on it if needed
 *
 * @param p_hwfn
 * @param p_change - ecore fills 1 iff bulletin board has changed, 0 otherwise.
 *
 * @return enum _ecore_status
 */
enum _ecore_status_t ecore_vf_read_bulletin(struct ecore_hwfn *p_hwfn,
					    u8 *p_change);

/**
 * @brief Get link paramters for VF from ecore
 *
 * @param p_hwfn
 * @param params - the link params structure to be filled for the VF
 */
void ecore_vf_get_link_params(struct ecore_hwfn *p_hwfn,
			      struct ecore_mcp_link_params *params);

/**
 * @brief Get link state for VF from ecore
 *
 * @param p_hwfn
 * @param link - the link state structure to be filled for the VF
 */
void ecore_vf_get_link_state(struct ecore_hwfn *p_hwfn,
			     struct ecore_mcp_link_state *link);

/**
 * @brief Get link capabilities for VF from ecore
 *
 * @param p_hwfn
 * @param p_link_caps - the link capabilities structure to be filled for the VF
 */
void ecore_vf_get_link_caps(struct ecore_hwfn *p_hwfn,
			    struct ecore_mcp_link_capabilities *p_link_caps);

/**
 * @brief Get number of Rx queues allocated for VF by ecore
 *
 *  @param p_hwfn
 *  @param num_rxqs - allocated RX queues
 */
void ecore_vf_get_num_rxqs(struct ecore_hwfn *p_hwfn,
			   u8 *num_rxqs);

/**
 * @brief Get number of Rx queues allocated for VF by ecore
 *
 *  @param p_hwfn
 *  @param num_txqs - allocated RX queues
 */
void ecore_vf_get_num_txqs(struct ecore_hwfn *p_hwfn,
			   u8 *num_txqs);

/**
 * @brief Get number of available connections [both Rx and Tx] for VF
 *
 * @param p_hwfn
 * @param num_cids - allocated number of connections
 */
void ecore_vf_get_num_cids(struct ecore_hwfn *p_hwfn, u8 *num_cids);

/**
 * @brief Get port mac address for VF
 *
 * @param p_hwfn
 * @param port_mac - destination location for port mac
 */
void ecore_vf_get_port_mac(struct ecore_hwfn *p_hwfn,
			   u8 *port_mac);

/**
 * @brief Get number of VLAN filters allocated for VF by ecore
 *
 *  @param p_hwfn
 *  @param num_rxqs - allocated VLAN filters
 */
void ecore_vf_get_num_vlan_filters(struct ecore_hwfn *p_hwfn,
				   u8 *num_vlan_filters);

/**
 * @brief Get number of MAC filters allocated for VF by ecore
 *
 *  @param p_hwfn
 *  @param num_rxqs - allocated MAC filters
 */
void ecore_vf_get_num_mac_filters(struct ecore_hwfn *p_hwfn,
				  u8 *num_mac_filters);

/**
 * @brief Check if VF can set a MAC address
 *
 * @param p_hwfn
 * @param mac
 *
 * @return bool
 */
bool ecore_vf_check_mac(struct ecore_hwfn *p_hwfn, u8 *mac);

#ifndef LINUX_REMOVE
/**
 * @brief Copy forced MAC address from bulletin board
 *
 * @param hwfn
 * @param dst_mac
 * @param p_is_forced - out param which indicate in case mac
 *      	        exist if it forced or not.
 *  
 * @return bool       - return true if mac exist and false if
 *                      not.
 */
bool ecore_vf_bulletin_get_forced_mac(struct ecore_hwfn *hwfn, u8 *dst_mac,
				      u8 *p_is_forced);

/**
 * @brief Check if force vlan is set and copy the forced vlan
 *        from bulletin board
 *
 * @param hwfn
 * @param dst_pvid
 * @return bool
 */
bool ecore_vf_bulletin_get_forced_vlan(struct ecore_hwfn *hwfn, u16 *dst_pvid);

/**
 * @brief Check if VF is based on PF whose driver is pre-fp-hsi version;
 *        This affects the fastpath implementation of the driver.
 *
 * @param p_hwfn
 *
 * @return bool - true iff PF is pre-fp-hsi version.
 */
bool ecore_vf_get_pre_fp_hsi(struct ecore_hwfn *p_hwfn);

#endif

/**
 * @brief Set firmware version information in dev_info from VFs acquire response tlv
 *
 * @param p_hwfn
 * @param fw_major
 * @param fw_minor
 * @param fw_rev
 * @param fw_eng
 */
void ecore_vf_get_fw_version(struct ecore_hwfn *p_hwfn,
			     u16 *fw_major,
			     u16 *fw_minor,
			     u16 *fw_rev,
			     u16 *fw_eng);
void ecore_vf_bulletin_get_udp_ports(struct ecore_hwfn *p_hwfn,
				     u16 *p_vxlan_port, u16 *p_geneve_port);

#ifdef CONFIG_ECORE_SW_CHANNEL
/**
 * @brief set the VF to use a SW/HW channel when communicating with PF.
 *        NOTICE: today the likely first place to call this from VF
 *        would be OSAL_VF_FILL_ACQUIRE_RESC_REQ(); Might want to consider
 *        something a bit more appropriate.
 *
 * @param p_hwfn
 * @param b_is_hw - true iff VF is to use a HW-channel
 */
void ecore_vf_set_hw_channel(struct ecore_hwfn *p_hwfn, bool b_is_hw);
#endif
#else
static OSAL_INLINE enum _ecore_status_t ecore_vf_read_bulletin(struct ecore_hwfn OSAL_UNUSED *p_hwfn, u8 OSAL_UNUSED *p_change) {return ECORE_INVAL;}
static OSAL_INLINE void ecore_vf_get_link_params(struct ecore_hwfn OSAL_UNUSED *p_hwfn, struct ecore_mcp_link_params OSAL_UNUSED *params) {}
static OSAL_INLINE void ecore_vf_get_link_state(struct ecore_hwfn OSAL_UNUSED *p_hwfn, struct ecore_mcp_link_state OSAL_UNUSED *link) {}
static OSAL_INLINE void ecore_vf_get_link_caps(struct ecore_hwfn OSAL_UNUSED *p_hwfn, struct ecore_mcp_link_capabilities OSAL_UNUSED *p_link_caps) {}
static OSAL_INLINE void ecore_vf_get_num_rxqs(struct ecore_hwfn OSAL_UNUSED *p_hwfn, u8 OSAL_UNUSED *num_rxqs) {}
static OSAL_INLINE void ecore_vf_get_num_txqs(struct ecore_hwfn OSAL_UNUSED *p_hwfn, u8 OSAL_UNUSED *num_txqs) {}

static OSAL_INLINE void
ecore_vf_get_num_cids(struct ecore_hwfn OSAL_UNUSED *p_hwfn,
		      u8 OSAL_UNUSED *num_cids)
{
}

static OSAL_INLINE void ecore_vf_get_port_mac(struct ecore_hwfn OSAL_UNUSED *p_hwfn, u8 OSAL_UNUSED *port_mac) {}
static OSAL_INLINE void ecore_vf_get_num_vlan_filters(struct ecore_hwfn OSAL_UNUSED *p_hwfn, u8 OSAL_UNUSED *num_vlan_filters) {}
static OSAL_INLINE void ecore_vf_get_num_mac_filters(struct ecore_hwfn OSAL_UNUSED *p_hwfn, u8 OSAL_UNUSED *num_mac_filters) {}
static OSAL_INLINE bool ecore_vf_check_mac(struct ecore_hwfn OSAL_UNUSED *p_hwfn, u8 OSAL_UNUSED *mac) {return false;}
#ifndef LINUX_REMOVE
static OSAL_INLINE bool ecore_vf_bulletin_get_forced_mac(struct ecore_hwfn OSAL_UNUSED *hwfn, u8 OSAL_UNUSED *dst_mac, u8 OSAL_UNUSED *p_is_forced) {return false;}
static OSAL_INLINE bool ecore_vf_get_pre_fp_hsi(struct ecore_hwfn OSAL_UNUSED *p_hwfn) {return false; }
#endif
static OSAL_INLINE void ecore_vf_get_fw_version(struct ecore_hwfn OSAL_UNUSED *p_hwfn, u16 OSAL_UNUSED *fw_major, u16 OSAL_UNUSED *fw_minor, u16 OSAL_UNUSED *fw_rev, u16 OSAL_UNUSED *fw_eng) {}
static OSAL_INLINE void ecore_vf_bulletin_get_udp_ports(struct ecore_hwfn OSAL_UNUSED *p_hwfn, u16 OSAL_UNUSED *p_vxlan_port, u16 OSAL_UNUSED *p_geneve_port) { return; }

#ifdef CONFIG_ECORE_SW_CHANNEL
static OSAL_INLINE void
ecore_vf_set_hw_channel(struct ecore_hwfn OSAL_UNUSED *p_hwfn,
			bool OSAL_UNUSED b_is_hw) {}
#endif
#endif
#endif
