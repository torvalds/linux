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
 */

/*
 * File : ecore_sp_commands.c
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "bcm_osal.h"

#include "ecore.h"
#include "ecore_status.h"
#include "ecore_chain.h"
#include "ecore_spq.h"
#include "ecore_init_fw_funcs.h"
#include "ecore_cxt.h"
#include "ecore_sp_commands.h"
#include "ecore_gtt_reg_addr.h"
#include "ecore_iro.h"
#include "reg_addr.h"
#include "ecore_int.h"
#include "ecore_hw.h"
#include "ecore_dcbx.h"
#include "ecore_sriov.h"
#include "ecore_vf.h"
#ifndef LINUX_REMOVE
#include "ecore_tcp_ip.h"
#endif

enum _ecore_status_t ecore_sp_init_request(struct ecore_hwfn *p_hwfn,
					   struct ecore_spq_entry **pp_ent,
					   u8 cmd,
					   u8 protocol,
					   struct ecore_sp_init_data *p_data)
{
	u32 opaque_cid = p_data->opaque_fid << 16 | p_data->cid;
	struct ecore_spq_entry *p_ent = OSAL_NULL;
	enum _ecore_status_t rc;

	if (!pp_ent)
		return ECORE_INVAL;

	/* Get an SPQ entry */
	rc = ecore_spq_get_entry(p_hwfn, pp_ent);
	if (rc != ECORE_SUCCESS)
		return rc;

	/* Fill the SPQ entry */
	p_ent = *pp_ent;
	p_ent->elem.hdr.cid = OSAL_CPU_TO_LE32(opaque_cid);
	p_ent->elem.hdr.cmd_id = cmd;
	p_ent->elem.hdr.protocol_id = protocol;
	p_ent->priority = ECORE_SPQ_PRIORITY_NORMAL;
	p_ent->comp_mode = p_data->comp_mode;
	p_ent->comp_done.done = 0;

	switch (p_ent->comp_mode) {
	case ECORE_SPQ_MODE_EBLOCK:
		p_ent->comp_cb.cookie = &p_ent->comp_done;
		break;

	case ECORE_SPQ_MODE_BLOCK:
		if (!p_data->p_comp_data)
			return ECORE_INVAL;

		p_ent->comp_cb.cookie = p_data->p_comp_data->cookie;
		break;

	case ECORE_SPQ_MODE_CB:
		if (!p_data->p_comp_data)
			p_ent->comp_cb.function = OSAL_NULL;
		else
			p_ent->comp_cb = *p_data->p_comp_data;
		break;

	default:
		DP_NOTICE(p_hwfn, true, "Unknown SPQE completion mode %d\n",
			  p_ent->comp_mode);
		return ECORE_INVAL;
	}

	DP_VERBOSE(p_hwfn, ECORE_MSG_SPQ,
		   "Initialized: CID %08x cmd %02x protocol %02x data_addr %llx comp_mode [%s]\n",
		   opaque_cid, cmd, protocol,
		   (unsigned long long)(osal_uintptr_t)&p_ent->ramrod,
		   D_TRINE(p_ent->comp_mode, ECORE_SPQ_MODE_EBLOCK,
			   ECORE_SPQ_MODE_BLOCK, "MODE_EBLOCK", "MODE_BLOCK",
			   "MODE_CB"));

	OSAL_MEMSET(&p_ent->ramrod, 0, sizeof(p_ent->ramrod));

	return ECORE_SUCCESS;
}

static enum tunnel_clss ecore_tunn_clss_to_fw_clss(u8 type)
{
	switch (type) {
	case ECORE_TUNN_CLSS_MAC_VLAN:
		return TUNNEL_CLSS_MAC_VLAN;
	case ECORE_TUNN_CLSS_MAC_VNI:
		return TUNNEL_CLSS_MAC_VNI;
	case ECORE_TUNN_CLSS_INNER_MAC_VLAN:
		return TUNNEL_CLSS_INNER_MAC_VLAN;
	case ECORE_TUNN_CLSS_INNER_MAC_VNI:
		return TUNNEL_CLSS_INNER_MAC_VNI;
	case ECORE_TUNN_CLSS_MAC_VLAN_DUAL_STAGE:
		return TUNNEL_CLSS_MAC_VLAN_DUAL_STAGE;
	default:
		return TUNNEL_CLSS_MAC_VLAN;
	}
}

static void
ecore_set_pf_update_tunn_mode(struct ecore_tunnel_info *p_tun,
			      struct ecore_tunnel_info *p_src,
			      bool b_pf_start)
{
	if (p_src->vxlan.b_update_mode || b_pf_start)
		p_tun->vxlan.b_mode_enabled = p_src->vxlan.b_mode_enabled;

	if (p_src->l2_gre.b_update_mode || b_pf_start)
		p_tun->l2_gre.b_mode_enabled = p_src->l2_gre.b_mode_enabled;

	if (p_src->ip_gre.b_update_mode || b_pf_start)
		p_tun->ip_gre.b_mode_enabled = p_src->ip_gre.b_mode_enabled;

	if (p_src->l2_geneve.b_update_mode || b_pf_start)
		p_tun->l2_geneve.b_mode_enabled =
				p_src->l2_geneve.b_mode_enabled;

	if (p_src->ip_geneve.b_update_mode || b_pf_start)
		p_tun->ip_geneve.b_mode_enabled =
				p_src->ip_geneve.b_mode_enabled;
}

static void ecore_set_tunn_cls_info(struct ecore_tunnel_info *p_tun,
				    struct ecore_tunnel_info *p_src)
{
	enum tunnel_clss type;

	p_tun->b_update_rx_cls = p_src->b_update_rx_cls;
	p_tun->b_update_tx_cls = p_src->b_update_tx_cls;

	type = ecore_tunn_clss_to_fw_clss(p_src->vxlan.tun_cls);
	p_tun->vxlan.tun_cls = (enum ecore_tunn_clss)type;
	type = ecore_tunn_clss_to_fw_clss(p_src->l2_gre.tun_cls);
	p_tun->l2_gre.tun_cls = (enum ecore_tunn_clss)type;
	type = ecore_tunn_clss_to_fw_clss(p_src->ip_gre.tun_cls);
	p_tun->ip_gre.tun_cls = (enum ecore_tunn_clss)type;
	type = ecore_tunn_clss_to_fw_clss(p_src->l2_geneve.tun_cls);
	p_tun->l2_geneve.tun_cls = (enum ecore_tunn_clss)type;
	type = ecore_tunn_clss_to_fw_clss(p_src->ip_geneve.tun_cls);
	p_tun->ip_geneve.tun_cls = (enum ecore_tunn_clss)type;
}

static void ecore_set_tunn_ports(struct ecore_tunnel_info *p_tun,
				 struct ecore_tunnel_info *p_src)
{
	p_tun->geneve_port.b_update_port = p_src->geneve_port.b_update_port;
	p_tun->vxlan_port.b_update_port = p_src->vxlan_port.b_update_port;

	if (p_src->geneve_port.b_update_port)
		p_tun->geneve_port.port = p_src->geneve_port.port;

	if (p_src->vxlan_port.b_update_port)
		p_tun->vxlan_port.port = p_src->vxlan_port.port;
}

static void
__ecore_set_ramrod_tunnel_param(u8 *p_tunn_cls,
				struct ecore_tunn_update_type *tun_type)
{
	*p_tunn_cls = tun_type->tun_cls;
}

static void
ecore_set_ramrod_tunnel_param(u8 *p_tunn_cls,
			      struct ecore_tunn_update_type *tun_type,
			      u8 *p_update_port, __le16 *p_port,
			      struct ecore_tunn_update_udp_port *p_udp_port)
{
	__ecore_set_ramrod_tunnel_param(p_tunn_cls, tun_type);
	if (p_udp_port->b_update_port) {
		*p_update_port = 1;
		*p_port = OSAL_CPU_TO_LE16(p_udp_port->port);
	}
}

static void
ecore_tunn_set_pf_update_params(struct ecore_hwfn		*p_hwfn,
				struct ecore_tunnel_info *p_src,
				struct pf_update_tunnel_config	*p_tunn_cfg)
{
	struct ecore_tunnel_info *p_tun = &p_hwfn->p_dev->tunnel;

	ecore_set_pf_update_tunn_mode(p_tun, p_src, false);
	ecore_set_tunn_cls_info(p_tun, p_src);
	ecore_set_tunn_ports(p_tun, p_src);

	ecore_set_ramrod_tunnel_param(&p_tunn_cfg->tunnel_clss_vxlan,
				      &p_tun->vxlan,
				      &p_tunn_cfg->set_vxlan_udp_port_flg,
				      &p_tunn_cfg->vxlan_udp_port,
				      &p_tun->vxlan_port);

	ecore_set_ramrod_tunnel_param(&p_tunn_cfg->tunnel_clss_l2geneve,
				      &p_tun->l2_geneve,
				      &p_tunn_cfg->set_geneve_udp_port_flg,
				      &p_tunn_cfg->geneve_udp_port,
				      &p_tun->geneve_port);

	__ecore_set_ramrod_tunnel_param(&p_tunn_cfg->tunnel_clss_ipgeneve,
					&p_tun->ip_geneve);

	__ecore_set_ramrod_tunnel_param(&p_tunn_cfg->tunnel_clss_l2gre,
					&p_tun->l2_gre);

	__ecore_set_ramrod_tunnel_param(&p_tunn_cfg->tunnel_clss_ipgre,
					&p_tun->ip_gre);

	p_tunn_cfg->update_rx_pf_clss = p_tun->b_update_rx_cls;
}

static void ecore_set_hw_tunn_mode(struct ecore_hwfn *p_hwfn,
				   struct ecore_ptt  *p_ptt,
				   struct ecore_tunnel_info *p_tun)
{
	ecore_set_gre_enable(p_hwfn, p_ptt, p_tun->l2_gre.b_mode_enabled,
			     p_tun->ip_gre.b_mode_enabled);
	ecore_set_vxlan_enable(p_hwfn, p_ptt, p_tun->vxlan.b_mode_enabled);

	ecore_set_geneve_enable(p_hwfn, p_ptt, p_tun->l2_geneve.b_mode_enabled,
				p_tun->ip_geneve.b_mode_enabled);
}

static void ecore_set_hw_tunn_mode_port(struct ecore_hwfn *p_hwfn,
					struct ecore_ptt  *p_ptt,
					struct ecore_tunnel_info *p_tunn)
{
	if (ECORE_IS_BB_A0(p_hwfn->p_dev)) {
		DP_NOTICE(p_hwfn, true,
			  "A0 chip: tunnel hw config is not supported\n");
		return;
	}

	if (p_tunn->vxlan_port.b_update_port)
		ecore_set_vxlan_dest_port(p_hwfn, p_ptt,
					  p_tunn->vxlan_port.port);

	if (p_tunn->geneve_port.b_update_port)
		ecore_set_geneve_dest_port(p_hwfn, p_ptt,
					   p_tunn->geneve_port.port);

	ecore_set_hw_tunn_mode(p_hwfn, p_ptt, p_tunn);
}

static void
ecore_tunn_set_pf_start_params(struct ecore_hwfn		*p_hwfn,
			       struct ecore_tunnel_info		*p_src,
			       struct pf_start_tunnel_config	*p_tunn_cfg)
{
	struct ecore_tunnel_info *p_tun = &p_hwfn->p_dev->tunnel;

	if (ECORE_IS_BB_A0(p_hwfn->p_dev)) {
		DP_NOTICE(p_hwfn, true,
			  "A0 chip: tunnel pf start config is not supported\n");
		return;
	}

	if (!p_src)
		return;

	ecore_set_pf_update_tunn_mode(p_tun, p_src, true);
	ecore_set_tunn_cls_info(p_tun, p_src);
	ecore_set_tunn_ports(p_tun, p_src);

	ecore_set_ramrod_tunnel_param(&p_tunn_cfg->tunnel_clss_vxlan,
				      &p_tun->vxlan,
				      &p_tunn_cfg->set_vxlan_udp_port_flg,
				      &p_tunn_cfg->vxlan_udp_port,
				      &p_tun->vxlan_port);

	ecore_set_ramrod_tunnel_param(&p_tunn_cfg->tunnel_clss_l2geneve,
				      &p_tun->l2_geneve,
				      &p_tunn_cfg->set_geneve_udp_port_flg,
				      &p_tunn_cfg->geneve_udp_port,
				      &p_tun->geneve_port);

	__ecore_set_ramrod_tunnel_param(&p_tunn_cfg->tunnel_clss_ipgeneve,
					&p_tun->ip_geneve);

	__ecore_set_ramrod_tunnel_param(&p_tunn_cfg->tunnel_clss_l2gre,
					&p_tun->l2_gre);

	__ecore_set_ramrod_tunnel_param(&p_tunn_cfg->tunnel_clss_ipgre,
					&p_tun->ip_gre);
}

enum _ecore_status_t ecore_sp_pf_start(struct ecore_hwfn *p_hwfn,
				       struct ecore_ptt *p_ptt,
				       struct ecore_tunnel_info *p_tunn,
				       bool allow_npar_tx_switch)
{
	struct pf_start_ramrod_data *p_ramrod = OSAL_NULL;
	u16 sb = ecore_int_get_sp_sb_id(p_hwfn);
	u8 sb_index = p_hwfn->p_eq->eq_sb_index;
	struct ecore_spq_entry *p_ent = OSAL_NULL;
	struct ecore_sp_init_data init_data;
	enum _ecore_status_t rc = ECORE_NOTIMPL;
	u8 page_cnt;
	u8 i;

	/* update initial eq producer */
	ecore_eq_prod_update(p_hwfn,
			     ecore_chain_get_prod_idx(&p_hwfn->p_eq->chain));

	/* Initialize the SPQ entry for the ramrod */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = ecore_spq_get_cid(p_hwfn);
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;

	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   COMMON_RAMROD_PF_START,
				   PROTOCOLID_COMMON,
				   &init_data);
	if (rc != ECORE_SUCCESS)
		return rc;

	/* Fill the ramrod data */
	p_ramrod = &p_ent->ramrod.pf_start;
	p_ramrod->event_ring_sb_id = OSAL_CPU_TO_LE16(sb);
	p_ramrod->event_ring_sb_index = sb_index;
	p_ramrod->path_id = ECORE_PATH_ID(p_hwfn);

	/* For easier debugging */
	p_ramrod->dont_log_ramrods = 0;
	p_ramrod->log_type_mask = OSAL_CPU_TO_LE16(0x8f);

	if (OSAL_TEST_BIT(ECORE_MF_OVLAN_CLSS, &p_hwfn->p_dev->mf_bits))
		p_ramrod->mf_mode = MF_OVLAN;
	else
		p_ramrod->mf_mode = MF_NPAR;

	p_ramrod->outer_tag_config.outer_tag.tci =
		OSAL_CPU_TO_LE16(p_hwfn->hw_info.ovlan);
	if (OSAL_TEST_BIT(ECORE_MF_8021Q_TAGGING, &p_hwfn->p_dev->mf_bits))
		p_ramrod->outer_tag_config.outer_tag.tpid = ETH_P_8021Q;
	else if (OSAL_TEST_BIT(ECORE_MF_8021AD_TAGGING,
		 &p_hwfn->p_dev->mf_bits)) {
		p_ramrod->outer_tag_config.outer_tag.tpid = ETH_P_8021AD;
		p_ramrod->outer_tag_config.enable_stag_pri_change = 1;
	}

	p_ramrod->outer_tag_config.pri_map_valid = 1;
	for (i = 0; i < ECORE_MAX_PFC_PRIORITIES; i++)
		p_ramrod->outer_tag_config.inner_to_outer_pri_map[i] = i;

	/* enable_stag_pri_change should be set if port is in BD mode or,
	 * UFP with Host Control mode or, UFP with DCB over base interface.
	 */
	if (OSAL_TEST_BIT(ECORE_MF_UFP_SPECIFIC, &p_hwfn->p_dev->mf_bits)) {
		if ((p_hwfn->ufp_info.pri_type == ECORE_UFP_PRI_OS) ||
		    (p_hwfn->p_dcbx_info->results.dcbx_enabled))
			p_ramrod->outer_tag_config.enable_stag_pri_change = 1;
		else
			p_ramrod->outer_tag_config.enable_stag_pri_change = 0;
	}

	/* Place EQ address in RAMROD */
	DMA_REGPAIR_LE(p_ramrod->event_ring_pbl_addr,
		       p_hwfn->p_eq->chain.pbl_sp.p_phys_table);
	page_cnt = (u8)ecore_chain_get_page_cnt(&p_hwfn->p_eq->chain);
	p_ramrod->event_ring_num_pages = page_cnt;
	DMA_REGPAIR_LE(p_ramrod->consolid_q_pbl_addr,
		       p_hwfn->p_consq->chain.pbl_sp.p_phys_table);

	ecore_tunn_set_pf_start_params(p_hwfn, p_tunn,
				       &p_ramrod->tunnel_config);

	if (OSAL_TEST_BIT(ECORE_MF_INTER_PF_SWITCH,
			  &p_hwfn->p_dev->mf_bits))
		p_ramrod->allow_npar_tx_switching = allow_npar_tx_switch;

	switch (p_hwfn->hw_info.personality) {
	case ECORE_PCI_ETH:
		p_ramrod->personality = PERSONALITY_ETH;
		break;
	case ECORE_PCI_FCOE:
		p_ramrod->personality = PERSONALITY_FCOE;
		break;
	case ECORE_PCI_ISCSI:
		p_ramrod->personality = PERSONALITY_ISCSI;
		break;
	case ECORE_PCI_ETH_IWARP:
	case ECORE_PCI_ETH_ROCE:
	case ECORE_PCI_ETH_RDMA:
		p_ramrod->personality = PERSONALITY_RDMA_AND_ETH;
		break;
	default:
		DP_NOTICE(p_hwfn, true, "Unknown personality %d\n",
			  p_hwfn->hw_info.personality);
		p_ramrod->personality = PERSONALITY_ETH;
	}

	if (p_hwfn->p_dev->p_iov_info) {
		struct ecore_hw_sriov_info *p_iov = p_hwfn->p_dev->p_iov_info;

		p_ramrod->base_vf_id = (u8)p_iov->first_vf_in_pf;
		p_ramrod->num_vfs = (u8)p_iov->total_vfs;
	}
	/* @@@TBD - update also the "ROCE_VER_KEY" entries when the FW RoCE HSI
	 * version is available.
	 */
	p_ramrod->hsi_fp_ver.major_ver_arr[ETH_VER_KEY] = ETH_HSI_VER_MAJOR;
	p_ramrod->hsi_fp_ver.minor_ver_arr[ETH_VER_KEY] = ETH_HSI_VER_MINOR;

	DP_VERBOSE(p_hwfn, ECORE_MSG_SPQ,
		   "Setting event_ring_sb [id %04x index %02x], outer_tag.tpid [%d], outer_tag.tci [%d]\n",
		   sb, sb_index, p_ramrod->outer_tag_config.outer_tag.tpid,
		   p_ramrod->outer_tag_config.outer_tag.tci);

	rc = ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);

	if (p_tunn)
		ecore_set_hw_tunn_mode_port(p_hwfn, p_ptt,
					    &p_hwfn->p_dev->tunnel);

	return rc;
}

enum _ecore_status_t ecore_sp_pf_update_dcbx(struct ecore_hwfn *p_hwfn)
{
	struct ecore_spq_entry *p_ent = OSAL_NULL;
	struct ecore_sp_init_data init_data;
	enum _ecore_status_t rc = ECORE_NOTIMPL;

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = ecore_spq_get_cid(p_hwfn);
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_CB;

	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   COMMON_RAMROD_PF_UPDATE, PROTOCOLID_COMMON,
				   &init_data);
	if (rc != ECORE_SUCCESS)
		return rc;

	ecore_dcbx_set_pf_update_params(&p_hwfn->p_dcbx_info->results,
					&p_ent->ramrod.pf_update);

	return ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);
}

enum _ecore_status_t ecore_sp_pf_update_ufp(struct ecore_hwfn *p_hwfn)
{
	struct ecore_spq_entry *p_ent = OSAL_NULL;
	struct ecore_sp_init_data init_data;
	enum _ecore_status_t rc = ECORE_NOTIMPL;

	if (p_hwfn->ufp_info.pri_type == ECORE_UFP_PRI_UNKNOWN) {
		DP_INFO(p_hwfn, "Invalid priority type %d\n",
			p_hwfn->ufp_info.pri_type);
		return ECORE_INVAL;
	}

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = ecore_spq_get_cid(p_hwfn);
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_CB;

	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   COMMON_RAMROD_PF_UPDATE, PROTOCOLID_COMMON,
				   &init_data);
	if (rc != ECORE_SUCCESS)
		return rc;

	p_ent->ramrod.pf_update.update_enable_stag_pri_change = true;
	if ((p_hwfn->ufp_info.pri_type == ECORE_UFP_PRI_OS) ||
	    (p_hwfn->p_dcbx_info->results.dcbx_enabled))
		p_ent->ramrod.pf_update.enable_stag_pri_change = 1;
	else
		p_ent->ramrod.pf_update.enable_stag_pri_change = 0;

	return ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);
}


/* QM rate limiter resolution is 1.6Mbps */
#define QM_RL_RESOLUTION(mb_val)	((mb_val) * 10 / 16)

/* FW uses 1/64k to express gd */
#define FW_GD_RESOLUTION(gd)		(64 * 1024 / (gd))

static u16 ecore_sp_rl_mb_to_qm(u32 mb_val)
{
	return (u16)OSAL_MIN_T(u32, (u16)(~0U), QM_RL_RESOLUTION(mb_val));
}

static u16 ecore_sp_rl_gd_denom(u32 gd)
{
	return gd ? (u16)OSAL_MIN_T(u32, (u16)(~0U), FW_GD_RESOLUTION(gd)) : 0;
}

enum _ecore_status_t ecore_sp_rl_update(struct ecore_hwfn *p_hwfn,
					struct ecore_rl_update_params *params)
{
	struct ecore_spq_entry *p_ent = OSAL_NULL;
	enum _ecore_status_t rc = ECORE_NOTIMPL;
	struct rl_update_ramrod_data *rl_update;
	struct ecore_sp_init_data init_data;

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = ecore_spq_get_cid(p_hwfn);
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;

	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   COMMON_RAMROD_RL_UPDATE, PROTOCOLID_COMMON,
				   &init_data);
	if (rc != ECORE_SUCCESS)
		return rc;

	rl_update = &p_ent->ramrod.rl_update;

	rl_update->qcn_update_param_flg = params->qcn_update_param_flg;
	rl_update->dcqcn_update_param_flg = params->dcqcn_update_param_flg;
	rl_update->rl_init_flg = params->rl_init_flg;
	rl_update->rl_start_flg = params->rl_start_flg;
	rl_update->rl_stop_flg = params->rl_stop_flg;
	rl_update->rl_id_first = params->rl_id_first;
	rl_update->rl_id_last = params->rl_id_last;
	rl_update->rl_dc_qcn_flg = params->rl_dc_qcn_flg;
	rl_update->rl_bc_rate = OSAL_CPU_TO_LE32(params->rl_bc_rate);
	rl_update->rl_max_rate = OSAL_CPU_TO_LE16(ecore_sp_rl_mb_to_qm(params->rl_max_rate));
	rl_update->rl_r_ai = OSAL_CPU_TO_LE16(ecore_sp_rl_mb_to_qm(params->rl_r_ai));
	rl_update->rl_r_hai = OSAL_CPU_TO_LE16(ecore_sp_rl_mb_to_qm(params->rl_r_hai));
	rl_update->dcqcn_g = OSAL_CPU_TO_LE16(ecore_sp_rl_gd_denom(params->dcqcn_gd));
	rl_update->dcqcn_k_us = OSAL_CPU_TO_LE32(params->dcqcn_k_us);
	rl_update->dcqcn_timeuot_us = OSAL_CPU_TO_LE32(params->dcqcn_timeuot_us);
	rl_update->qcn_timeuot_us = OSAL_CPU_TO_LE32(params->qcn_timeuot_us);

	DP_VERBOSE(p_hwfn, ECORE_MSG_SPQ, "rl_params: qcn_update_param_flg %x, dcqcn_update_param_flg %x, rl_init_flg %x, rl_start_flg %x, rl_stop_flg %x, rl_id_first %x, rl_id_last %x, rl_dc_qcn_flg %x, rl_bc_rate %x, rl_max_rate %x, rl_r_ai %x, rl_r_hai %x, dcqcn_g %x, dcqcn_k_us %x, dcqcn_timeuot_us %x, qcn_timeuot_us %x\n",
		   rl_update->qcn_update_param_flg, rl_update->dcqcn_update_param_flg,
		   rl_update->rl_init_flg, rl_update->rl_start_flg,
		   rl_update->rl_stop_flg, rl_update->rl_id_first,
		   rl_update->rl_id_last, rl_update->rl_dc_qcn_flg,
		   rl_update->rl_bc_rate, rl_update->rl_max_rate,
		   rl_update->rl_r_ai, rl_update->rl_r_hai,
		   rl_update->dcqcn_g, rl_update->dcqcn_k_us,
		   rl_update->dcqcn_timeuot_us, rl_update->qcn_timeuot_us);

	return ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);
}

/* Set pf update ramrod command params */
enum _ecore_status_t
ecore_sp_pf_update_tunn_cfg(struct ecore_hwfn *p_hwfn,
			    struct ecore_ptt *p_ptt,
			    struct ecore_tunnel_info *p_tunn,
			    enum spq_mode comp_mode,
			    struct ecore_spq_comp_cb *p_comp_data)
{
	struct ecore_spq_entry *p_ent = OSAL_NULL;
	struct ecore_sp_init_data init_data;
	enum _ecore_status_t rc = ECORE_NOTIMPL;

	if (IS_VF(p_hwfn->p_dev))
		return ecore_vf_pf_tunnel_param_update(p_hwfn, p_tunn);

	if (ECORE_IS_BB_A0(p_hwfn->p_dev)) {
		DP_NOTICE(p_hwfn, true,
			  "A0 chip: tunnel pf update config is not supported\n");
		return rc;
	}

	if (!p_tunn)
		return ECORE_INVAL;

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = ecore_spq_get_cid(p_hwfn);
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = comp_mode;
	init_data.p_comp_data = p_comp_data;

	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   COMMON_RAMROD_PF_UPDATE, PROTOCOLID_COMMON,
				   &init_data);
	if (rc != ECORE_SUCCESS)
		return rc;

	ecore_tunn_set_pf_update_params(p_hwfn, p_tunn,
					&p_ent->ramrod.pf_update.tunnel_config);

	rc = ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);
	if (rc != ECORE_SUCCESS)
		return rc;

	ecore_set_hw_tunn_mode_port(p_hwfn, p_ptt, &p_hwfn->p_dev->tunnel);

	return rc;
}

enum _ecore_status_t ecore_sp_pf_stop(struct ecore_hwfn *p_hwfn)
{
	struct ecore_spq_entry *p_ent = OSAL_NULL;
	struct ecore_sp_init_data init_data;
	enum _ecore_status_t rc = ECORE_NOTIMPL;

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = ecore_spq_get_cid(p_hwfn);
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;

	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   COMMON_RAMROD_PF_STOP, PROTOCOLID_COMMON,
				   &init_data);
	if (rc != ECORE_SUCCESS)
		return rc;

	return ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);
}

enum _ecore_status_t ecore_sp_heartbeat_ramrod(struct ecore_hwfn *p_hwfn)
{
	struct ecore_spq_entry *p_ent = OSAL_NULL;
	struct ecore_sp_init_data init_data;
	enum _ecore_status_t rc;

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = ecore_spq_get_cid(p_hwfn);
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;

	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   COMMON_RAMROD_EMPTY, PROTOCOLID_COMMON,
				   &init_data);
	if (rc != ECORE_SUCCESS)
		return rc;

	return ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);
}

enum _ecore_status_t ecore_sp_pf_update_stag(struct ecore_hwfn *p_hwfn)
{
	struct ecore_spq_entry *p_ent = OSAL_NULL;
	struct ecore_sp_init_data init_data;
	enum _ecore_status_t rc = ECORE_NOTIMPL;

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = ecore_spq_get_cid(p_hwfn);
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_CB;

	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   COMMON_RAMROD_PF_UPDATE, PROTOCOLID_COMMON,
				   &init_data);
	if (rc != ECORE_SUCCESS)
		return rc;

	p_ent->ramrod.pf_update.update_mf_vlan_flag = true;
	p_ent->ramrod.pf_update.mf_vlan = OSAL_CPU_TO_LE16(p_hwfn->hw_info.ovlan);

	return ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);
}
