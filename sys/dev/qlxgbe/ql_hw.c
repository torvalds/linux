/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013-2016 Qlogic Corporation
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
 * File: ql_hw.c
 * Author : David C Somayajulu, Qlogic Corporation, Aliso Viejo, CA 92656.
 * Content: Contains Hardware dependent functions
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "ql_os.h"
#include "ql_hw.h"
#include "ql_def.h"
#include "ql_inline.h"
#include "ql_ver.h"
#include "ql_glbl.h"
#include "ql_dbg.h"
#include "ql_minidump.h"

/*
 * Static Functions
 */

static void qla_del_rcv_cntxt(qla_host_t *ha);
static int qla_init_rcv_cntxt(qla_host_t *ha);
static int qla_del_xmt_cntxt(qla_host_t *ha);
static int qla_init_xmt_cntxt(qla_host_t *ha);
static int qla_mbx_cmd(qla_host_t *ha, uint32_t *h_mbox, uint32_t n_hmbox,
	uint32_t *fw_mbox, uint32_t n_fwmbox, uint32_t no_pause);
static int qla_config_intr_cntxt(qla_host_t *ha, uint32_t start_idx,
	uint32_t num_intrs, uint32_t create);
static int qla_config_rss(qla_host_t *ha, uint16_t cntxt_id);
static int qla_config_intr_coalesce(qla_host_t *ha, uint16_t cntxt_id,
	int tenable, int rcv);
static int qla_set_mac_rcv_mode(qla_host_t *ha, uint32_t mode);
static int qla_link_event_req(qla_host_t *ha, uint16_t cntxt_id);

static int qla_tx_tso(qla_host_t *ha, struct mbuf *mp, q80_tx_cmd_t *tx_cmd,
		uint8_t *hdr);
static int qla_hw_add_all_mcast(qla_host_t *ha);
static int qla_add_rcv_rings(qla_host_t *ha, uint32_t sds_idx, uint32_t nsds);

static int qla_init_nic_func(qla_host_t *ha);
static int qla_stop_nic_func(qla_host_t *ha);
static int qla_query_fw_dcbx_caps(qla_host_t *ha);
static int qla_set_port_config(qla_host_t *ha, uint32_t cfg_bits);
static int qla_get_port_config(qla_host_t *ha, uint32_t *cfg_bits);
static int qla_set_cam_search_mode(qla_host_t *ha, uint32_t search_mode);
static int qla_get_cam_search_mode(qla_host_t *ha);

static void ql_minidump_free(qla_host_t *ha);

#ifdef QL_DBG

static void
qla_stop_pegs(qla_host_t *ha)
{
        uint32_t val = 1;

        ql_rdwr_indreg32(ha, Q8_CRB_PEG_0, &val, 0);
        ql_rdwr_indreg32(ha, Q8_CRB_PEG_1, &val, 0);
        ql_rdwr_indreg32(ha, Q8_CRB_PEG_2, &val, 0);
        ql_rdwr_indreg32(ha, Q8_CRB_PEG_3, &val, 0);
        ql_rdwr_indreg32(ha, Q8_CRB_PEG_4, &val, 0);
        device_printf(ha->pci_dev, "%s PEGS HALTED!!!!!\n", __func__);
}

static int
qla_sysctl_stop_pegs(SYSCTL_HANDLER_ARGS)
{
	int err, ret = 0;
	qla_host_t *ha;
	
	err = sysctl_handle_int(oidp, &ret, 0, req);


	if (err || !req->newptr)
		return (err);

	if (ret == 1) {
		ha = (qla_host_t *)arg1;
		if (QLA_LOCK(ha, __func__, QLA_LOCK_DEFAULT_MS_TIMEOUT, 0) == 0) {
			qla_stop_pegs(ha);	
			QLA_UNLOCK(ha, __func__);
		}
	}

	return err;
}
#endif /* #ifdef QL_DBG */

static int
qla_validate_set_port_cfg_bit(uint32_t bits)
{
        if ((bits & 0xF) > 1)
                return (-1);

        if (((bits >> 4) & 0xF) > 2)
                return (-1);

        if (((bits >> 8) & 0xF) > 2)
                return (-1);

        return (0);
}

static int
qla_sysctl_port_cfg(SYSCTL_HANDLER_ARGS)
{
        int err, ret = 0;
        qla_host_t *ha;
        uint32_t cfg_bits;

        err = sysctl_handle_int(oidp, &ret, 0, req);

        if (err || !req->newptr)
                return (err);

	ha = (qla_host_t *)arg1;

        if ((qla_validate_set_port_cfg_bit((uint32_t)ret) == 0)) {

                err = qla_get_port_config(ha, &cfg_bits);

                if (err)
                        goto qla_sysctl_set_port_cfg_exit;

                if (ret & 0x1) {
                        cfg_bits |= Q8_PORT_CFG_BITS_DCBX_ENABLE;
                } else {
                        cfg_bits &= ~Q8_PORT_CFG_BITS_DCBX_ENABLE;
                }

                ret = ret >> 4;
                cfg_bits &= ~Q8_PORT_CFG_BITS_PAUSE_CFG_MASK;

                if ((ret & 0xF) == 0) {
                        cfg_bits |= Q8_PORT_CFG_BITS_PAUSE_DISABLED;
                } else if ((ret & 0xF) == 1){
                        cfg_bits |= Q8_PORT_CFG_BITS_PAUSE_STD;
                } else {
                        cfg_bits |= Q8_PORT_CFG_BITS_PAUSE_PPM;
                }

                ret = ret >> 4;
                cfg_bits &= ~Q8_PORT_CFG_BITS_STDPAUSE_DIR_MASK;

                if (ret == 0) {
                        cfg_bits |= Q8_PORT_CFG_BITS_STDPAUSE_XMT_RCV;
                } else if (ret == 1){
                        cfg_bits |= Q8_PORT_CFG_BITS_STDPAUSE_XMT;
                } else {
                        cfg_bits |= Q8_PORT_CFG_BITS_STDPAUSE_RCV;
                }

		if (QLA_LOCK(ha, __func__, QLA_LOCK_DEFAULT_MS_TIMEOUT, 0) == 0) {
                	err = qla_set_port_config(ha, cfg_bits);
			QLA_UNLOCK(ha, __func__);
		} else {
			device_printf(ha->pci_dev, "%s: failed\n", __func__);
		}
        } else {
		if (QLA_LOCK(ha, __func__, QLA_LOCK_DEFAULT_MS_TIMEOUT, 0) == 0) {
                	err = qla_get_port_config(ha, &cfg_bits);
			QLA_UNLOCK(ha, __func__);
		} else {
			device_printf(ha->pci_dev, "%s: failed\n", __func__);
		}
        }

qla_sysctl_set_port_cfg_exit:
        return err;
}

static int
qla_sysctl_set_cam_search_mode(SYSCTL_HANDLER_ARGS)
{
	int err, ret = 0;
	qla_host_t *ha;

	err = sysctl_handle_int(oidp, &ret, 0, req);

	if (err || !req->newptr)
		return (err);

	ha = (qla_host_t *)arg1;

	if ((ret == Q8_HW_CONFIG_CAM_SEARCH_MODE_INTERNAL) ||
		(ret == Q8_HW_CONFIG_CAM_SEARCH_MODE_AUTO)) {

		if (QLA_LOCK(ha, __func__, QLA_LOCK_DEFAULT_MS_TIMEOUT, 0) == 0) {
			err = qla_set_cam_search_mode(ha, (uint32_t)ret);
			QLA_UNLOCK(ha, __func__);
		} else {
			device_printf(ha->pci_dev, "%s: failed\n", __func__);
		}

	} else {
		device_printf(ha->pci_dev, "%s: ret = %d\n", __func__, ret);
	}

	return (err);
}

static int
qla_sysctl_get_cam_search_mode(SYSCTL_HANDLER_ARGS)
{
	int err, ret = 0;
	qla_host_t *ha;

	err = sysctl_handle_int(oidp, &ret, 0, req);

	if (err || !req->newptr)
		return (err);

	ha = (qla_host_t *)arg1;
	if (QLA_LOCK(ha, __func__, QLA_LOCK_DEFAULT_MS_TIMEOUT, 0) == 0) {
		err = qla_get_cam_search_mode(ha);
		QLA_UNLOCK(ha, __func__);
	} else {
		device_printf(ha->pci_dev, "%s: failed\n", __func__);
	}

	return (err);
}

static void
qlnx_add_hw_mac_stats_sysctls(qla_host_t *ha)
{
        struct sysctl_ctx_list  *ctx;
        struct sysctl_oid_list  *children;
        struct sysctl_oid       *ctx_oid;

        ctx = device_get_sysctl_ctx(ha->pci_dev);
        children = SYSCTL_CHILDREN(device_get_sysctl_tree(ha->pci_dev));

        ctx_oid = SYSCTL_ADD_NODE(ctx, children, OID_AUTO, "stats_hw_mac",
                        CTLFLAG_RD, NULL, "stats_hw_mac");
        children = SYSCTL_CHILDREN(ctx_oid);

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "xmt_frames",
                CTLFLAG_RD, &ha->hw.mac.xmt_frames,
                "xmt_frames");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "xmt_bytes",
                CTLFLAG_RD, &ha->hw.mac.xmt_bytes,
                "xmt_frames");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "xmt_mcast_pkts",
                CTLFLAG_RD, &ha->hw.mac.xmt_mcast_pkts,
                "xmt_mcast_pkts");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "xmt_bcast_pkts",
                CTLFLAG_RD, &ha->hw.mac.xmt_bcast_pkts,
                "xmt_bcast_pkts");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "xmt_pause_frames",
                CTLFLAG_RD, &ha->hw.mac.xmt_pause_frames,
                "xmt_pause_frames");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "xmt_cntrl_pkts",
                CTLFLAG_RD, &ha->hw.mac.xmt_cntrl_pkts,
                "xmt_cntrl_pkts");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "xmt_pkt_lt_64bytes",
                CTLFLAG_RD, &ha->hw.mac.xmt_pkt_lt_64bytes,
                "xmt_pkt_lt_64bytes");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "xmt_pkt_lt_127bytes",
                CTLFLAG_RD, &ha->hw.mac.xmt_pkt_lt_127bytes,
                "xmt_pkt_lt_127bytes");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "xmt_pkt_lt_255bytes",
                CTLFLAG_RD, &ha->hw.mac.xmt_pkt_lt_255bytes,
                "xmt_pkt_lt_255bytes");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "xmt_pkt_lt_511bytes",
                CTLFLAG_RD, &ha->hw.mac.xmt_pkt_lt_511bytes,
                "xmt_pkt_lt_511bytes");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "xmt_pkt_lt_1023bytes",
                CTLFLAG_RD, &ha->hw.mac.xmt_pkt_lt_1023bytes,
                "xmt_pkt_lt_1023bytes");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "xmt_pkt_lt_1518bytes",
                CTLFLAG_RD, &ha->hw.mac.xmt_pkt_lt_1518bytes,
                "xmt_pkt_lt_1518bytes");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "xmt_pkt_gt_1518bytes",
                CTLFLAG_RD, &ha->hw.mac.xmt_pkt_gt_1518bytes,
                "xmt_pkt_gt_1518bytes");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rcv_frames",
                CTLFLAG_RD, &ha->hw.mac.rcv_frames,
                "rcv_frames");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rcv_bytes",
                CTLFLAG_RD, &ha->hw.mac.rcv_bytes,
                "rcv_bytes");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rcv_mcast_pkts",
                CTLFLAG_RD, &ha->hw.mac.rcv_mcast_pkts,
                "rcv_mcast_pkts");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rcv_bcast_pkts",
                CTLFLAG_RD, &ha->hw.mac.rcv_bcast_pkts,
                "rcv_bcast_pkts");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rcv_pause_frames",
                CTLFLAG_RD, &ha->hw.mac.rcv_pause_frames,
                "rcv_pause_frames");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rcv_cntrl_pkts",
                CTLFLAG_RD, &ha->hw.mac.rcv_cntrl_pkts,
                "rcv_cntrl_pkts");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rcv_pkt_lt_64bytes",
                CTLFLAG_RD, &ha->hw.mac.rcv_pkt_lt_64bytes,
                "rcv_pkt_lt_64bytes");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rcv_pkt_lt_127bytes",
                CTLFLAG_RD, &ha->hw.mac.rcv_pkt_lt_127bytes,
                "rcv_pkt_lt_127bytes");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rcv_pkt_lt_255bytes",
                CTLFLAG_RD, &ha->hw.mac.rcv_pkt_lt_255bytes,
                "rcv_pkt_lt_255bytes");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rcv_pkt_lt_511bytes",
                CTLFLAG_RD, &ha->hw.mac.rcv_pkt_lt_511bytes,
                "rcv_pkt_lt_511bytes");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rcv_pkt_lt_1023bytes",
                CTLFLAG_RD, &ha->hw.mac.rcv_pkt_lt_1023bytes,
                "rcv_pkt_lt_1023bytes");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rcv_pkt_lt_1518bytes",
                CTLFLAG_RD, &ha->hw.mac.rcv_pkt_lt_1518bytes,
                "rcv_pkt_lt_1518bytes");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rcv_pkt_gt_1518bytes",
                CTLFLAG_RD, &ha->hw.mac.rcv_pkt_gt_1518bytes,
                "rcv_pkt_gt_1518bytes");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rcv_len_error",
                CTLFLAG_RD, &ha->hw.mac.rcv_len_error,
                "rcv_len_error");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rcv_len_small",
                CTLFLAG_RD, &ha->hw.mac.rcv_len_small,
                "rcv_len_small");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rcv_len_large",
                CTLFLAG_RD, &ha->hw.mac.rcv_len_large,
                "rcv_len_large");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rcv_jabber",
                CTLFLAG_RD, &ha->hw.mac.rcv_jabber,
                "rcv_jabber");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rcv_dropped",
                CTLFLAG_RD, &ha->hw.mac.rcv_dropped,
                "rcv_dropped");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "fcs_error",
                CTLFLAG_RD, &ha->hw.mac.fcs_error,
                "fcs_error");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "align_error",
                CTLFLAG_RD, &ha->hw.mac.align_error,
                "align_error");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "eswitched_frames",
                CTLFLAG_RD, &ha->hw.mac.eswitched_frames,
                "eswitched_frames");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "eswitched_bytes",
                CTLFLAG_RD, &ha->hw.mac.eswitched_bytes,
                "eswitched_bytes");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "eswitched_mcast_frames",
                CTLFLAG_RD, &ha->hw.mac.eswitched_mcast_frames,
                "eswitched_mcast_frames");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "eswitched_bcast_frames",
                CTLFLAG_RD, &ha->hw.mac.eswitched_bcast_frames,
                "eswitched_bcast_frames");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "eswitched_ucast_frames",
                CTLFLAG_RD, &ha->hw.mac.eswitched_ucast_frames,
                "eswitched_ucast_frames");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "eswitched_err_free_frames",
                CTLFLAG_RD, &ha->hw.mac.eswitched_err_free_frames,
                "eswitched_err_free_frames");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "eswitched_err_free_bytes",
                CTLFLAG_RD, &ha->hw.mac.eswitched_err_free_bytes,
                "eswitched_err_free_bytes");

	return;
}

static void
qlnx_add_hw_rcv_stats_sysctls(qla_host_t *ha)
{
        struct sysctl_ctx_list  *ctx;
        struct sysctl_oid_list  *children;
        struct sysctl_oid       *ctx_oid;

        ctx = device_get_sysctl_ctx(ha->pci_dev);
        children = SYSCTL_CHILDREN(device_get_sysctl_tree(ha->pci_dev));

        ctx_oid = SYSCTL_ADD_NODE(ctx, children, OID_AUTO, "stats_hw_rcv",
                        CTLFLAG_RD, NULL, "stats_hw_rcv");
        children = SYSCTL_CHILDREN(ctx_oid);

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "total_bytes",
                CTLFLAG_RD, &ha->hw.rcv.total_bytes,
                "total_bytes");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "total_pkts",
                CTLFLAG_RD, &ha->hw.rcv.total_pkts,
                "total_pkts");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "lro_pkt_count",
                CTLFLAG_RD, &ha->hw.rcv.lro_pkt_count,
                "lro_pkt_count");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "sw_pkt_count",
                CTLFLAG_RD, &ha->hw.rcv.sw_pkt_count,
                "sw_pkt_count");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "ip_chksum_err",
                CTLFLAG_RD, &ha->hw.rcv.ip_chksum_err,
                "ip_chksum_err");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "pkts_wo_acntxts",
                CTLFLAG_RD, &ha->hw.rcv.pkts_wo_acntxts,
                "pkts_wo_acntxts");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "pkts_dropped_no_sds_card",
                CTLFLAG_RD, &ha->hw.rcv.pkts_dropped_no_sds_card,
                "pkts_dropped_no_sds_card");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "pkts_dropped_no_sds_host",
                CTLFLAG_RD, &ha->hw.rcv.pkts_dropped_no_sds_host,
                "pkts_dropped_no_sds_host");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "oversized_pkts",
                CTLFLAG_RD, &ha->hw.rcv.oversized_pkts,
                "oversized_pkts");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "pkts_dropped_no_rds",
                CTLFLAG_RD, &ha->hw.rcv.pkts_dropped_no_rds,
                "pkts_dropped_no_rds");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "unxpctd_mcast_pkts",
                CTLFLAG_RD, &ha->hw.rcv.unxpctd_mcast_pkts,
                "unxpctd_mcast_pkts");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "re1_fbq_error",
                CTLFLAG_RD, &ha->hw.rcv.re1_fbq_error,
                "re1_fbq_error");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "invalid_mac_addr",
                CTLFLAG_RD, &ha->hw.rcv.invalid_mac_addr,
                "invalid_mac_addr");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rds_prime_trys",
                CTLFLAG_RD, &ha->hw.rcv.rds_prime_trys,
                "rds_prime_trys");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rds_prime_success",
                CTLFLAG_RD, &ha->hw.rcv.rds_prime_success,
                "rds_prime_success");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "lro_flows_added",
                CTLFLAG_RD, &ha->hw.rcv.lro_flows_added,
                "lro_flows_added");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "lro_flows_deleted",
                CTLFLAG_RD, &ha->hw.rcv.lro_flows_deleted,
                "lro_flows_deleted");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "lro_flows_active",
                CTLFLAG_RD, &ha->hw.rcv.lro_flows_active,
                "lro_flows_active");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "pkts_droped_unknown",
                CTLFLAG_RD, &ha->hw.rcv.pkts_droped_unknown,
                "pkts_droped_unknown");

        SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "pkts_cnt_oversized",
                CTLFLAG_RD, &ha->hw.rcv.pkts_cnt_oversized,
                "pkts_cnt_oversized");

	return;
}

static void
qlnx_add_hw_xmt_stats_sysctls(qla_host_t *ha)
{
        struct sysctl_ctx_list  *ctx;
        struct sysctl_oid_list  *children;
        struct sysctl_oid_list  *node_children;
        struct sysctl_oid       *ctx_oid;
        int                     i;
        uint8_t                 name_str[16];

        ctx = device_get_sysctl_ctx(ha->pci_dev);
        children = SYSCTL_CHILDREN(device_get_sysctl_tree(ha->pci_dev));

        ctx_oid = SYSCTL_ADD_NODE(ctx, children, OID_AUTO, "stats_hw_xmt",
                        CTLFLAG_RD, NULL, "stats_hw_xmt");
        children = SYSCTL_CHILDREN(ctx_oid);

        for (i = 0; i < ha->hw.num_tx_rings; i++) {

                bzero(name_str, (sizeof(uint8_t) * sizeof(name_str)));
                snprintf(name_str, sizeof(name_str), "%d", i);

                ctx_oid = SYSCTL_ADD_NODE(ctx, children, OID_AUTO, name_str,
                        CTLFLAG_RD, NULL, name_str);
                node_children = SYSCTL_CHILDREN(ctx_oid);

                /* Tx Related */

                SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "total_bytes",
                        CTLFLAG_RD, &ha->hw.xmt[i].total_bytes,
                        "total_bytes");

                SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "total_pkts",
                        CTLFLAG_RD, &ha->hw.xmt[i].total_pkts,
                        "total_pkts");

                SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "errors",
                        CTLFLAG_RD, &ha->hw.xmt[i].errors,
                        "errors");

                SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "pkts_dropped",
                        CTLFLAG_RD, &ha->hw.xmt[i].pkts_dropped,
                        "pkts_dropped");

                SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "switch_pkts",
                        CTLFLAG_RD, &ha->hw.xmt[i].switch_pkts,
                        "switch_pkts");

                SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "num_buffers",
                        CTLFLAG_RD, &ha->hw.xmt[i].num_buffers,
                        "num_buffers");
	}

	return;
}

static void
qlnx_add_hw_mbx_cmpl_stats_sysctls(qla_host_t *ha)
{
        struct sysctl_ctx_list  *ctx;
        struct sysctl_oid_list  *node_children;

        ctx = device_get_sysctl_ctx(ha->pci_dev);
        node_children = SYSCTL_CHILDREN(device_get_sysctl_tree(ha->pci_dev));

	SYSCTL_ADD_QUAD(ctx, node_children,
		OID_AUTO, "mbx_completion_time_lt_200ms",
		CTLFLAG_RD, &ha->hw.mbx_comp_msecs[0],
		"mbx_completion_time_lt_200ms");

	SYSCTL_ADD_QUAD(ctx, node_children,
		OID_AUTO, "mbx_completion_time_200ms_400ms",
		CTLFLAG_RD, &ha->hw.mbx_comp_msecs[1],
		"mbx_completion_time_200ms_400ms");

	SYSCTL_ADD_QUAD(ctx, node_children,
		OID_AUTO, "mbx_completion_time_400ms_600ms",
		CTLFLAG_RD, &ha->hw.mbx_comp_msecs[2],
		"mbx_completion_time_400ms_600ms");

	SYSCTL_ADD_QUAD(ctx, node_children,
		OID_AUTO, "mbx_completion_time_600ms_800ms",
		CTLFLAG_RD, &ha->hw.mbx_comp_msecs[3],
		"mbx_completion_time_600ms_800ms");

	SYSCTL_ADD_QUAD(ctx, node_children,
		OID_AUTO, "mbx_completion_time_800ms_1000ms",
		CTLFLAG_RD, &ha->hw.mbx_comp_msecs[4],
		"mbx_completion_time_800ms_1000ms");

	SYSCTL_ADD_QUAD(ctx, node_children,
		OID_AUTO, "mbx_completion_time_1000ms_1200ms",
		CTLFLAG_RD, &ha->hw.mbx_comp_msecs[5],
		"mbx_completion_time_1000ms_1200ms");

	SYSCTL_ADD_QUAD(ctx, node_children,
		OID_AUTO, "mbx_completion_time_1200ms_1400ms",
		CTLFLAG_RD, &ha->hw.mbx_comp_msecs[6],
		"mbx_completion_time_1200ms_1400ms");

	SYSCTL_ADD_QUAD(ctx, node_children,
		OID_AUTO, "mbx_completion_time_1400ms_1600ms",
		CTLFLAG_RD, &ha->hw.mbx_comp_msecs[7],
		"mbx_completion_time_1400ms_1600ms");

	SYSCTL_ADD_QUAD(ctx, node_children,
		OID_AUTO, "mbx_completion_time_1600ms_1800ms",
		CTLFLAG_RD, &ha->hw.mbx_comp_msecs[8],
		"mbx_completion_time_1600ms_1800ms");

	SYSCTL_ADD_QUAD(ctx, node_children,
		OID_AUTO, "mbx_completion_time_1800ms_2000ms",
		CTLFLAG_RD, &ha->hw.mbx_comp_msecs[9],
		"mbx_completion_time_1800ms_2000ms");

	SYSCTL_ADD_QUAD(ctx, node_children,
		OID_AUTO, "mbx_completion_time_2000ms_2200ms",
		CTLFLAG_RD, &ha->hw.mbx_comp_msecs[10],
		"mbx_completion_time_2000ms_2200ms");

	SYSCTL_ADD_QUAD(ctx, node_children,
		OID_AUTO, "mbx_completion_time_2200ms_2400ms",
		CTLFLAG_RD, &ha->hw.mbx_comp_msecs[11],
		"mbx_completion_time_2200ms_2400ms");

	SYSCTL_ADD_QUAD(ctx, node_children,
		OID_AUTO, "mbx_completion_time_2400ms_2600ms",
		CTLFLAG_RD, &ha->hw.mbx_comp_msecs[12],
		"mbx_completion_time_2400ms_2600ms");

	SYSCTL_ADD_QUAD(ctx, node_children,
		OID_AUTO, "mbx_completion_time_2600ms_2800ms",
		CTLFLAG_RD, &ha->hw.mbx_comp_msecs[13],
		"mbx_completion_time_2600ms_2800ms");

	SYSCTL_ADD_QUAD(ctx, node_children,
		OID_AUTO, "mbx_completion_time_2800ms_3000ms",
		CTLFLAG_RD, &ha->hw.mbx_comp_msecs[14],
		"mbx_completion_time_2800ms_3000ms");

	SYSCTL_ADD_QUAD(ctx, node_children,
		OID_AUTO, "mbx_completion_time_3000ms_4000ms",
		CTLFLAG_RD, &ha->hw.mbx_comp_msecs[15],
		"mbx_completion_time_3000ms_4000ms");

	SYSCTL_ADD_QUAD(ctx, node_children,
		OID_AUTO, "mbx_completion_time_4000ms_5000ms",
		CTLFLAG_RD, &ha->hw.mbx_comp_msecs[16],
		"mbx_completion_time_4000ms_5000ms");

	SYSCTL_ADD_QUAD(ctx, node_children,
		OID_AUTO, "mbx_completion_host_mbx_cntrl_timeout",
		CTLFLAG_RD, &ha->hw.mbx_comp_msecs[17],
		"mbx_completion_host_mbx_cntrl_timeout");

	SYSCTL_ADD_QUAD(ctx, node_children,
		OID_AUTO, "mbx_completion_fw_mbx_cntrl_timeout",
		CTLFLAG_RD, &ha->hw.mbx_comp_msecs[18],
		"mbx_completion_fw_mbx_cntrl_timeout");
	return;
}

static void
qlnx_add_hw_stats_sysctls(qla_host_t *ha)
{
	qlnx_add_hw_mac_stats_sysctls(ha);
	qlnx_add_hw_rcv_stats_sysctls(ha);
	qlnx_add_hw_xmt_stats_sysctls(ha);
	qlnx_add_hw_mbx_cmpl_stats_sysctls(ha);

	return;
}

static void
qlnx_add_drvr_sds_stats(qla_host_t *ha)
{
        struct sysctl_ctx_list  *ctx;
        struct sysctl_oid_list  *children;
        struct sysctl_oid_list  *node_children;
        struct sysctl_oid       *ctx_oid;
        int                     i;
        uint8_t                 name_str[16];

        ctx = device_get_sysctl_ctx(ha->pci_dev);
        children = SYSCTL_CHILDREN(device_get_sysctl_tree(ha->pci_dev));

        ctx_oid = SYSCTL_ADD_NODE(ctx, children, OID_AUTO, "stats_drvr_sds",
                        CTLFLAG_RD, NULL, "stats_drvr_sds");
        children = SYSCTL_CHILDREN(ctx_oid);

        for (i = 0; i < ha->hw.num_sds_rings; i++) {

                bzero(name_str, (sizeof(uint8_t) * sizeof(name_str)));
                snprintf(name_str, sizeof(name_str), "%d", i);

                ctx_oid = SYSCTL_ADD_NODE(ctx, children, OID_AUTO, name_str,
                        CTLFLAG_RD, NULL, name_str);
                node_children = SYSCTL_CHILDREN(ctx_oid);

                SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "intr_count",
                        CTLFLAG_RD, &ha->hw.sds[i].intr_count,
                        "intr_count");

                SYSCTL_ADD_UINT(ctx, node_children,
			OID_AUTO, "rx_free",
                        CTLFLAG_RD, &ha->hw.sds[i].rx_free,
			ha->hw.sds[i].rx_free, "rx_free");
	}

	return;
}
static void
qlnx_add_drvr_rds_stats(qla_host_t *ha)
{
        struct sysctl_ctx_list  *ctx;
        struct sysctl_oid_list  *children;
        struct sysctl_oid_list  *node_children;
        struct sysctl_oid       *ctx_oid;
        int                     i;
        uint8_t                 name_str[16];

        ctx = device_get_sysctl_ctx(ha->pci_dev);
        children = SYSCTL_CHILDREN(device_get_sysctl_tree(ha->pci_dev));

        ctx_oid = SYSCTL_ADD_NODE(ctx, children, OID_AUTO, "stats_drvr_rds",
                        CTLFLAG_RD, NULL, "stats_drvr_rds");
        children = SYSCTL_CHILDREN(ctx_oid);

        for (i = 0; i < ha->hw.num_rds_rings; i++) {

                bzero(name_str, (sizeof(uint8_t) * sizeof(name_str)));
                snprintf(name_str, sizeof(name_str), "%d", i);

                ctx_oid = SYSCTL_ADD_NODE(ctx, children, OID_AUTO, name_str,
                        CTLFLAG_RD, NULL, name_str);
                node_children = SYSCTL_CHILDREN(ctx_oid);

                SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "count",
                        CTLFLAG_RD, &ha->hw.rds[i].count,
                        "count");

                SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "lro_pkt_count",
                        CTLFLAG_RD, &ha->hw.rds[i].lro_pkt_count,
                        "lro_pkt_count");

                SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "lro_bytes",
                        CTLFLAG_RD, &ha->hw.rds[i].lro_bytes,
                        "lro_bytes");
	}

	return;
}

static void
qlnx_add_drvr_tx_stats(qla_host_t *ha)
{
        struct sysctl_ctx_list  *ctx;
        struct sysctl_oid_list  *children;
        struct sysctl_oid_list  *node_children;
        struct sysctl_oid       *ctx_oid;
        int                     i;
        uint8_t                 name_str[16];

        ctx = device_get_sysctl_ctx(ha->pci_dev);
        children = SYSCTL_CHILDREN(device_get_sysctl_tree(ha->pci_dev));

        ctx_oid = SYSCTL_ADD_NODE(ctx, children, OID_AUTO, "stats_drvr_xmt",
                        CTLFLAG_RD, NULL, "stats_drvr_xmt");
        children = SYSCTL_CHILDREN(ctx_oid);

        for (i = 0; i < ha->hw.num_tx_rings; i++) {

                bzero(name_str, (sizeof(uint8_t) * sizeof(name_str)));
                snprintf(name_str, sizeof(name_str), "%d", i);

                ctx_oid = SYSCTL_ADD_NODE(ctx, children, OID_AUTO, name_str,
                        CTLFLAG_RD, NULL, name_str);
                node_children = SYSCTL_CHILDREN(ctx_oid);

                SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "count",
                        CTLFLAG_RD, &ha->tx_ring[i].count,
                        "count");

#ifdef QL_ENABLE_ISCSI_TLV
                SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "iscsi_pkt_count",
                        CTLFLAG_RD, &ha->tx_ring[i].iscsi_pkt_count,
                        "iscsi_pkt_count");
#endif /* #ifdef QL_ENABLE_ISCSI_TLV */
	}

	return;
}

static void
qlnx_add_drvr_stats_sysctls(qla_host_t *ha)
{
	qlnx_add_drvr_sds_stats(ha);
	qlnx_add_drvr_rds_stats(ha);
	qlnx_add_drvr_tx_stats(ha);
	return;
}

/*
 * Name: ql_hw_add_sysctls
 * Function: Add P3Plus specific sysctls
 */
void
ql_hw_add_sysctls(qla_host_t *ha)
{
        device_t	dev;

        dev = ha->pci_dev;

	SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
		SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
		OID_AUTO, "num_rds_rings", CTLFLAG_RD, &ha->hw.num_rds_rings,
		ha->hw.num_rds_rings, "Number of Rcv Descriptor Rings");

        SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "num_sds_rings", CTLFLAG_RD, &ha->hw.num_sds_rings,
		ha->hw.num_sds_rings, "Number of Status Descriptor Rings");

        SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "num_tx_rings", CTLFLAG_RD, &ha->hw.num_tx_rings,
		ha->hw.num_tx_rings, "Number of Transmit Rings");

        SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "tx_ring_index", CTLFLAG_RW, &ha->txr_idx,
		ha->txr_idx, "Tx Ring Used");

        SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "max_tx_segs", CTLFLAG_RD, &ha->hw.max_tx_segs,
		ha->hw.max_tx_segs, "Max # of Segments in a non-TSO pkt");

	ha->hw.sds_cidx_thres = 32;
        SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "sds_cidx_thres", CTLFLAG_RW, &ha->hw.sds_cidx_thres,
		ha->hw.sds_cidx_thres,
		"Number of SDS entries to process before updating"
		" SDS Ring Consumer Index");

	ha->hw.rds_pidx_thres = 32;
        SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "rds_pidx_thres", CTLFLAG_RW, &ha->hw.rds_pidx_thres,
		ha->hw.rds_pidx_thres,
		"Number of Rcv Rings Entries to post before updating"
		" RDS Ring Producer Index");

        ha->hw.rcv_intr_coalesce = (3 << 16) | 256;
        SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "rcv_intr_coalesce", CTLFLAG_RW,
                &ha->hw.rcv_intr_coalesce,
                ha->hw.rcv_intr_coalesce,
                "Rcv Intr Coalescing Parameters\n"
                "\tbits 15:0 max packets\n"
                "\tbits 31:16 max micro-seconds to wait\n"
                "\tplease run\n"
                "\tifconfig <if> down && ifconfig <if> up\n"
                "\tto take effect \n");

        ha->hw.xmt_intr_coalesce = (64 << 16) | 64;
        SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "xmt_intr_coalesce", CTLFLAG_RW,
                &ha->hw.xmt_intr_coalesce,
                ha->hw.xmt_intr_coalesce,
                "Xmt Intr Coalescing Parameters\n"
                "\tbits 15:0 max packets\n"
                "\tbits 31:16 max micro-seconds to wait\n"
                "\tplease run\n"
                "\tifconfig <if> down && ifconfig <if> up\n"
                "\tto take effect \n");

        SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "port_cfg", CTLTYPE_INT | CTLFLAG_RW,
                (void *)ha, 0,
                qla_sysctl_port_cfg, "I",
                        "Set Port Configuration if values below "
                        "otherwise Get Port Configuration\n"
                        "\tBits 0-3 ; 1 = DCBX Enable; 0 = DCBX Disable\n"
                        "\tBits 4-7 : 0 = no pause; 1 = std ; 2 = ppm \n"
                        "\tBits 8-11: std pause cfg; 0 = xmt and rcv;"
                        " 1 = xmt only; 2 = rcv only;\n"
                );

	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
		SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
		OID_AUTO, "set_cam_search_mode", CTLTYPE_INT | CTLFLAG_RW,
		(void *)ha, 0,
		qla_sysctl_set_cam_search_mode, "I",
			"Set CAM Search Mode"
			"\t 1 = search mode internal\n"
			"\t 2 = search mode auto\n");

	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
		SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
		OID_AUTO, "get_cam_search_mode", CTLTYPE_INT | CTLFLAG_RW,
		(void *)ha, 0,
		qla_sysctl_get_cam_search_mode, "I",
			"Get CAM Search Mode"
			"\t 1 = search mode internal\n"
			"\t 2 = search mode auto\n");

        ha->hw.enable_9kb = 1;

        SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "enable_9kb", CTLFLAG_RW, &ha->hw.enable_9kb,
                ha->hw.enable_9kb, "Enable 9Kbyte Buffers when MTU = 9000");

        ha->hw.enable_hw_lro = 1;

        SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "enable_hw_lro", CTLFLAG_RW, &ha->hw.enable_hw_lro,
                ha->hw.enable_hw_lro, "Enable Hardware LRO; Default is true \n"
		"\t 1 : Hardware LRO if LRO is enabled\n"
		"\t 0 : Software LRO if LRO is enabled\n"
		"\t Any change requires ifconfig down/up to take effect\n"
		"\t Note that LRO may be turned off/on via ifconfig\n");

        SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "sp_log_index", CTLFLAG_RW, &ha->hw.sp_log_index,
                ha->hw.sp_log_index, "sp_log_index");

        SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "sp_log_stop", CTLFLAG_RW, &ha->hw.sp_log_stop,
                ha->hw.sp_log_stop, "sp_log_stop");

        ha->hw.sp_log_stop_events = 0;

        SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "sp_log_stop_events", CTLFLAG_RW,
		&ha->hw.sp_log_stop_events,
                ha->hw.sp_log_stop_events, "Slow path event log is stopped"
		" when OR of the following events occur \n"
		"\t 0x01 : Heart beat Failure\n"
		"\t 0x02 : Temperature Failure\n"
		"\t 0x04 : HW Initialization Failure\n"
		"\t 0x08 : Interface Initialization Failure\n"
		"\t 0x10 : Error Recovery Failure\n");

	ha->hw.mdump_active = 0;
        SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "minidump_active", CTLFLAG_RW, &ha->hw.mdump_active,
		ha->hw.mdump_active,
		"Minidump retrieval is Active");

	ha->hw.mdump_done = 0;
        SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "mdump_done", CTLFLAG_RW,
		&ha->hw.mdump_done, ha->hw.mdump_done,
		"Minidump has been done and available for retrieval");

	ha->hw.mdump_capture_mask = 0xF;
        SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "minidump_capture_mask", CTLFLAG_RW,
		&ha->hw.mdump_capture_mask, ha->hw.mdump_capture_mask,
		"Minidump capture mask");
#ifdef QL_DBG

	ha->err_inject = 0;
        SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "err_inject",
                CTLFLAG_RW, &ha->err_inject, ha->err_inject,
                "Error to be injected\n"
                "\t\t\t 0: No Errors\n"
                "\t\t\t 1: rcv: rxb struct invalid\n"
                "\t\t\t 2: rcv: mp == NULL\n"
                "\t\t\t 3: lro: rxb struct invalid\n"
                "\t\t\t 4: lro: mp == NULL\n"
                "\t\t\t 5: rcv: num handles invalid\n"
                "\t\t\t 6: reg: indirect reg rd_wr failure\n"
                "\t\t\t 7: ocm: offchip memory rd_wr failure\n"
                "\t\t\t 8: mbx: mailbox command failure\n"
                "\t\t\t 9: heartbeat failure\n"
                "\t\t\t A: temperature failure\n"
		"\t\t\t 11: m_getcl or m_getjcl failure\n"
		"\t\t\t 13: Invalid Descriptor Count in SGL Receive\n"
		"\t\t\t 14: Invalid Descriptor Count in LRO Receive\n"
		"\t\t\t 15: peer port error recovery failure\n"
		"\t\t\t 16: tx_buf[next_prod_index].mbuf != NULL\n" );

	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "peg_stop", CTLTYPE_INT | CTLFLAG_RW,
                (void *)ha, 0,
                qla_sysctl_stop_pegs, "I", "Peg Stop");

#endif /* #ifdef QL_DBG */

        ha->hw.user_pri_nic = 0;
        SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "user_pri_nic", CTLFLAG_RW, &ha->hw.user_pri_nic,
                ha->hw.user_pri_nic,
                "VLAN Tag User Priority for Normal Ethernet Packets");

        ha->hw.user_pri_iscsi = 4;
        SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "user_pri_iscsi", CTLFLAG_RW, &ha->hw.user_pri_iscsi,
                ha->hw.user_pri_iscsi,
                "VLAN Tag User Priority for iSCSI Packets");

	qlnx_add_hw_stats_sysctls(ha);
	qlnx_add_drvr_stats_sysctls(ha);

	return;
}

void
ql_hw_link_status(qla_host_t *ha)
{
	device_printf(ha->pci_dev, "cable_oui\t\t 0x%08x\n", ha->hw.cable_oui);

	if (ha->hw.link_up) {
		device_printf(ha->pci_dev, "link Up\n");
	} else {
		device_printf(ha->pci_dev, "link Down\n");
	}

	if (ha->hw.fduplex) {
		device_printf(ha->pci_dev, "Full Duplex\n");
	} else {
		device_printf(ha->pci_dev, "Half Duplex\n");
	}

	if (ha->hw.autoneg) {
		device_printf(ha->pci_dev, "Auto Negotiation Enabled\n");
	} else {
		device_printf(ha->pci_dev, "Auto Negotiation Disabled\n");
	}

	switch (ha->hw.link_speed) {
	case 0x710:
		device_printf(ha->pci_dev, "link speed\t\t 10Gps\n");
		break;

	case 0x3E8:
		device_printf(ha->pci_dev, "link speed\t\t 1Gps\n");
		break;

	case 0x64:
		device_printf(ha->pci_dev, "link speed\t\t 100Mbps\n");
		break;

	default:
		device_printf(ha->pci_dev, "link speed\t\t Unknown\n");
		break;
	}

	switch (ha->hw.module_type) {

	case 0x01:
		device_printf(ha->pci_dev, "Module Type 10GBase-LRM\n");
		break;

	case 0x02:
		device_printf(ha->pci_dev, "Module Type 10GBase-LR\n");
		break;

	case 0x03:
		device_printf(ha->pci_dev, "Module Type 10GBase-SR\n");
		break;

	case 0x04:
		device_printf(ha->pci_dev,
			"Module Type 10GE Passive Copper(Compliant)[%d m]\n",
			ha->hw.cable_length);
		break;

	case 0x05:
		device_printf(ha->pci_dev, "Module Type 10GE Active"
			" Limiting Copper(Compliant)[%d m]\n",
			ha->hw.cable_length);
		break;

	case 0x06:
		device_printf(ha->pci_dev,
			"Module Type 10GE Passive Copper"
			" (Legacy, Best Effort)[%d m]\n",
			ha->hw.cable_length);
		break;

	case 0x07:
		device_printf(ha->pci_dev, "Module Type 1000Base-SX\n");
		break;

	case 0x08:
		device_printf(ha->pci_dev, "Module Type 1000Base-LX\n");
		break;

	case 0x09:
		device_printf(ha->pci_dev, "Module Type 1000Base-CX\n");
		break;

	case 0x0A:
		device_printf(ha->pci_dev, "Module Type 1000Base-T\n");
		break;

	case 0x0B:
		device_printf(ha->pci_dev, "Module Type 1GE Passive Copper"
			"(Legacy, Best Effort)\n");
		break;

	default:
		device_printf(ha->pci_dev, "Unknown Module Type 0x%x\n",
			ha->hw.module_type);
		break;
	}

	if (ha->hw.link_faults == 1)
		device_printf(ha->pci_dev, "SFP Power Fault\n");
}

/*
 * Name: ql_free_dma
 * Function: Frees the DMA'able memory allocated in ql_alloc_dma()
 */
void
ql_free_dma(qla_host_t *ha)
{
	uint32_t i;

        if (ha->hw.dma_buf.flags.sds_ring) {
		for (i = 0; i < ha->hw.num_sds_rings; i++) {
			ql_free_dmabuf(ha, &ha->hw.dma_buf.sds_ring[i]);
		}
        	ha->hw.dma_buf.flags.sds_ring = 0;
	}

        if (ha->hw.dma_buf.flags.rds_ring) {
		for (i = 0; i < ha->hw.num_rds_rings; i++) {
			ql_free_dmabuf(ha, &ha->hw.dma_buf.rds_ring[i]);
		}
        	ha->hw.dma_buf.flags.rds_ring = 0;
	}

        if (ha->hw.dma_buf.flags.tx_ring) {
		ql_free_dmabuf(ha, &ha->hw.dma_buf.tx_ring);
        	ha->hw.dma_buf.flags.tx_ring = 0;
	}
	ql_minidump_free(ha);
}

/*
 * Name: ql_alloc_dma
 * Function: Allocates DMA'able memory for Tx/Rx Rings, Tx/Rx Contexts.
 */
int
ql_alloc_dma(qla_host_t *ha)
{
        device_t                dev;
	uint32_t		i, j, size, tx_ring_size;
	qla_hw_t		*hw;
	qla_hw_tx_cntxt_t	*tx_cntxt;
	uint8_t			*vaddr;
	bus_addr_t		paddr;

        dev = ha->pci_dev;

        QL_DPRINT2(ha, (dev, "%s: enter\n", __func__));

	hw = &ha->hw;
	/*
	 * Allocate Transmit Ring
	 */
	tx_ring_size = (sizeof(q80_tx_cmd_t) * NUM_TX_DESCRIPTORS);
	size = (tx_ring_size * ha->hw.num_tx_rings);

	hw->dma_buf.tx_ring.alignment = 8;
	hw->dma_buf.tx_ring.size = size + PAGE_SIZE;
	
        if (ql_alloc_dmabuf(ha, &hw->dma_buf.tx_ring)) {
                device_printf(dev, "%s: tx ring alloc failed\n", __func__);
                goto ql_alloc_dma_exit;
        }

	vaddr = (uint8_t *)hw->dma_buf.tx_ring.dma_b;
	paddr = hw->dma_buf.tx_ring.dma_addr;
	
	for (i = 0; i < ha->hw.num_tx_rings; i++) {
		tx_cntxt = (qla_hw_tx_cntxt_t *)&hw->tx_cntxt[i];

		tx_cntxt->tx_ring_base = (q80_tx_cmd_t *)vaddr;
		tx_cntxt->tx_ring_paddr = paddr;

		vaddr += tx_ring_size;
		paddr += tx_ring_size;
	}

	for (i = 0; i < ha->hw.num_tx_rings; i++) {
		tx_cntxt = (qla_hw_tx_cntxt_t *)&hw->tx_cntxt[i];

		tx_cntxt->tx_cons = (uint32_t *)vaddr;
		tx_cntxt->tx_cons_paddr = paddr;

		vaddr += sizeof (uint32_t);
		paddr += sizeof (uint32_t);
	}

        ha->hw.dma_buf.flags.tx_ring = 1;

	QL_DPRINT2(ha, (dev, "%s: tx_ring phys %p virt %p\n",
		__func__, (void *)(hw->dma_buf.tx_ring.dma_addr),
		hw->dma_buf.tx_ring.dma_b));
	/*
	 * Allocate Receive Descriptor Rings
	 */

	for (i = 0; i < hw->num_rds_rings; i++) {

		hw->dma_buf.rds_ring[i].alignment = 8;
		hw->dma_buf.rds_ring[i].size =
			(sizeof(q80_recv_desc_t)) * NUM_RX_DESCRIPTORS;

		if (ql_alloc_dmabuf(ha, &hw->dma_buf.rds_ring[i])) {
			device_printf(dev, "%s: rds ring[%d] alloc failed\n",
				__func__, i);

			for (j = 0; j < i; j++)
				ql_free_dmabuf(ha, &hw->dma_buf.rds_ring[j]);

			goto ql_alloc_dma_exit;
		}
		QL_DPRINT4(ha, (dev, "%s: rx_ring[%d] phys %p virt %p\n",
			__func__, i, (void *)(hw->dma_buf.rds_ring[i].dma_addr),
			hw->dma_buf.rds_ring[i].dma_b));
	}

	hw->dma_buf.flags.rds_ring = 1;

	/*
	 * Allocate Status Descriptor Rings
	 */

	for (i = 0; i < hw->num_sds_rings; i++) {
		hw->dma_buf.sds_ring[i].alignment = 8;
		hw->dma_buf.sds_ring[i].size =
			(sizeof(q80_stat_desc_t)) * NUM_STATUS_DESCRIPTORS;

		if (ql_alloc_dmabuf(ha, &hw->dma_buf.sds_ring[i])) {
			device_printf(dev, "%s: sds ring alloc failed\n",
				__func__);

			for (j = 0; j < i; j++)
				ql_free_dmabuf(ha, &hw->dma_buf.sds_ring[j]);

			goto ql_alloc_dma_exit;
		}
		QL_DPRINT4(ha, (dev, "%s: sds_ring[%d] phys %p virt %p\n",
			__func__, i,
			(void *)(hw->dma_buf.sds_ring[i].dma_addr),
			hw->dma_buf.sds_ring[i].dma_b));
	}
	for (i = 0; i < hw->num_sds_rings; i++) {
		hw->sds[i].sds_ring_base =
			(q80_stat_desc_t *)hw->dma_buf.sds_ring[i].dma_b;
	}

	hw->dma_buf.flags.sds_ring = 1;

	return 0;

ql_alloc_dma_exit:
	ql_free_dma(ha);
	return -1;
}

#define Q8_MBX_MSEC_DELAY	5000

static int
qla_mbx_cmd(qla_host_t *ha, uint32_t *h_mbox, uint32_t n_hmbox,
	uint32_t *fw_mbox, uint32_t n_fwmbox, uint32_t no_pause)
{
	uint32_t i;
	uint32_t data;
	int ret = 0;
	uint64_t start_usecs;
	uint64_t end_usecs;
	uint64_t msecs_200;

	ql_sp_log(ha, 0, 5, no_pause, h_mbox[0], h_mbox[1], h_mbox[2], h_mbox[3]);

	if (ha->offline || ha->qla_initiate_recovery) {
		ql_sp_log(ha, 1, 2, ha->offline, ha->qla_initiate_recovery, 0, 0, 0);
		goto exit_qla_mbx_cmd;
	}

	if (((ha->err_inject & 0xFFFF) == INJCT_MBX_CMD_FAILURE) &&
		(((ha->err_inject & ~0xFFFF) == ((h_mbox[0] & 0xFFFF) << 16))||
		!(ha->err_inject & ~0xFFFF))) {
		ret = -3;
		QL_INITIATE_RECOVERY(ha);
		goto exit_qla_mbx_cmd;
	}

	start_usecs = qla_get_usec_timestamp();

	if (no_pause)
		i = 1000;
	else
		i = Q8_MBX_MSEC_DELAY;

	while (i) {

		if (ha->qla_initiate_recovery) {
			ql_sp_log(ha, 2, 1, ha->qla_initiate_recovery, 0, 0, 0, 0);
			return (-1);
		}

		data = READ_REG32(ha, Q8_HOST_MBOX_CNTRL);
		if (data == 0)
			break;
		if (no_pause) {
			DELAY(1000);
		} else {
			qla_mdelay(__func__, 1);
		}
		i--;
	}

	if (i == 0) {
		device_printf(ha->pci_dev, "%s: host_mbx_cntrl 0x%08x\n",
			__func__, data);
		ql_sp_log(ha, 3, 1, data, 0, 0, 0, 0);
		ret = -1;
		ha->hw.mbx_comp_msecs[(Q8_MBX_COMP_MSECS - 2)]++;
		QL_INITIATE_RECOVERY(ha);
		goto exit_qla_mbx_cmd;
	}

	for (i = 0; i < n_hmbox; i++) {
		WRITE_REG32(ha, (Q8_HOST_MBOX0 + (i << 2)), *h_mbox);
		h_mbox++;
	}

	WRITE_REG32(ha, Q8_HOST_MBOX_CNTRL, 0x1);


	i = Q8_MBX_MSEC_DELAY;
	while (i) {

		if (ha->qla_initiate_recovery) {
			ql_sp_log(ha, 4, 1, ha->qla_initiate_recovery, 0, 0, 0, 0);
			return (-1);
		}

		data = READ_REG32(ha, Q8_FW_MBOX_CNTRL);

		if ((data & 0x3) == 1) {
			data = READ_REG32(ha, Q8_FW_MBOX0);
			if ((data & 0xF000) != 0x8000)
				break;
		}
		if (no_pause) {
			DELAY(1000);
		} else {
			qla_mdelay(__func__, 1);
		}
		i--;
	}
	if (i == 0) {
		device_printf(ha->pci_dev, "%s: fw_mbx_cntrl 0x%08x\n",
			__func__, data);
		ql_sp_log(ha, 5, 1, data, 0, 0, 0, 0);
		ret = -2;
		ha->hw.mbx_comp_msecs[(Q8_MBX_COMP_MSECS - 1)]++;
		QL_INITIATE_RECOVERY(ha);
		goto exit_qla_mbx_cmd;
	}

	for (i = 0; i < n_fwmbox; i++) {

		if (ha->qla_initiate_recovery) {
			ql_sp_log(ha, 6, 1, ha->qla_initiate_recovery, 0, 0, 0, 0);
			return (-1);
		}

		*fw_mbox++ = READ_REG32(ha, (Q8_FW_MBOX0 + (i << 2)));
	}

	WRITE_REG32(ha, Q8_FW_MBOX_CNTRL, 0x0);
	WRITE_REG32(ha, ha->hw.mbx_intr_mask_offset, 0x0);

	end_usecs = qla_get_usec_timestamp();

	if (end_usecs > start_usecs) {
		msecs_200 = (end_usecs - start_usecs)/(1000 * 200);

		if (msecs_200 < 15) 
			ha->hw.mbx_comp_msecs[msecs_200]++;
		else if (msecs_200 < 20)
			ha->hw.mbx_comp_msecs[15]++;
		else {
			device_printf(ha->pci_dev, "%s: [%ld, %ld] %ld\n", __func__,
				start_usecs, end_usecs, msecs_200);
			ha->hw.mbx_comp_msecs[16]++;
		}
	}
	ql_sp_log(ha, 7, 5, fw_mbox[0], fw_mbox[1], fw_mbox[2], fw_mbox[3], fw_mbox[4]);


exit_qla_mbx_cmd:
	return (ret);
}

int
qla_get_nic_partition(qla_host_t *ha, uint32_t *supports_9kb,
	uint32_t *num_rcvq)
{
	uint32_t *mbox, err;
	device_t dev = ha->pci_dev;

	bzero(ha->hw.mbox, (sizeof (uint32_t) * Q8_NUM_MBOX));

	mbox = ha->hw.mbox;

	mbox[0] = Q8_MBX_GET_NIC_PARTITION | (0x2 << 16) | (0x2 << 29);	

	if (qla_mbx_cmd(ha, mbox, 2, mbox, 19, 0)) {
		device_printf(dev, "%s: failed0\n", __func__);
		return (-1);
	}
	err = mbox[0] >> 25; 

	if (supports_9kb != NULL) {
		if (mbox[16] & 0x80) /* bit 7 of mbox 16 */
			*supports_9kb = 1;
		else
			*supports_9kb = 0;
	}

	if (num_rcvq != NULL)
		*num_rcvq =  ((mbox[6] >> 16) & 0xFFFF);

	if ((err != 1) && (err != 0)) {
		device_printf(dev, "%s: failed1 [0x%08x]\n", __func__, err);
		return (-1);
	}
	return 0;
}

static int
qla_config_intr_cntxt(qla_host_t *ha, uint32_t start_idx, uint32_t num_intrs,
	uint32_t create)
{
	uint32_t i, err;
	device_t dev = ha->pci_dev;
	q80_config_intr_t *c_intr;
	q80_config_intr_rsp_t *c_intr_rsp;

	c_intr = (q80_config_intr_t *)ha->hw.mbox;
	bzero(c_intr, (sizeof (q80_config_intr_t)));

	c_intr->opcode = Q8_MBX_CONFIG_INTR;

	c_intr->count_version = (sizeof (q80_config_intr_t) >> 2);
	c_intr->count_version |= Q8_MBX_CMD_VERSION;

	c_intr->nentries = num_intrs;

	for (i = 0; i < num_intrs; i++) {
		if (create) {
			c_intr->intr[i].cmd_type = Q8_MBX_CONFIG_INTR_CREATE;
			c_intr->intr[i].msix_index = start_idx + 1 + i;
		} else {
			c_intr->intr[i].cmd_type = Q8_MBX_CONFIG_INTR_DELETE;
			c_intr->intr[i].msix_index =
				ha->hw.intr_id[(start_idx + i)];
		}

		c_intr->intr[i].cmd_type |= Q8_MBX_CONFIG_INTR_TYPE_MSI_X;
	}

	if (qla_mbx_cmd(ha, (uint32_t *)c_intr,
		(sizeof (q80_config_intr_t) >> 2),
		ha->hw.mbox, (sizeof (q80_config_intr_rsp_t) >> 2), 0)) {
		device_printf(dev, "%s: %s failed0\n", __func__,
			(create ? "create" : "delete"));
		return (-1);
	}

	c_intr_rsp = (q80_config_intr_rsp_t *)ha->hw.mbox;

	err = Q8_MBX_RSP_STATUS(c_intr_rsp->regcnt_status);

	if (err) {
		device_printf(dev, "%s: %s failed1 [0x%08x, %d]\n", __func__,
			(create ? "create" : "delete"), err, c_intr_rsp->nentries);

		for (i = 0; i < c_intr_rsp->nentries; i++) {
			device_printf(dev, "%s: [%d]:[0x%x 0x%x 0x%x]\n",
				__func__, i, 
				c_intr_rsp->intr[i].status,
				c_intr_rsp->intr[i].intr_id,
				c_intr_rsp->intr[i].intr_src);
		}

		return (-1);
	}

	for (i = 0; ((i < num_intrs) && create); i++) {
		if (!c_intr_rsp->intr[i].status) {
			ha->hw.intr_id[(start_idx + i)] =
				c_intr_rsp->intr[i].intr_id;
			ha->hw.intr_src[(start_idx + i)] =
				c_intr_rsp->intr[i].intr_src;
		}
	}

	return (0);
}

/*
 * Name: qla_config_rss
 * Function: Configure RSS for the context/interface.
 */
static const uint64_t rss_key[] = { 0xbeac01fa6a42b73bULL,
			0x8030f20c77cb2da3ULL,
			0xae7b30b4d0ca2bcbULL, 0x43a38fb04167253dULL,
			0x255b0ec26d5a56daULL };

static int
qla_config_rss(qla_host_t *ha, uint16_t cntxt_id)
{
	q80_config_rss_t	*c_rss;
	q80_config_rss_rsp_t	*c_rss_rsp;
	uint32_t		err, i;
	device_t		dev = ha->pci_dev;

	c_rss = (q80_config_rss_t *)ha->hw.mbox;
	bzero(c_rss, (sizeof (q80_config_rss_t)));

	c_rss->opcode = Q8_MBX_CONFIG_RSS;

	c_rss->count_version = (sizeof (q80_config_rss_t) >> 2);
	c_rss->count_version |= Q8_MBX_CMD_VERSION;

	c_rss->hash_type = (Q8_MBX_RSS_HASH_TYPE_IPV4_TCP_IP |
				Q8_MBX_RSS_HASH_TYPE_IPV6_TCP_IP);
	//c_rss->hash_type = (Q8_MBX_RSS_HASH_TYPE_IPV4_TCP |
	//			Q8_MBX_RSS_HASH_TYPE_IPV6_TCP);

	c_rss->flags = Q8_MBX_RSS_FLAGS_ENABLE_RSS;
	c_rss->flags |= Q8_MBX_RSS_FLAGS_USE_IND_TABLE;

	c_rss->indtbl_mask = Q8_MBX_RSS_INDTBL_MASK;

	c_rss->indtbl_mask |= Q8_MBX_RSS_FLAGS_MULTI_RSS_VALID;
	c_rss->flags |= Q8_MBX_RSS_FLAGS_TYPE_CRSS;

	c_rss->cntxt_id = cntxt_id;

	for (i = 0; i < 5; i++) {
		c_rss->rss_key[i] = rss_key[i];
	}

	if (qla_mbx_cmd(ha, (uint32_t *)c_rss,
		(sizeof (q80_config_rss_t) >> 2),
		ha->hw.mbox, (sizeof(q80_config_rss_rsp_t) >> 2), 0)) {
		device_printf(dev, "%s: failed0\n", __func__);
		return (-1);
	}
	c_rss_rsp = (q80_config_rss_rsp_t *)ha->hw.mbox;

	err = Q8_MBX_RSP_STATUS(c_rss_rsp->regcnt_status);

	if (err) {
		device_printf(dev, "%s: failed1 [0x%08x]\n", __func__, err);
		return (-1);
	}
	return 0;
}

static int
qla_set_rss_ind_table(qla_host_t *ha, uint32_t start_idx, uint32_t count,
        uint16_t cntxt_id, uint8_t *ind_table)
{
        q80_config_rss_ind_table_t      *c_rss_ind;
        q80_config_rss_ind_table_rsp_t  *c_rss_ind_rsp;
        uint32_t                        err;
        device_t                        dev = ha->pci_dev;

	if ((count > Q8_RSS_IND_TBL_SIZE) ||
		((start_idx + count - 1) > Q8_RSS_IND_TBL_MAX_IDX)) {
		device_printf(dev, "%s: illegal count [%d, %d]\n", __func__,
			start_idx, count);
		return (-1);
	}

        c_rss_ind = (q80_config_rss_ind_table_t *)ha->hw.mbox;
        bzero(c_rss_ind, sizeof (q80_config_rss_ind_table_t));

        c_rss_ind->opcode = Q8_MBX_CONFIG_RSS_TABLE;
        c_rss_ind->count_version = (sizeof (q80_config_rss_ind_table_t) >> 2);
        c_rss_ind->count_version |= Q8_MBX_CMD_VERSION;

	c_rss_ind->start_idx = start_idx;
	c_rss_ind->end_idx = start_idx + count - 1;
	c_rss_ind->cntxt_id = cntxt_id;
	bcopy(ind_table, c_rss_ind->ind_table, count);

	if (qla_mbx_cmd(ha, (uint32_t *)c_rss_ind,
		(sizeof (q80_config_rss_ind_table_t) >> 2), ha->hw.mbox,
		(sizeof(q80_config_rss_ind_table_rsp_t) >> 2), 0)) {
		device_printf(dev, "%s: failed0\n", __func__);
		return (-1);
	}

	c_rss_ind_rsp = (q80_config_rss_ind_table_rsp_t *)ha->hw.mbox;
	err = Q8_MBX_RSP_STATUS(c_rss_ind_rsp->regcnt_status);

	if (err) {
		device_printf(dev, "%s: failed1 [0x%08x]\n", __func__, err);
		return (-1);
	}
	return 0;
}

/*
 * Name: qla_config_intr_coalesce
 * Function: Configure Interrupt Coalescing.
 */
static int
qla_config_intr_coalesce(qla_host_t *ha, uint16_t cntxt_id, int tenable,
	int rcv)
{
	q80_config_intr_coalesc_t	*intrc;
	q80_config_intr_coalesc_rsp_t	*intrc_rsp;
	uint32_t			err, i;
	device_t			dev = ha->pci_dev;
	
	intrc = (q80_config_intr_coalesc_t *)ha->hw.mbox;
	bzero(intrc, (sizeof (q80_config_intr_coalesc_t)));

	intrc->opcode = Q8_MBX_CONFIG_INTR_COALESCE;
	intrc->count_version = (sizeof (q80_config_intr_coalesc_t) >> 2);
	intrc->count_version |= Q8_MBX_CMD_VERSION;

	if (rcv) {
		intrc->flags = Q8_MBX_INTRC_FLAGS_RCV;
		intrc->max_pkts = ha->hw.rcv_intr_coalesce & 0xFFFF;
		intrc->max_mswait = (ha->hw.rcv_intr_coalesce >> 16) & 0xFFFF;
	} else {
		intrc->flags = Q8_MBX_INTRC_FLAGS_XMT;
		intrc->max_pkts = ha->hw.xmt_intr_coalesce & 0xFFFF;
		intrc->max_mswait = (ha->hw.xmt_intr_coalesce >> 16) & 0xFFFF;
	}

	intrc->cntxt_id = cntxt_id;

	if (tenable) {
		intrc->flags |= Q8_MBX_INTRC_FLAGS_PERIODIC;
		intrc->timer_type = Q8_MBX_INTRC_TIMER_PERIODIC;

		for (i = 0; i < ha->hw.num_sds_rings; i++) {
			intrc->sds_ring_mask |= (1 << i);
		}
		intrc->ms_timeout = 1000;
	}

	if (qla_mbx_cmd(ha, (uint32_t *)intrc,
		(sizeof (q80_config_intr_coalesc_t) >> 2),
		ha->hw.mbox, (sizeof(q80_config_intr_coalesc_rsp_t) >> 2), 0)) {
		device_printf(dev, "%s: failed0\n", __func__);
		return (-1);
	}
	intrc_rsp = (q80_config_intr_coalesc_rsp_t *)ha->hw.mbox;

	err = Q8_MBX_RSP_STATUS(intrc_rsp->regcnt_status);

	if (err) {
		device_printf(dev, "%s: failed1 [0x%08x]\n", __func__, err);
		return (-1);
	}
	
	return 0;
}


/*
 * Name: qla_config_mac_addr
 * Function: binds a MAC address to the context/interface.
 *	Can be unicast, multicast or broadcast.
 */
static int
qla_config_mac_addr(qla_host_t *ha, uint8_t *mac_addr, uint32_t add_mac,
	uint32_t num_mac)
{
	q80_config_mac_addr_t		*cmac;
	q80_config_mac_addr_rsp_t	*cmac_rsp;
	uint32_t			err;
	device_t			dev = ha->pci_dev;
	int				i;
	uint8_t				*mac_cpy = mac_addr;

	if (num_mac > Q8_MAX_MAC_ADDRS) {
		device_printf(dev, "%s: %s num_mac [0x%x] > Q8_MAX_MAC_ADDRS\n",
			__func__, (add_mac ? "Add" : "Del"), num_mac);
		return (-1);
	}

	cmac = (q80_config_mac_addr_t *)ha->hw.mbox;
	bzero(cmac, (sizeof (q80_config_mac_addr_t)));

	cmac->opcode = Q8_MBX_CONFIG_MAC_ADDR;
	cmac->count_version = sizeof (q80_config_mac_addr_t) >> 2;
	cmac->count_version |= Q8_MBX_CMD_VERSION;

	if (add_mac) 
		cmac->cmd = Q8_MBX_CMAC_CMD_ADD_MAC_ADDR;
	else
		cmac->cmd = Q8_MBX_CMAC_CMD_DEL_MAC_ADDR;
		
	cmac->cmd |= Q8_MBX_CMAC_CMD_CAM_INGRESS;

	cmac->nmac_entries = num_mac;
	cmac->cntxt_id = ha->hw.rcv_cntxt_id;

	for (i = 0; i < num_mac; i++) {
		bcopy(mac_addr, cmac->mac_addr[i].addr, Q8_ETHER_ADDR_LEN); 
		mac_addr = mac_addr + ETHER_ADDR_LEN;
	}

	if (qla_mbx_cmd(ha, (uint32_t *)cmac,
		(sizeof (q80_config_mac_addr_t) >> 2),
		ha->hw.mbox, (sizeof(q80_config_mac_addr_rsp_t) >> 2), 1)) {
		device_printf(dev, "%s: %s failed0\n", __func__,
			(add_mac ? "Add" : "Del"));
		return (-1);
	}
	cmac_rsp = (q80_config_mac_addr_rsp_t *)ha->hw.mbox;

	err = Q8_MBX_RSP_STATUS(cmac_rsp->regcnt_status);

	if (err) {
		device_printf(dev, "%s: %s failed1 [0x%08x]\n", __func__,
			(add_mac ? "Add" : "Del"), err);
		for (i = 0; i < num_mac; i++) {
			device_printf(dev, "%s: %02x:%02x:%02x:%02x:%02x:%02x\n",
				__func__, mac_cpy[0], mac_cpy[1], mac_cpy[2],
				mac_cpy[3], mac_cpy[4], mac_cpy[5]);
			mac_cpy += ETHER_ADDR_LEN;
		}
		return (-1);
	}
	
	return 0;
}


/*
 * Name: qla_set_mac_rcv_mode
 * Function: Enable/Disable AllMulticast and Promiscous Modes.
 */
static int
qla_set_mac_rcv_mode(qla_host_t *ha, uint32_t mode)
{
	q80_config_mac_rcv_mode_t	*rcv_mode;
	uint32_t			err;
	q80_config_mac_rcv_mode_rsp_t	*rcv_mode_rsp;
	device_t			dev = ha->pci_dev;

	rcv_mode = (q80_config_mac_rcv_mode_t *)ha->hw.mbox;
	bzero(rcv_mode, (sizeof (q80_config_mac_rcv_mode_t)));

	rcv_mode->opcode = Q8_MBX_CONFIG_MAC_RX_MODE;
	rcv_mode->count_version = sizeof (q80_config_mac_rcv_mode_t) >> 2;
	rcv_mode->count_version |= Q8_MBX_CMD_VERSION;

	rcv_mode->mode = mode;

	rcv_mode->cntxt_id = ha->hw.rcv_cntxt_id;

	if (qla_mbx_cmd(ha, (uint32_t *)rcv_mode,
		(sizeof (q80_config_mac_rcv_mode_t) >> 2),
		ha->hw.mbox, (sizeof(q80_config_mac_rcv_mode_rsp_t) >> 2), 1)) {
		device_printf(dev, "%s: failed0\n", __func__);
		return (-1);
	}
	rcv_mode_rsp = (q80_config_mac_rcv_mode_rsp_t *)ha->hw.mbox;

	err = Q8_MBX_RSP_STATUS(rcv_mode_rsp->regcnt_status);

	if (err) {
		device_printf(dev, "%s: failed1 [0x%08x]\n", __func__, err);
		return (-1);
	}
	
	return 0;
}

int
ql_set_promisc(qla_host_t *ha)
{
	int ret;

	ha->hw.mac_rcv_mode |= Q8_MBX_MAC_RCV_PROMISC_ENABLE;
	ret = qla_set_mac_rcv_mode(ha, ha->hw.mac_rcv_mode);
	return (ret);
}

void
qla_reset_promisc(qla_host_t *ha)
{
	ha->hw.mac_rcv_mode &= ~Q8_MBX_MAC_RCV_PROMISC_ENABLE;
	(void)qla_set_mac_rcv_mode(ha, ha->hw.mac_rcv_mode);
}

int
ql_set_allmulti(qla_host_t *ha)
{
	int ret;

	ha->hw.mac_rcv_mode |= Q8_MBX_MAC_ALL_MULTI_ENABLE;
	ret = qla_set_mac_rcv_mode(ha, ha->hw.mac_rcv_mode);
	return (ret);
}

void
qla_reset_allmulti(qla_host_t *ha)
{
	ha->hw.mac_rcv_mode &= ~Q8_MBX_MAC_ALL_MULTI_ENABLE;
	(void)qla_set_mac_rcv_mode(ha, ha->hw.mac_rcv_mode);
}

/*
 * Name: ql_set_max_mtu
 * Function:
 *	Sets the maximum transfer unit size for the specified rcv context.
 */
int
ql_set_max_mtu(qla_host_t *ha, uint32_t mtu, uint16_t cntxt_id)
{
	device_t		dev;
	q80_set_max_mtu_t	*max_mtu;
	q80_set_max_mtu_rsp_t	*max_mtu_rsp;
	uint32_t		err;

	dev = ha->pci_dev;

	max_mtu = (q80_set_max_mtu_t *)ha->hw.mbox;
	bzero(max_mtu, (sizeof (q80_set_max_mtu_t)));

	max_mtu->opcode = Q8_MBX_SET_MAX_MTU;
	max_mtu->count_version = (sizeof (q80_set_max_mtu_t) >> 2);
	max_mtu->count_version |= Q8_MBX_CMD_VERSION;

	max_mtu->cntxt_id = cntxt_id;
	max_mtu->mtu = mtu;

        if (qla_mbx_cmd(ha, (uint32_t *)max_mtu,
		(sizeof (q80_set_max_mtu_t) >> 2),
                ha->hw.mbox, (sizeof (q80_set_max_mtu_rsp_t) >> 2), 1)) {
                device_printf(dev, "%s: failed\n", __func__);
                return -1;
        }

	max_mtu_rsp = (q80_set_max_mtu_rsp_t *)ha->hw.mbox;

        err = Q8_MBX_RSP_STATUS(max_mtu_rsp->regcnt_status);

        if (err) {
                device_printf(dev, "%s: failed [0x%08x]\n", __func__, err);
        }

	return 0;
}

static int
qla_link_event_req(qla_host_t *ha, uint16_t cntxt_id)
{
	device_t		dev;
	q80_link_event_t	*lnk;
	q80_link_event_rsp_t	*lnk_rsp;
	uint32_t		err;

	dev = ha->pci_dev;

	lnk = (q80_link_event_t *)ha->hw.mbox;
	bzero(lnk, (sizeof (q80_link_event_t)));

	lnk->opcode = Q8_MBX_LINK_EVENT_REQ;
	lnk->count_version = (sizeof (q80_link_event_t) >> 2);
	lnk->count_version |= Q8_MBX_CMD_VERSION;

	lnk->cntxt_id = cntxt_id;
	lnk->cmd = Q8_LINK_EVENT_CMD_ENABLE_ASYNC;

        if (qla_mbx_cmd(ha, (uint32_t *)lnk, (sizeof (q80_link_event_t) >> 2),
                ha->hw.mbox, (sizeof (q80_link_event_rsp_t) >> 2), 0)) {
                device_printf(dev, "%s: failed\n", __func__);
                return -1;
        }

	lnk_rsp = (q80_link_event_rsp_t *)ha->hw.mbox;

        err = Q8_MBX_RSP_STATUS(lnk_rsp->regcnt_status);

        if (err) {
                device_printf(dev, "%s: failed [0x%08x]\n", __func__, err);
        }

	return 0;
}

static int
qla_config_fw_lro(qla_host_t *ha, uint16_t cntxt_id)
{
	device_t		dev;
	q80_config_fw_lro_t	*fw_lro;
	q80_config_fw_lro_rsp_t	*fw_lro_rsp;
	uint32_t		err;

	dev = ha->pci_dev;

	fw_lro = (q80_config_fw_lro_t *)ha->hw.mbox;
	bzero(fw_lro, sizeof(q80_config_fw_lro_t));

	fw_lro->opcode = Q8_MBX_CONFIG_FW_LRO;
	fw_lro->count_version = (sizeof (q80_config_fw_lro_t) >> 2);
	fw_lro->count_version |= Q8_MBX_CMD_VERSION;

	fw_lro->flags |= Q8_MBX_FW_LRO_IPV4 | Q8_MBX_FW_LRO_IPV4_WO_DST_IP_CHK;
	fw_lro->flags |= Q8_MBX_FW_LRO_IPV6 | Q8_MBX_FW_LRO_IPV6_WO_DST_IP_CHK;

	fw_lro->cntxt_id = cntxt_id;

	if (qla_mbx_cmd(ha, (uint32_t *)fw_lro,
		(sizeof (q80_config_fw_lro_t) >> 2),
		ha->hw.mbox, (sizeof (q80_config_fw_lro_rsp_t) >> 2), 0)) {
		device_printf(dev, "%s: failed\n", __func__);
		return -1;
	}

	fw_lro_rsp = (q80_config_fw_lro_rsp_t *)ha->hw.mbox;

	err = Q8_MBX_RSP_STATUS(fw_lro_rsp->regcnt_status);

	if (err) {
		device_printf(dev, "%s: failed [0x%08x]\n", __func__, err);
	}

	return 0;
}

static int
qla_set_cam_search_mode(qla_host_t *ha, uint32_t search_mode)
{
	device_t                dev;
	q80_hw_config_t         *hw_config;
	q80_hw_config_rsp_t     *hw_config_rsp;
	uint32_t                err;

	dev = ha->pci_dev;

	hw_config = (q80_hw_config_t *)ha->hw.mbox;
	bzero(hw_config, sizeof (q80_hw_config_t));

	hw_config->opcode = Q8_MBX_HW_CONFIG;
	hw_config->count_version = Q8_HW_CONFIG_SET_CAM_SEARCH_MODE_COUNT;
	hw_config->count_version |= Q8_MBX_CMD_VERSION;

	hw_config->cmd = Q8_HW_CONFIG_SET_CAM_SEARCH_MODE;

	hw_config->u.set_cam_search_mode.mode = search_mode;

	if (qla_mbx_cmd(ha, (uint32_t *)hw_config,
		(sizeof (q80_hw_config_t) >> 2),
		ha->hw.mbox, (sizeof (q80_hw_config_rsp_t) >> 2), 0)) {
		device_printf(dev, "%s: failed\n", __func__);
		return -1;
	}
	hw_config_rsp = (q80_hw_config_rsp_t *)ha->hw.mbox;

	err = Q8_MBX_RSP_STATUS(hw_config_rsp->regcnt_status);

	if (err) {
		device_printf(dev, "%s: failed [0x%08x]\n", __func__, err);
	}

	return 0;
}

static int
qla_get_cam_search_mode(qla_host_t *ha)
{
	device_t                dev;
	q80_hw_config_t         *hw_config;
	q80_hw_config_rsp_t     *hw_config_rsp;
	uint32_t                err;

	dev = ha->pci_dev;

	hw_config = (q80_hw_config_t *)ha->hw.mbox;
	bzero(hw_config, sizeof (q80_hw_config_t));

	hw_config->opcode = Q8_MBX_HW_CONFIG;
	hw_config->count_version = Q8_HW_CONFIG_GET_CAM_SEARCH_MODE_COUNT;
	hw_config->count_version |= Q8_MBX_CMD_VERSION;

	hw_config->cmd = Q8_HW_CONFIG_GET_CAM_SEARCH_MODE;

	if (qla_mbx_cmd(ha, (uint32_t *)hw_config,
		(sizeof (q80_hw_config_t) >> 2),
		ha->hw.mbox, (sizeof (q80_hw_config_rsp_t) >> 2), 0)) {
		device_printf(dev, "%s: failed\n", __func__);
		return -1;
	}
	hw_config_rsp = (q80_hw_config_rsp_t *)ha->hw.mbox;

	err = Q8_MBX_RSP_STATUS(hw_config_rsp->regcnt_status);

	if (err) {
		device_printf(dev, "%s: failed [0x%08x]\n", __func__, err);
	} else {
		device_printf(dev, "%s: cam search mode [0x%08x]\n", __func__,
			hw_config_rsp->u.get_cam_search_mode.mode);
	}

	return 0;
}

static int
qla_get_hw_stats(qla_host_t *ha, uint32_t cmd, uint32_t rsp_size)
{
	device_t		dev;
	q80_get_stats_t		*stat;
	q80_get_stats_rsp_t	*stat_rsp;
	uint32_t		err;

	dev = ha->pci_dev;

	stat = (q80_get_stats_t *)ha->hw.mbox;
	bzero(stat, (sizeof (q80_get_stats_t)));

	stat->opcode = Q8_MBX_GET_STATS;
	stat->count_version = 2;
	stat->count_version |= Q8_MBX_CMD_VERSION;

	stat->cmd = cmd;

        if (qla_mbx_cmd(ha, (uint32_t *)stat, 2,
                ha->hw.mbox, (rsp_size >> 2), 0)) {
                device_printf(dev, "%s: failed\n", __func__);
                return -1;
        }

	stat_rsp = (q80_get_stats_rsp_t *)ha->hw.mbox;

        err = Q8_MBX_RSP_STATUS(stat_rsp->regcnt_status);

        if (err) {
                return -1;
        }

	return 0;
}

void
ql_get_stats(qla_host_t *ha)
{
	q80_get_stats_rsp_t	*stat_rsp;
	q80_mac_stats_t		*mstat;
	q80_xmt_stats_t		*xstat;
	q80_rcv_stats_t		*rstat;
	uint32_t		cmd;
	int			i;
	struct ifnet *ifp = ha->ifp;

	if (ifp == NULL)
		return;

	if (QLA_LOCK(ha, __func__, QLA_LOCK_DEFAULT_MS_TIMEOUT, 0) != 0) {
		device_printf(ha->pci_dev, "%s: failed\n", __func__);
		return;
	}

	if (!(ifp->if_drv_flags & IFF_DRV_RUNNING)) {
		QLA_UNLOCK(ha, __func__);
		return;
	}

	stat_rsp = (q80_get_stats_rsp_t *)ha->hw.mbox;
	/*
	 * Get MAC Statistics
	 */
	cmd = Q8_GET_STATS_CMD_TYPE_MAC;
//	cmd |= Q8_GET_STATS_CMD_CLEAR;

	cmd |= ((ha->pci_func & 0x1) << 16);

	if (ha->qla_watchdog_pause || (!(ifp->if_drv_flags & IFF_DRV_RUNNING)) ||
		ha->offline)
		goto ql_get_stats_exit;

	if (qla_get_hw_stats(ha, cmd, sizeof (q80_get_stats_rsp_t)) == 0) {
		mstat = (q80_mac_stats_t *)&stat_rsp->u.mac;
		bcopy(mstat, &ha->hw.mac, sizeof(q80_mac_stats_t));
	} else {
                device_printf(ha->pci_dev, "%s: mac failed [0x%08x]\n",
			__func__, ha->hw.mbox[0]);
	}
	/*
	 * Get RCV Statistics
	 */
	cmd = Q8_GET_STATS_CMD_RCV | Q8_GET_STATS_CMD_TYPE_CNTXT;
//	cmd |= Q8_GET_STATS_CMD_CLEAR;
	cmd |= (ha->hw.rcv_cntxt_id << 16);

	if (ha->qla_watchdog_pause || (!(ifp->if_drv_flags & IFF_DRV_RUNNING)) ||
		ha->offline)
		goto ql_get_stats_exit;

	if (qla_get_hw_stats(ha, cmd, sizeof (q80_get_stats_rsp_t)) == 0) {
		rstat = (q80_rcv_stats_t *)&stat_rsp->u.rcv;
		bcopy(rstat, &ha->hw.rcv, sizeof(q80_rcv_stats_t));
	} else {
                device_printf(ha->pci_dev, "%s: rcv failed [0x%08x]\n",
			__func__, ha->hw.mbox[0]);
	}

	if (ha->qla_watchdog_pause || (!(ifp->if_drv_flags & IFF_DRV_RUNNING)) ||
		ha->offline)
		goto ql_get_stats_exit;
	/*
	 * Get XMT Statistics
	 */
	for (i = 0 ; (i < ha->hw.num_tx_rings); i++) {
		if (ha->qla_watchdog_pause ||
			(!(ifp->if_drv_flags & IFF_DRV_RUNNING)) ||
			ha->offline)
			goto ql_get_stats_exit;

		cmd = Q8_GET_STATS_CMD_XMT | Q8_GET_STATS_CMD_TYPE_CNTXT;
//		cmd |= Q8_GET_STATS_CMD_CLEAR;
		cmd |= (ha->hw.tx_cntxt[i].tx_cntxt_id << 16);

		if (qla_get_hw_stats(ha, cmd, sizeof(q80_get_stats_rsp_t))
			== 0) {
			xstat = (q80_xmt_stats_t *)&stat_rsp->u.xmt;
			bcopy(xstat, &ha->hw.xmt[i], sizeof(q80_xmt_stats_t));
		} else {
			device_printf(ha->pci_dev, "%s: xmt failed [0x%08x]\n",
				__func__, ha->hw.mbox[0]);
		}
	}

ql_get_stats_exit:
	QLA_UNLOCK(ha, __func__);

	return;
}

/*
 * Name: qla_tx_tso
 * Function: Checks if the packet to be transmitted is a candidate for
 *	Large TCP Segment Offload. If yes, the appropriate fields in the Tx
 *	Ring Structure are plugged in.
 */
static int
qla_tx_tso(qla_host_t *ha, struct mbuf *mp, q80_tx_cmd_t *tx_cmd, uint8_t *hdr)
{
	struct ether_vlan_header *eh;
	struct ip *ip = NULL;
	struct ip6_hdr *ip6 = NULL;
	struct tcphdr *th = NULL;
	uint32_t ehdrlen,  hdrlen, ip_hlen, tcp_hlen, tcp_opt_off;
	uint16_t etype, opcode, offload = 1;
	device_t dev;

	dev = ha->pci_dev;


	eh = mtod(mp, struct ether_vlan_header *);

	if (eh->evl_encap_proto == htons(ETHERTYPE_VLAN)) {
		ehdrlen = ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN;
		etype = ntohs(eh->evl_proto);
	} else {
		ehdrlen = ETHER_HDR_LEN;
		etype = ntohs(eh->evl_encap_proto);
	}

	hdrlen = 0;

	switch (etype) {
		case ETHERTYPE_IP:

			tcp_opt_off = ehdrlen + sizeof(struct ip) +
					sizeof(struct tcphdr);

			if (mp->m_len < tcp_opt_off) {
				m_copydata(mp, 0, tcp_opt_off, hdr);
				ip = (struct ip *)(hdr + ehdrlen);
			} else {
				ip = (struct ip *)(mp->m_data + ehdrlen);
			}

			ip_hlen = ip->ip_hl << 2;
			opcode = Q8_TX_CMD_OP_XMT_TCP_LSO;

				
			if ((ip->ip_p != IPPROTO_TCP) ||
				(ip_hlen != sizeof (struct ip))){
				/* IP Options are not supported */

				offload = 0;
			} else
				th = (struct tcphdr *)((caddr_t)ip + ip_hlen);

		break;

		case ETHERTYPE_IPV6:

			tcp_opt_off = ehdrlen + sizeof(struct ip6_hdr) +
					sizeof (struct tcphdr);

			if (mp->m_len < tcp_opt_off) {
				m_copydata(mp, 0, tcp_opt_off, hdr);
				ip6 = (struct ip6_hdr *)(hdr + ehdrlen);
			} else {
				ip6 = (struct ip6_hdr *)(mp->m_data + ehdrlen);
			}

			ip_hlen = sizeof(struct ip6_hdr);
			opcode = Q8_TX_CMD_OP_XMT_TCP_LSO_IPV6;

			if (ip6->ip6_nxt != IPPROTO_TCP) {
				//device_printf(dev, "%s: ipv6\n", __func__);
				offload = 0;
			} else
				th = (struct tcphdr *)((caddr_t)ip6 + ip_hlen);
		break;

		default:
			QL_DPRINT8(ha, (dev, "%s: type!=ip\n", __func__));
			offload = 0;
		break;
	}

	if (!offload)
		return (-1);

	tcp_hlen = th->th_off << 2;
	hdrlen = ehdrlen + ip_hlen + tcp_hlen;

        if (mp->m_len < hdrlen) {
                if (mp->m_len < tcp_opt_off) {
                        if (tcp_hlen > sizeof(struct tcphdr)) {
                                m_copydata(mp, tcp_opt_off,
                                        (tcp_hlen - sizeof(struct tcphdr)),
                                        &hdr[tcp_opt_off]);
                        }
                } else {
                        m_copydata(mp, 0, hdrlen, hdr);
                }
        }

	tx_cmd->mss = mp->m_pkthdr.tso_segsz;

	tx_cmd->flags_opcode = opcode ;
	tx_cmd->tcp_hdr_off = ip_hlen + ehdrlen;
	tx_cmd->total_hdr_len = hdrlen;

	/* Check for Multicast least significant bit of MSB == 1 */
	if (eh->evl_dhost[0] & 0x01) {
		tx_cmd->flags_opcode |= Q8_TX_CMD_FLAGS_MULTICAST;
	}

	if (mp->m_len < hdrlen) {
		printf("%d\n", hdrlen);
		return (1);
	}

	return (0);
}

/*
 * Name: qla_tx_chksum
 * Function: Checks if the packet to be transmitted is a candidate for
 *	TCP/UDP Checksum offload. If yes, the appropriate fields in the Tx
 *	Ring Structure are plugged in.
 */
static int
qla_tx_chksum(qla_host_t *ha, struct mbuf *mp, uint32_t *op_code,
	uint32_t *tcp_hdr_off)
{
	struct ether_vlan_header *eh;
	struct ip *ip;
	struct ip6_hdr *ip6;
	uint32_t ehdrlen, ip_hlen;
	uint16_t etype, opcode, offload = 1;
	device_t dev;
	uint8_t buf[sizeof(struct ip6_hdr)];

	dev = ha->pci_dev;

	*op_code = 0;

	if ((mp->m_pkthdr.csum_flags &
		(CSUM_TCP|CSUM_UDP|CSUM_TCP_IPV6 | CSUM_UDP_IPV6)) == 0)
		return (-1);

	eh = mtod(mp, struct ether_vlan_header *);

	if (eh->evl_encap_proto == htons(ETHERTYPE_VLAN)) {
		ehdrlen = ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN;
		etype = ntohs(eh->evl_proto);
	} else {
		ehdrlen = ETHER_HDR_LEN;
		etype = ntohs(eh->evl_encap_proto);
	}

		
	switch (etype) {
		case ETHERTYPE_IP:
			ip = (struct ip *)(mp->m_data + ehdrlen);

			ip_hlen = sizeof (struct ip);

			if (mp->m_len < (ehdrlen + ip_hlen)) {
				m_copydata(mp, ehdrlen, sizeof(struct ip), buf);
				ip = (struct ip *)buf;
			}

			if (ip->ip_p == IPPROTO_TCP)
				opcode = Q8_TX_CMD_OP_XMT_TCP_CHKSUM;
			else if (ip->ip_p == IPPROTO_UDP)
				opcode = Q8_TX_CMD_OP_XMT_UDP_CHKSUM;
			else {
				//device_printf(dev, "%s: ipv4\n", __func__);
				offload = 0;
			}
		break;

		case ETHERTYPE_IPV6:
			ip6 = (struct ip6_hdr *)(mp->m_data + ehdrlen);

			ip_hlen = sizeof(struct ip6_hdr);

			if (mp->m_len < (ehdrlen + ip_hlen)) {
				m_copydata(mp, ehdrlen, sizeof (struct ip6_hdr),
					buf);
				ip6 = (struct ip6_hdr *)buf;
			}

			if (ip6->ip6_nxt == IPPROTO_TCP)
				opcode = Q8_TX_CMD_OP_XMT_TCP_CHKSUM_IPV6;
			else if (ip6->ip6_nxt == IPPROTO_UDP)
				opcode = Q8_TX_CMD_OP_XMT_UDP_CHKSUM_IPV6;
			else {
				//device_printf(dev, "%s: ipv6\n", __func__);
				offload = 0;
			}
		break;

		default:
			offload = 0;
		break;
	}
	if (!offload)
		return (-1);

	*op_code = opcode;
	*tcp_hdr_off = (ip_hlen + ehdrlen);

	return (0);
}

#define QLA_TX_MIN_FREE 2
/*
 * Name: ql_hw_send
 * Function: Transmits a packet. It first checks if the packet is a
 *	candidate for Large TCP Segment Offload and then for UDP/TCP checksum
 *	offload. If either of these creteria are not met, it is transmitted
 *	as a regular ethernet frame.
 */
int
ql_hw_send(qla_host_t *ha, bus_dma_segment_t *segs, int nsegs,
	uint32_t tx_idx, struct mbuf *mp, uint32_t txr_idx, uint32_t iscsi_pdu)
{
	struct ether_vlan_header *eh;
	qla_hw_t *hw = &ha->hw;
	q80_tx_cmd_t *tx_cmd, tso_cmd;
	bus_dma_segment_t *c_seg;
	uint32_t num_tx_cmds, hdr_len = 0;
	uint32_t total_length = 0, bytes, tx_cmd_count = 0, txr_next;
	device_t dev;
	int i, ret;
	uint8_t *src = NULL, *dst = NULL;
	uint8_t frame_hdr[QL_FRAME_HDR_SIZE];
	uint32_t op_code = 0;
	uint32_t tcp_hdr_off = 0;

	dev = ha->pci_dev;

	/*
	 * Always make sure there is atleast one empty slot in the tx_ring
	 * tx_ring is considered full when there only one entry available
	 */
        num_tx_cmds = (nsegs + (Q8_TX_CMD_MAX_SEGMENTS - 1)) >> 2;

	total_length = mp->m_pkthdr.len;
	if (total_length > QLA_MAX_TSO_FRAME_SIZE) {
		device_printf(dev, "%s: total length exceeds maxlen(%d)\n",
			__func__, total_length);
		return (EINVAL);
	}
	eh = mtod(mp, struct ether_vlan_header *);

	if (mp->m_pkthdr.csum_flags & CSUM_TSO) {

		bzero((void *)&tso_cmd, sizeof(q80_tx_cmd_t));

		src = frame_hdr;
		ret = qla_tx_tso(ha, mp, &tso_cmd, src);

		if (!(ret & ~1)) {
			/* find the additional tx_cmd descriptors required */

			if (mp->m_flags & M_VLANTAG)
				tso_cmd.total_hdr_len += ETHER_VLAN_ENCAP_LEN;

			hdr_len = tso_cmd.total_hdr_len;

			bytes = sizeof(q80_tx_cmd_t) - Q8_TX_CMD_TSO_ALIGN;
			bytes = QL_MIN(bytes, hdr_len);

			num_tx_cmds++;
			hdr_len -= bytes;

			while (hdr_len) {
				bytes = QL_MIN((sizeof(q80_tx_cmd_t)), hdr_len);
				hdr_len -= bytes;
				num_tx_cmds++;
			}
			hdr_len = tso_cmd.total_hdr_len;

			if (ret == 0)
				src = (uint8_t *)eh;
		} else 
			return (EINVAL);
	} else {
		(void)qla_tx_chksum(ha, mp, &op_code, &tcp_hdr_off);
	}

	if (hw->tx_cntxt[txr_idx].txr_free <= (num_tx_cmds + QLA_TX_MIN_FREE)) {
		ql_hw_tx_done_locked(ha, txr_idx);
		if (hw->tx_cntxt[txr_idx].txr_free <=
				(num_tx_cmds + QLA_TX_MIN_FREE)) {
        		QL_DPRINT8(ha, (dev, "%s: (hw->txr_free <= "
				"(num_tx_cmds + QLA_TX_MIN_FREE))\n",
				__func__));
			return (-1);
		}
	}

	for (i = 0; i < num_tx_cmds; i++) {
		int j;

		j = (tx_idx+i) & (NUM_TX_DESCRIPTORS - 1);

		if (NULL != ha->tx_ring[txr_idx].tx_buf[j].m_head) {
			QL_ASSERT(ha, 0, \
				("%s [%d]: txr_idx = %d tx_idx = %d mbuf = %p\n",\
				__func__, __LINE__, txr_idx, j,\
				ha->tx_ring[txr_idx].tx_buf[j].m_head));
			return (EINVAL);
		}
	}

	tx_cmd = &hw->tx_cntxt[txr_idx].tx_ring_base[tx_idx];

        if (!(mp->m_pkthdr.csum_flags & CSUM_TSO)) {

                if (nsegs > ha->hw.max_tx_segs)
                        ha->hw.max_tx_segs = nsegs;

                bzero((void *)tx_cmd, sizeof(q80_tx_cmd_t));

                if (op_code) {
                        tx_cmd->flags_opcode = op_code;
                        tx_cmd->tcp_hdr_off = tcp_hdr_off;

                } else {
                        tx_cmd->flags_opcode = Q8_TX_CMD_OP_XMT_ETHER;
                }
	} else {
		bcopy(&tso_cmd, tx_cmd, sizeof(q80_tx_cmd_t));
		ha->tx_tso_frames++;
	}

	if (eh->evl_encap_proto == htons(ETHERTYPE_VLAN)) {
        	tx_cmd->flags_opcode |= Q8_TX_CMD_FLAGS_VLAN_TAGGED;

		if (iscsi_pdu)
			eh->evl_tag |= ha->hw.user_pri_iscsi << 13;

	} else if (mp->m_flags & M_VLANTAG) {

		if (hdr_len) { /* TSO */
			tx_cmd->flags_opcode |= (Q8_TX_CMD_FLAGS_VLAN_TAGGED |
						Q8_TX_CMD_FLAGS_HW_VLAN_ID);
			tx_cmd->tcp_hdr_off += ETHER_VLAN_ENCAP_LEN;
		} else
			tx_cmd->flags_opcode |= Q8_TX_CMD_FLAGS_HW_VLAN_ID;

		ha->hw_vlan_tx_frames++;
		tx_cmd->vlan_tci = mp->m_pkthdr.ether_vtag;

		if (iscsi_pdu) {
			tx_cmd->vlan_tci |= ha->hw.user_pri_iscsi << 13;
			mp->m_pkthdr.ether_vtag = tx_cmd->vlan_tci;
		}
	}


        tx_cmd->n_bufs = (uint8_t)nsegs;
        tx_cmd->data_len_lo = (uint8_t)(total_length & 0xFF);
        tx_cmd->data_len_hi = qla_host_to_le16(((uint16_t)(total_length >> 8)));
	tx_cmd->cntxtid = Q8_TX_CMD_PORT_CNXTID(ha->pci_func);

	c_seg = segs;

	while (1) {
		for (i = 0; ((i < Q8_TX_CMD_MAX_SEGMENTS) && nsegs); i++) {

			switch (i) {
			case 0:
				tx_cmd->buf1_addr = c_seg->ds_addr;
				tx_cmd->buf1_len = c_seg->ds_len;
				break;

			case 1:
				tx_cmd->buf2_addr = c_seg->ds_addr;
				tx_cmd->buf2_len = c_seg->ds_len;
				break;

			case 2:
				tx_cmd->buf3_addr = c_seg->ds_addr;
				tx_cmd->buf3_len = c_seg->ds_len;
				break;

			case 3:
				tx_cmd->buf4_addr = c_seg->ds_addr;
				tx_cmd->buf4_len = c_seg->ds_len;
				break;
			}

			c_seg++;
			nsegs--;
		}

		txr_next = hw->tx_cntxt[txr_idx].txr_next =
			(hw->tx_cntxt[txr_idx].txr_next + 1) &
				(NUM_TX_DESCRIPTORS - 1);
		tx_cmd_count++;

		if (!nsegs)
			break;
		
		tx_cmd = &hw->tx_cntxt[txr_idx].tx_ring_base[txr_next];
		bzero((void *)tx_cmd, sizeof(q80_tx_cmd_t));
	}

	if (mp->m_pkthdr.csum_flags & CSUM_TSO) {

		/* TSO : Copy the header in the following tx cmd descriptors */

		txr_next = hw->tx_cntxt[txr_idx].txr_next;

		tx_cmd = &hw->tx_cntxt[txr_idx].tx_ring_base[txr_next];
		bzero((void *)tx_cmd, sizeof(q80_tx_cmd_t));

		bytes = sizeof(q80_tx_cmd_t) - Q8_TX_CMD_TSO_ALIGN;
		bytes = QL_MIN(bytes, hdr_len);

		dst = (uint8_t *)tx_cmd + Q8_TX_CMD_TSO_ALIGN;

		if (mp->m_flags & M_VLANTAG) {
			/* first copy the src/dst MAC addresses */
			bcopy(src, dst, (ETHER_ADDR_LEN * 2));
			dst += (ETHER_ADDR_LEN * 2);
			src += (ETHER_ADDR_LEN * 2);
			
			*((uint16_t *)dst) = htons(ETHERTYPE_VLAN);
			dst += 2;
			*((uint16_t *)dst) = htons(mp->m_pkthdr.ether_vtag);
			dst += 2;

			/* bytes left in src header */
			hdr_len -= ((ETHER_ADDR_LEN * 2) +
					ETHER_VLAN_ENCAP_LEN);

			/* bytes left in TxCmd Entry */
			bytes -= ((ETHER_ADDR_LEN * 2) + ETHER_VLAN_ENCAP_LEN);


			bcopy(src, dst, bytes);
			src += bytes;
			hdr_len -= bytes;
		} else {
			bcopy(src, dst, bytes);
			src += bytes;
			hdr_len -= bytes;
		}

		txr_next = hw->tx_cntxt[txr_idx].txr_next =
				(hw->tx_cntxt[txr_idx].txr_next + 1) &
					(NUM_TX_DESCRIPTORS - 1);
		tx_cmd_count++;
		
		while (hdr_len) {
			tx_cmd = &hw->tx_cntxt[txr_idx].tx_ring_base[txr_next];
			bzero((void *)tx_cmd, sizeof(q80_tx_cmd_t));

			bytes = QL_MIN((sizeof(q80_tx_cmd_t)), hdr_len);

			bcopy(src, tx_cmd, bytes);
			src += bytes;
			hdr_len -= bytes;

			txr_next = hw->tx_cntxt[txr_idx].txr_next =
				(hw->tx_cntxt[txr_idx].txr_next + 1) &
					(NUM_TX_DESCRIPTORS - 1);
			tx_cmd_count++;
		}
	}

	hw->tx_cntxt[txr_idx].txr_free =
		hw->tx_cntxt[txr_idx].txr_free - tx_cmd_count;

	QL_UPDATE_TX_PRODUCER_INDEX(ha, hw->tx_cntxt[txr_idx].txr_next,\
		txr_idx);
       	QL_DPRINT8(ha, (dev, "%s: return\n", __func__));

	return (0);
}



#define Q8_CONFIG_IND_TBL_SIZE	32 /* < Q8_RSS_IND_TBL_SIZE and power of 2 */
static int
qla_config_rss_ind_table(qla_host_t *ha)
{
	uint32_t i, count;
	uint8_t rss_ind_tbl[Q8_CONFIG_IND_TBL_SIZE];


	for (i = 0; i < Q8_CONFIG_IND_TBL_SIZE; i++) {
		rss_ind_tbl[i] = i % ha->hw.num_sds_rings;
	}

	for (i = 0; i <= Q8_RSS_IND_TBL_MAX_IDX ;
		i = i + Q8_CONFIG_IND_TBL_SIZE) {

		if ((i + Q8_CONFIG_IND_TBL_SIZE) > Q8_RSS_IND_TBL_MAX_IDX) {
			count = Q8_RSS_IND_TBL_MAX_IDX - i + 1;
		} else {
			count = Q8_CONFIG_IND_TBL_SIZE;
		}

		if (qla_set_rss_ind_table(ha, i, count, ha->hw.rcv_cntxt_id,
			rss_ind_tbl))
			return (-1);
	}

	return (0);
}

static int
qla_config_soft_lro(qla_host_t *ha)
{
        int i;
        qla_hw_t *hw = &ha->hw;
        struct lro_ctrl *lro;

        for (i = 0; i < hw->num_sds_rings; i++) {
                lro = &hw->sds[i].lro;

		bzero(lro, sizeof(struct lro_ctrl));

#if (__FreeBSD_version >= 1100101)
                if (tcp_lro_init_args(lro, ha->ifp, 0, NUM_RX_DESCRIPTORS)) {
                        device_printf(ha->pci_dev,
				"%s: tcp_lro_init_args [%d] failed\n",
                                __func__, i);
                        return (-1);
                }
#else
                if (tcp_lro_init(lro)) {
                        device_printf(ha->pci_dev,
				"%s: tcp_lro_init [%d] failed\n",
                                __func__, i);
                        return (-1);
                }
#endif /* #if (__FreeBSD_version >= 1100101) */

                lro->ifp = ha->ifp;
        }

        QL_DPRINT2(ha, (ha->pci_dev, "%s: LRO initialized\n", __func__));
        return (0);
}

static void
qla_drain_soft_lro(qla_host_t *ha)
{
        int i;
        qla_hw_t *hw = &ha->hw;
        struct lro_ctrl *lro;

       	for (i = 0; i < hw->num_sds_rings; i++) {
               	lro = &hw->sds[i].lro;

#if (__FreeBSD_version >= 1100101)
		tcp_lro_flush_all(lro);
#else
                struct lro_entry *queued;

		while ((!SLIST_EMPTY(&lro->lro_active))) {
			queued = SLIST_FIRST(&lro->lro_active);
			SLIST_REMOVE_HEAD(&lro->lro_active, next);
			tcp_lro_flush(lro, queued);
		}
#endif /* #if (__FreeBSD_version >= 1100101) */
	}

	return;
}

static void
qla_free_soft_lro(qla_host_t *ha)
{
        int i;
        qla_hw_t *hw = &ha->hw;
        struct lro_ctrl *lro;

        for (i = 0; i < hw->num_sds_rings; i++) {
               	lro = &hw->sds[i].lro;
		tcp_lro_free(lro);
	}

	return;
}


/*
 * Name: ql_del_hw_if
 * Function: Destroys the hardware specific entities corresponding to an
 *	Ethernet Interface
 */
void
ql_del_hw_if(qla_host_t *ha)
{
	uint32_t i;
	uint32_t num_msix;

	(void)qla_stop_nic_func(ha);

	qla_del_rcv_cntxt(ha);

	if(qla_del_xmt_cntxt(ha))
		goto ql_del_hw_if_exit;

	if (ha->hw.flags.init_intr_cnxt) {
		for (i = 0; i < ha->hw.num_sds_rings; ) {

			if ((i + Q8_MAX_INTR_VECTORS) < ha->hw.num_sds_rings)
				num_msix = Q8_MAX_INTR_VECTORS;
			else
				num_msix = ha->hw.num_sds_rings - i;

			if (qla_config_intr_cntxt(ha, i, num_msix, 0))
				break;

			i += num_msix;
		}

		ha->hw.flags.init_intr_cnxt = 0;
	}

ql_del_hw_if_exit:
	if (ha->hw.enable_soft_lro) {
		qla_drain_soft_lro(ha);
		qla_free_soft_lro(ha);
	}

	return;
}

void
qla_confirm_9kb_enable(qla_host_t *ha)
{
//	uint32_t supports_9kb = 0;

	ha->hw.mbx_intr_mask_offset = READ_REG32(ha, Q8_MBOX_INT_MASK_MSIX);

	/* Use MSI-X vector 0; Enable Firmware Mailbox Interrupt */
	WRITE_REG32(ha, Q8_MBOX_INT_ENABLE, BIT_2);
	WRITE_REG32(ha, ha->hw.mbx_intr_mask_offset, 0x0);

#if 0
	qla_get_nic_partition(ha, &supports_9kb, NULL);

	if (!supports_9kb)
#endif
	ha->hw.enable_9kb = 0;

	return;
}

/*
 * Name: ql_init_hw_if
 * Function: Creates the hardware specific entities corresponding to an
 *	Ethernet Interface - Transmit and Receive Contexts. Sets the MAC Address
 *	corresponding to the interface. Enables LRO if allowed.
 */
int
ql_init_hw_if(qla_host_t *ha)
{
	device_t	dev;
	uint32_t	i;
	uint8_t		bcast_mac[6];
	qla_rdesc_t	*rdesc;
	uint32_t	num_msix;

	dev = ha->pci_dev;

	for (i = 0; i < ha->hw.num_sds_rings; i++) {
		bzero(ha->hw.dma_buf.sds_ring[i].dma_b,
			ha->hw.dma_buf.sds_ring[i].size);
	}

	for (i = 0; i < ha->hw.num_sds_rings; ) {

		if ((i + Q8_MAX_INTR_VECTORS) < ha->hw.num_sds_rings)
			num_msix = Q8_MAX_INTR_VECTORS;
		else
			num_msix = ha->hw.num_sds_rings - i;

		if (qla_config_intr_cntxt(ha, i, num_msix, 1)) {

			if (i > 0) {

				num_msix = i;

				for (i = 0; i < num_msix; ) {
					qla_config_intr_cntxt(ha, i,
						Q8_MAX_INTR_VECTORS, 0);
					i += Q8_MAX_INTR_VECTORS;
				}
			}
			return (-1);
		}

		i = i + num_msix;
	}

        ha->hw.flags.init_intr_cnxt = 1;

	/*
	 * Create Receive Context
	 */
	if (qla_init_rcv_cntxt(ha)) {
		return (-1);
	}

	for (i = 0; i < ha->hw.num_rds_rings; i++) {
		rdesc = &ha->hw.rds[i];
		rdesc->rx_next = NUM_RX_DESCRIPTORS - 2;
		rdesc->rx_in = 0;
		/* Update the RDS Producer Indices */
		QL_UPDATE_RDS_PRODUCER_INDEX(ha, rdesc->prod_std,\
			rdesc->rx_next);
	}

	/*
	 * Create Transmit Context
	 */
	if (qla_init_xmt_cntxt(ha)) {
		qla_del_rcv_cntxt(ha);
		return (-1);
	}
	ha->hw.max_tx_segs = 0;

	if (qla_config_mac_addr(ha, ha->hw.mac_addr, 1, 1))
		return(-1);

	ha->hw.flags.unicast_mac = 1;

	bcast_mac[0] = 0xFF; bcast_mac[1] = 0xFF; bcast_mac[2] = 0xFF;
	bcast_mac[3] = 0xFF; bcast_mac[4] = 0xFF; bcast_mac[5] = 0xFF;

	if (qla_config_mac_addr(ha, bcast_mac, 1, 1))
		return (-1);

	ha->hw.flags.bcast_mac = 1;

	/*
	 * program any cached multicast addresses
	 */
	if (qla_hw_add_all_mcast(ha))
		return (-1);

	if (ql_set_max_mtu(ha, ha->max_frame_size, ha->hw.rcv_cntxt_id))
		return (-1);

	if (qla_config_rss(ha, ha->hw.rcv_cntxt_id))
		return (-1);

	if (qla_config_rss_ind_table(ha))
		return (-1);

	if (qla_config_intr_coalesce(ha, ha->hw.rcv_cntxt_id, 0, 1))
		return (-1);

	if (qla_link_event_req(ha, ha->hw.rcv_cntxt_id))
		return (-1);

	if (ha->ifp->if_capenable & IFCAP_LRO) {
		if (ha->hw.enable_hw_lro) {
			ha->hw.enable_soft_lro = 0;

			if (qla_config_fw_lro(ha, ha->hw.rcv_cntxt_id))
				return (-1);
		} else {
			ha->hw.enable_soft_lro = 1;

			if (qla_config_soft_lro(ha))
				return (-1);
		}
	}

        if (qla_init_nic_func(ha))
                return (-1);

        if (qla_query_fw_dcbx_caps(ha))
                return (-1);

	for (i = 0; i < ha->hw.num_sds_rings; i++)
		QL_ENABLE_INTERRUPTS(ha, i);

	return (0);
}

static int
qla_map_sds_to_rds(qla_host_t *ha, uint32_t start_idx, uint32_t num_idx)
{
        device_t                dev = ha->pci_dev;
        q80_rq_map_sds_to_rds_t *map_rings;
	q80_rsp_map_sds_to_rds_t *map_rings_rsp;
        uint32_t                i, err;
        qla_hw_t                *hw = &ha->hw;

        map_rings = (q80_rq_map_sds_to_rds_t *)ha->hw.mbox;
        bzero(map_rings, sizeof(q80_rq_map_sds_to_rds_t));

        map_rings->opcode = Q8_MBX_MAP_SDS_TO_RDS;
        map_rings->count_version = (sizeof (q80_rq_map_sds_to_rds_t) >> 2);
        map_rings->count_version |= Q8_MBX_CMD_VERSION;

        map_rings->cntxt_id = hw->rcv_cntxt_id;
        map_rings->num_rings = num_idx;

	for (i = 0; i < num_idx; i++) {
		map_rings->sds_rds[i].sds_ring = i + start_idx;
		map_rings->sds_rds[i].rds_ring = i + start_idx;
	}

        if (qla_mbx_cmd(ha, (uint32_t *)map_rings,
                (sizeof (q80_rq_map_sds_to_rds_t) >> 2),
                ha->hw.mbox, (sizeof(q80_rsp_add_rcv_rings_t) >> 2), 0)) {
                device_printf(dev, "%s: failed0\n", __func__);
                return (-1);
        }

        map_rings_rsp = (q80_rsp_map_sds_to_rds_t *)ha->hw.mbox;

        err = Q8_MBX_RSP_STATUS(map_rings_rsp->regcnt_status);

        if (err) {
                device_printf(dev, "%s: failed1 [0x%08x]\n", __func__, err);
                return (-1);
        }

        return (0);
}

/*
 * Name: qla_init_rcv_cntxt
 * Function: Creates the Receive Context.
 */
static int
qla_init_rcv_cntxt(qla_host_t *ha)
{
	q80_rq_rcv_cntxt_t	*rcntxt;
	q80_rsp_rcv_cntxt_t	*rcntxt_rsp;
	q80_stat_desc_t		*sdesc;
	int			i, j;
        qla_hw_t		*hw = &ha->hw;
	device_t		dev;
	uint32_t		err;
	uint32_t		rcntxt_sds_rings;
	uint32_t		rcntxt_rds_rings;
	uint32_t		max_idx;

	dev = ha->pci_dev;

	/*
	 * Create Receive Context
	 */

	for (i = 0; i < hw->num_sds_rings; i++) {
		sdesc = (q80_stat_desc_t *)&hw->sds[i].sds_ring_base[0];

		for (j = 0; j < NUM_STATUS_DESCRIPTORS; j++) {
			sdesc->data[0] = 1ULL;
			sdesc->data[1] = 1ULL;
		}
	}

	rcntxt_sds_rings = hw->num_sds_rings;
	if (hw->num_sds_rings > MAX_RCNTXT_SDS_RINGS)
		rcntxt_sds_rings = MAX_RCNTXT_SDS_RINGS;

	rcntxt_rds_rings = hw->num_rds_rings;

	if (hw->num_rds_rings > MAX_RDS_RING_SETS)
		rcntxt_rds_rings = MAX_RDS_RING_SETS;

	rcntxt = (q80_rq_rcv_cntxt_t *)ha->hw.mbox;
	bzero(rcntxt, (sizeof (q80_rq_rcv_cntxt_t)));

	rcntxt->opcode = Q8_MBX_CREATE_RX_CNTXT;
	rcntxt->count_version = (sizeof (q80_rq_rcv_cntxt_t) >> 2);
	rcntxt->count_version |= Q8_MBX_CMD_VERSION;

	rcntxt->cap0 = Q8_RCV_CNTXT_CAP0_BASEFW |
			Q8_RCV_CNTXT_CAP0_LRO |
			Q8_RCV_CNTXT_CAP0_HW_LRO |
			Q8_RCV_CNTXT_CAP0_RSS |
			Q8_RCV_CNTXT_CAP0_SGL_LRO;

	if (ha->hw.enable_9kb)
		rcntxt->cap0 |= Q8_RCV_CNTXT_CAP0_SINGLE_JUMBO;
	else
		rcntxt->cap0 |= Q8_RCV_CNTXT_CAP0_SGL_JUMBO;

	if (ha->hw.num_rds_rings > 1) {
		rcntxt->nrds_sets_rings = rcntxt_rds_rings | (1 << 5);
		rcntxt->cap0 |= Q8_RCV_CNTXT_CAP0_MULTI_RDS;
	} else
		rcntxt->nrds_sets_rings = 0x1 | (1 << 5);

	rcntxt->nsds_rings = rcntxt_sds_rings;

	rcntxt->rds_producer_mode = Q8_RCV_CNTXT_RDS_PROD_MODE_UNIQUE;

	rcntxt->rcv_vpid = 0;

	for (i = 0; i <  rcntxt_sds_rings; i++) {
		rcntxt->sds[i].paddr =
			qla_host_to_le64(hw->dma_buf.sds_ring[i].dma_addr);
		rcntxt->sds[i].size =
			qla_host_to_le32(NUM_STATUS_DESCRIPTORS);
		rcntxt->sds[i].intr_id = qla_host_to_le16(hw->intr_id[i]);
		rcntxt->sds[i].intr_src_bit = qla_host_to_le16(0);
	}

	for (i = 0; i <  rcntxt_rds_rings; i++) {
		rcntxt->rds[i].paddr_std =
			qla_host_to_le64(hw->dma_buf.rds_ring[i].dma_addr);

		if (ha->hw.enable_9kb)
			rcntxt->rds[i].std_bsize =
				qla_host_to_le64(MJUM9BYTES);
		else
			rcntxt->rds[i].std_bsize = qla_host_to_le64(MCLBYTES);

		rcntxt->rds[i].std_nentries =
			qla_host_to_le32(NUM_RX_DESCRIPTORS);
	}

        if (qla_mbx_cmd(ha, (uint32_t *)rcntxt,
		(sizeof (q80_rq_rcv_cntxt_t) >> 2),
                ha->hw.mbox, (sizeof(q80_rsp_rcv_cntxt_t) >> 2), 0)) {
                device_printf(dev, "%s: failed0\n", __func__);
                return (-1);
        }

        rcntxt_rsp = (q80_rsp_rcv_cntxt_t *)ha->hw.mbox;

        err = Q8_MBX_RSP_STATUS(rcntxt_rsp->regcnt_status);

        if (err) {
                device_printf(dev, "%s: failed1 [0x%08x]\n", __func__, err);
                return (-1);
        }

	for (i = 0; i <  rcntxt_sds_rings; i++) {
		hw->sds[i].sds_consumer = rcntxt_rsp->sds_cons[i];
	}

	for (i = 0; i <  rcntxt_rds_rings; i++) {
		hw->rds[i].prod_std = rcntxt_rsp->rds[i].prod_std;
	}

	hw->rcv_cntxt_id = rcntxt_rsp->cntxt_id;

	ha->hw.flags.init_rx_cnxt = 1;

	if (hw->num_sds_rings > MAX_RCNTXT_SDS_RINGS) {

		for (i = MAX_RCNTXT_SDS_RINGS; i < hw->num_sds_rings;) {

			if ((i + MAX_RCNTXT_SDS_RINGS) < hw->num_sds_rings)
				max_idx = MAX_RCNTXT_SDS_RINGS;
			else
				max_idx = hw->num_sds_rings - i;

			err = qla_add_rcv_rings(ha, i, max_idx);
			if (err)
				return -1;

			i += max_idx;
		}
	}

	if (hw->num_rds_rings > 1) {

		for (i = 0; i < hw->num_rds_rings; ) {

			if ((i + MAX_SDS_TO_RDS_MAP) < hw->num_rds_rings)
				max_idx = MAX_SDS_TO_RDS_MAP;
			else
				max_idx = hw->num_rds_rings - i;

			err = qla_map_sds_to_rds(ha, i, max_idx);
			if (err)
				return -1;

			i += max_idx;
		}
	}

	return (0);
}

static int
qla_add_rcv_rings(qla_host_t *ha, uint32_t sds_idx, uint32_t nsds)
{
	device_t		dev = ha->pci_dev;
	q80_rq_add_rcv_rings_t	*add_rcv;
	q80_rsp_add_rcv_rings_t	*add_rcv_rsp;
	uint32_t		i,j, err;
        qla_hw_t		*hw = &ha->hw;

	add_rcv = (q80_rq_add_rcv_rings_t *)ha->hw.mbox;
	bzero(add_rcv, sizeof (q80_rq_add_rcv_rings_t));

	add_rcv->opcode = Q8_MBX_ADD_RX_RINGS;
	add_rcv->count_version = (sizeof (q80_rq_add_rcv_rings_t) >> 2);
	add_rcv->count_version |= Q8_MBX_CMD_VERSION;

	add_rcv->nrds_sets_rings = nsds | (1 << 5);
	add_rcv->nsds_rings = nsds;
	add_rcv->cntxt_id = hw->rcv_cntxt_id;

        for (i = 0; i <  nsds; i++) {

		j = i + sds_idx;

                add_rcv->sds[i].paddr =
                        qla_host_to_le64(hw->dma_buf.sds_ring[j].dma_addr);

                add_rcv->sds[i].size =
                        qla_host_to_le32(NUM_STATUS_DESCRIPTORS);

                add_rcv->sds[i].intr_id = qla_host_to_le16(hw->intr_id[j]);
                add_rcv->sds[i].intr_src_bit = qla_host_to_le16(0);

        }

        for (i = 0; (i <  nsds); i++) {
                j = i + sds_idx;

                add_rcv->rds[i].paddr_std =
                        qla_host_to_le64(hw->dma_buf.rds_ring[j].dma_addr);

		if (ha->hw.enable_9kb)
			add_rcv->rds[i].std_bsize =
				qla_host_to_le64(MJUM9BYTES);
		else
                	add_rcv->rds[i].std_bsize = qla_host_to_le64(MCLBYTES);

                add_rcv->rds[i].std_nentries =
                        qla_host_to_le32(NUM_RX_DESCRIPTORS);
        }


        if (qla_mbx_cmd(ha, (uint32_t *)add_rcv,
		(sizeof (q80_rq_add_rcv_rings_t) >> 2),
                ha->hw.mbox, (sizeof(q80_rsp_add_rcv_rings_t) >> 2), 0)) {
                device_printf(dev, "%s: failed0\n", __func__);
                return (-1);
        }

        add_rcv_rsp = (q80_rsp_add_rcv_rings_t *)ha->hw.mbox;

        err = Q8_MBX_RSP_STATUS(add_rcv_rsp->regcnt_status);

        if (err) {
                device_printf(dev, "%s: failed1 [0x%08x]\n", __func__, err);
                return (-1);
        }

	for (i = 0; i < nsds; i++) {
		hw->sds[(i + sds_idx)].sds_consumer = add_rcv_rsp->sds_cons[i];
	}

	for (i = 0; i < nsds; i++) {
		hw->rds[(i + sds_idx)].prod_std = add_rcv_rsp->rds[i].prod_std;
	}

	return (0);
}

/*
 * Name: qla_del_rcv_cntxt
 * Function: Destroys the Receive Context.
 */
static void
qla_del_rcv_cntxt(qla_host_t *ha)
{
	device_t			dev = ha->pci_dev;
	q80_rcv_cntxt_destroy_t		*rcntxt;
	q80_rcv_cntxt_destroy_rsp_t	*rcntxt_rsp;
	uint32_t			err;
	uint8_t				bcast_mac[6];

	if (!ha->hw.flags.init_rx_cnxt)
		return;

	if (qla_hw_del_all_mcast(ha))
		return;

	if (ha->hw.flags.bcast_mac) {

		bcast_mac[0] = 0xFF; bcast_mac[1] = 0xFF; bcast_mac[2] = 0xFF;
		bcast_mac[3] = 0xFF; bcast_mac[4] = 0xFF; bcast_mac[5] = 0xFF;

		if (qla_config_mac_addr(ha, bcast_mac, 0, 1))
			return;
		ha->hw.flags.bcast_mac = 0;

	}

	if (ha->hw.flags.unicast_mac) {
		if (qla_config_mac_addr(ha, ha->hw.mac_addr, 0, 1))
			return;
		ha->hw.flags.unicast_mac = 0;
	}

	rcntxt = (q80_rcv_cntxt_destroy_t *)ha->hw.mbox;
	bzero(rcntxt, (sizeof (q80_rcv_cntxt_destroy_t)));

	rcntxt->opcode = Q8_MBX_DESTROY_RX_CNTXT;
	rcntxt->count_version = (sizeof (q80_rcv_cntxt_destroy_t) >> 2);
	rcntxt->count_version |= Q8_MBX_CMD_VERSION;

	rcntxt->cntxt_id = ha->hw.rcv_cntxt_id;

        if (qla_mbx_cmd(ha, (uint32_t *)rcntxt,
		(sizeof (q80_rcv_cntxt_destroy_t) >> 2),
                ha->hw.mbox, (sizeof(q80_rcv_cntxt_destroy_rsp_t) >> 2), 0)) {
                device_printf(dev, "%s: failed0\n", __func__);
                return;
        }
        rcntxt_rsp = (q80_rcv_cntxt_destroy_rsp_t *)ha->hw.mbox;

        err = Q8_MBX_RSP_STATUS(rcntxt_rsp->regcnt_status);

        if (err) {
                device_printf(dev, "%s: failed1 [0x%08x]\n", __func__, err);
        }

	ha->hw.flags.init_rx_cnxt = 0;
	return;
}

/*
 * Name: qla_init_xmt_cntxt
 * Function: Creates the Transmit Context.
 */
static int
qla_init_xmt_cntxt_i(qla_host_t *ha, uint32_t txr_idx)
{
	device_t		dev;
        qla_hw_t		*hw = &ha->hw;
	q80_rq_tx_cntxt_t	*tcntxt;
	q80_rsp_tx_cntxt_t	*tcntxt_rsp;
	uint32_t		err;
	qla_hw_tx_cntxt_t       *hw_tx_cntxt;
	uint32_t		intr_idx;

	hw_tx_cntxt = &hw->tx_cntxt[txr_idx];

	dev = ha->pci_dev;

	/*
	 * Create Transmit Context
	 */
	tcntxt = (q80_rq_tx_cntxt_t *)ha->hw.mbox;
	bzero(tcntxt, (sizeof (q80_rq_tx_cntxt_t)));

	tcntxt->opcode = Q8_MBX_CREATE_TX_CNTXT;
	tcntxt->count_version = (sizeof (q80_rq_tx_cntxt_t) >> 2);
	tcntxt->count_version |= Q8_MBX_CMD_VERSION;

	intr_idx = txr_idx;

#ifdef QL_ENABLE_ISCSI_TLV

	tcntxt->cap0 = Q8_TX_CNTXT_CAP0_BASEFW | Q8_TX_CNTXT_CAP0_LSO |
				Q8_TX_CNTXT_CAP0_TC;

	if (txr_idx >= (ha->hw.num_tx_rings >> 1)) {
		tcntxt->traffic_class = 1;
	}

	intr_idx = txr_idx % (ha->hw.num_tx_rings >> 1);

#else
	tcntxt->cap0 = Q8_TX_CNTXT_CAP0_BASEFW | Q8_TX_CNTXT_CAP0_LSO;

#endif /* #ifdef QL_ENABLE_ISCSI_TLV */

	tcntxt->ntx_rings = 1;

	tcntxt->tx_ring[0].paddr =
		qla_host_to_le64(hw_tx_cntxt->tx_ring_paddr);
	tcntxt->tx_ring[0].tx_consumer =
		qla_host_to_le64(hw_tx_cntxt->tx_cons_paddr);
	tcntxt->tx_ring[0].nentries = qla_host_to_le16(NUM_TX_DESCRIPTORS);

	tcntxt->tx_ring[0].intr_id = qla_host_to_le16(hw->intr_id[intr_idx]);
	tcntxt->tx_ring[0].intr_src_bit = qla_host_to_le16(0);

	hw_tx_cntxt->txr_free = NUM_TX_DESCRIPTORS;
	hw_tx_cntxt->txr_next = hw_tx_cntxt->txr_comp = 0;
	*(hw_tx_cntxt->tx_cons) = 0;

        if (qla_mbx_cmd(ha, (uint32_t *)tcntxt,
		(sizeof (q80_rq_tx_cntxt_t) >> 2),
                ha->hw.mbox,
		(sizeof(q80_rsp_tx_cntxt_t) >> 2), 0)) {
                device_printf(dev, "%s: failed0\n", __func__);
                return (-1);
        }
        tcntxt_rsp = (q80_rsp_tx_cntxt_t *)ha->hw.mbox;

        err = Q8_MBX_RSP_STATUS(tcntxt_rsp->regcnt_status);

        if (err) {
                device_printf(dev, "%s: failed1 [0x%08x]\n", __func__, err);
		return -1;
        }

	hw_tx_cntxt->tx_prod_reg = tcntxt_rsp->tx_ring[0].prod_index;
	hw_tx_cntxt->tx_cntxt_id = tcntxt_rsp->tx_ring[0].cntxt_id;

	if (qla_config_intr_coalesce(ha, hw_tx_cntxt->tx_cntxt_id, 0, 0))
		return (-1);

	return (0);
}


/*
 * Name: qla_del_xmt_cntxt
 * Function: Destroys the Transmit Context.
 */
static int
qla_del_xmt_cntxt_i(qla_host_t *ha, uint32_t txr_idx)
{
	device_t			dev = ha->pci_dev;
	q80_tx_cntxt_destroy_t		*tcntxt;
	q80_tx_cntxt_destroy_rsp_t	*tcntxt_rsp;
	uint32_t			err;

	tcntxt = (q80_tx_cntxt_destroy_t *)ha->hw.mbox;
	bzero(tcntxt, (sizeof (q80_tx_cntxt_destroy_t)));

	tcntxt->opcode = Q8_MBX_DESTROY_TX_CNTXT;
	tcntxt->count_version = (sizeof (q80_tx_cntxt_destroy_t) >> 2);
	tcntxt->count_version |= Q8_MBX_CMD_VERSION;

	tcntxt->cntxt_id = ha->hw.tx_cntxt[txr_idx].tx_cntxt_id;

        if (qla_mbx_cmd(ha, (uint32_t *)tcntxt,
		(sizeof (q80_tx_cntxt_destroy_t) >> 2),
                ha->hw.mbox, (sizeof (q80_tx_cntxt_destroy_rsp_t) >> 2), 0)) {
                device_printf(dev, "%s: failed0\n", __func__);
                return (-1);
        }
        tcntxt_rsp = (q80_tx_cntxt_destroy_rsp_t *)ha->hw.mbox;

        err = Q8_MBX_RSP_STATUS(tcntxt_rsp->regcnt_status);

        if (err) {
                device_printf(dev, "%s: failed1 [0x%08x]\n", __func__, err);
		return (-1);
        }

	return (0);
}
static int
qla_del_xmt_cntxt(qla_host_t *ha)
{
	uint32_t i;
	int ret = 0;

	if (!ha->hw.flags.init_tx_cnxt)
		return (ret);

	for (i = 0; i < ha->hw.num_tx_rings; i++) {
		if ((ret = qla_del_xmt_cntxt_i(ha, i)) != 0)
			break;
	}
	ha->hw.flags.init_tx_cnxt = 0;

	return (ret);
}

static int
qla_init_xmt_cntxt(qla_host_t *ha)
{
	uint32_t i, j;

	for (i = 0; i < ha->hw.num_tx_rings; i++) {
		if (qla_init_xmt_cntxt_i(ha, i) != 0) {
			for (j = 0; j < i; j++) {
				if (qla_del_xmt_cntxt_i(ha, j))
					break;
			}
			return (-1);
		}
	}
	ha->hw.flags.init_tx_cnxt = 1;
	return (0);
}

static int
qla_hw_all_mcast(qla_host_t *ha, uint32_t add_mcast)
{
	int i, nmcast;
	uint32_t count = 0;
	uint8_t *mcast;

	nmcast = ha->hw.nmcast;

	QL_DPRINT2(ha, (ha->pci_dev,
		"%s:[0x%x] enter nmcast = %d \n", __func__, add_mcast, nmcast));

	mcast = ha->hw.mac_addr_arr;
	memset(mcast, 0, (Q8_MAX_MAC_ADDRS * ETHER_ADDR_LEN));

	for (i = 0 ; ((i < Q8_MAX_NUM_MULTICAST_ADDRS) && nmcast); i++) {
		if ((ha->hw.mcast[i].addr[0] != 0) || 
			(ha->hw.mcast[i].addr[1] != 0) ||
			(ha->hw.mcast[i].addr[2] != 0) ||
			(ha->hw.mcast[i].addr[3] != 0) ||
			(ha->hw.mcast[i].addr[4] != 0) ||
			(ha->hw.mcast[i].addr[5] != 0)) {

			bcopy(ha->hw.mcast[i].addr, mcast, ETHER_ADDR_LEN);
			mcast = mcast + ETHER_ADDR_LEN;
			count++;

			device_printf(ha->pci_dev,
				"%s: %x:%x:%x:%x:%x:%x \n",
				__func__, ha->hw.mcast[i].addr[0],
				ha->hw.mcast[i].addr[1], ha->hw.mcast[i].addr[2],
				ha->hw.mcast[i].addr[3], ha->hw.mcast[i].addr[4],
				ha->hw.mcast[i].addr[5]);
			
			if (count == Q8_MAX_MAC_ADDRS) {
				if (qla_config_mac_addr(ha, ha->hw.mac_addr_arr,
					add_mcast, count)) {
                			device_printf(ha->pci_dev,
						"%s: failed\n", __func__);
					return (-1);
				}

				count = 0;
				mcast = ha->hw.mac_addr_arr;
				memset(mcast, 0,
					(Q8_MAX_MAC_ADDRS * ETHER_ADDR_LEN));
			}

			nmcast--;
		}
	}

	if (count) {
		if (qla_config_mac_addr(ha, ha->hw.mac_addr_arr, add_mcast,
			count)) {
                	device_printf(ha->pci_dev, "%s: failed\n", __func__);
			return (-1);
		}
	}
	QL_DPRINT2(ha, (ha->pci_dev,
		"%s:[0x%x] exit nmcast = %d \n", __func__, add_mcast, nmcast));

	return 0;
}

static int
qla_hw_add_all_mcast(qla_host_t *ha)
{
	int ret;

	ret = qla_hw_all_mcast(ha, 1);

	return (ret);
}

int
qla_hw_del_all_mcast(qla_host_t *ha)
{
	int ret;

	ret = qla_hw_all_mcast(ha, 0);

	bzero(ha->hw.mcast, (sizeof (qla_mcast_t) * Q8_MAX_NUM_MULTICAST_ADDRS));
	ha->hw.nmcast = 0;

	return (ret);
}

static int
qla_hw_mac_addr_present(qla_host_t *ha, uint8_t *mta)
{
	int i;

	for (i = 0; i < Q8_MAX_NUM_MULTICAST_ADDRS; i++) {
		if (QL_MAC_CMP(ha->hw.mcast[i].addr, mta) == 0)
			return (0); /* its been already added */
	}
	return (-1);
}

static int
qla_hw_add_mcast(qla_host_t *ha, uint8_t *mta, uint32_t nmcast)
{
	int i;

	for (i = 0; i < Q8_MAX_NUM_MULTICAST_ADDRS; i++) {

		if ((ha->hw.mcast[i].addr[0] == 0) && 
			(ha->hw.mcast[i].addr[1] == 0) &&
			(ha->hw.mcast[i].addr[2] == 0) &&
			(ha->hw.mcast[i].addr[3] == 0) &&
			(ha->hw.mcast[i].addr[4] == 0) &&
			(ha->hw.mcast[i].addr[5] == 0)) {

			bcopy(mta, ha->hw.mcast[i].addr, Q8_MAC_ADDR_LEN);
			ha->hw.nmcast++;	

			mta = mta + ETHER_ADDR_LEN;
			nmcast--;

			if (nmcast == 0)
				break;
		}

	}
	return 0;
}

static int
qla_hw_del_mcast(qla_host_t *ha, uint8_t *mta, uint32_t nmcast)
{
	int i;

	for (i = 0; i < Q8_MAX_NUM_MULTICAST_ADDRS; i++) {
		if (QL_MAC_CMP(ha->hw.mcast[i].addr, mta) == 0) {

			ha->hw.mcast[i].addr[0] = 0;
			ha->hw.mcast[i].addr[1] = 0;
			ha->hw.mcast[i].addr[2] = 0;
			ha->hw.mcast[i].addr[3] = 0;
			ha->hw.mcast[i].addr[4] = 0;
			ha->hw.mcast[i].addr[5] = 0;

			ha->hw.nmcast--;	

			mta = mta + ETHER_ADDR_LEN;
			nmcast--;

			if (nmcast == 0)
				break;
		}
	}
	return 0;
}

/*
 * Name: ql_hw_set_multi
 * Function: Sets the Multicast Addresses provided by the host O.S into the
 *	hardware (for the given interface)
 */
int
ql_hw_set_multi(qla_host_t *ha, uint8_t *mcast_addr, uint32_t mcnt,
	uint32_t add_mac)
{
	uint8_t *mta = mcast_addr;
	int i;
	int ret = 0;
	uint32_t count = 0;
	uint8_t *mcast;

	mcast = ha->hw.mac_addr_arr;
	memset(mcast, 0, (Q8_MAX_MAC_ADDRS * ETHER_ADDR_LEN));

	for (i = 0; i < mcnt; i++) {
		if (mta[0] || mta[1] || mta[2] || mta[3] || mta[4] || mta[5]) {
			if (add_mac) {
				if (qla_hw_mac_addr_present(ha, mta) != 0) {
					bcopy(mta, mcast, ETHER_ADDR_LEN);
					mcast = mcast + ETHER_ADDR_LEN;
					count++;
				}
			} else {
				if (qla_hw_mac_addr_present(ha, mta) == 0) {
					bcopy(mta, mcast, ETHER_ADDR_LEN);
					mcast = mcast + ETHER_ADDR_LEN;
					count++;
				}
			}
		}
		if (count == Q8_MAX_MAC_ADDRS) {
			if (qla_config_mac_addr(ha, ha->hw.mac_addr_arr,
				add_mac, count)) {
                		device_printf(ha->pci_dev, "%s: failed\n",
					__func__);
				return (-1);
			}

			if (add_mac) {
				qla_hw_add_mcast(ha, ha->hw.mac_addr_arr,
					count);
			} else {
				qla_hw_del_mcast(ha, ha->hw.mac_addr_arr,
					count);
			}

			count = 0;
			mcast = ha->hw.mac_addr_arr;
			memset(mcast, 0, (Q8_MAX_MAC_ADDRS * ETHER_ADDR_LEN));
		}
			
		mta += Q8_MAC_ADDR_LEN;
	}

	if (count) {
		if (qla_config_mac_addr(ha, ha->hw.mac_addr_arr, add_mac,
			count)) {
                	device_printf(ha->pci_dev, "%s: failed\n", __func__);
			return (-1);
		}
		if (add_mac) {
			qla_hw_add_mcast(ha, ha->hw.mac_addr_arr, count);
		} else {
			qla_hw_del_mcast(ha, ha->hw.mac_addr_arr, count);
		}
	}

	return (ret);
}

/*
 * Name: ql_hw_tx_done_locked
 * Function: Handle Transmit Completions
 */
void
ql_hw_tx_done_locked(qla_host_t *ha, uint32_t txr_idx)
{
	qla_tx_buf_t *txb;
        qla_hw_t *hw = &ha->hw;
	uint32_t comp_idx, comp_count = 0;
	qla_hw_tx_cntxt_t *hw_tx_cntxt;

	hw_tx_cntxt = &hw->tx_cntxt[txr_idx];

	/* retrieve index of last entry in tx ring completed */
	comp_idx = qla_le32_to_host(*(hw_tx_cntxt->tx_cons));

	while (comp_idx != hw_tx_cntxt->txr_comp) {

		txb = &ha->tx_ring[txr_idx].tx_buf[hw_tx_cntxt->txr_comp];

		hw_tx_cntxt->txr_comp++;
		if (hw_tx_cntxt->txr_comp == NUM_TX_DESCRIPTORS)
			hw_tx_cntxt->txr_comp = 0;

		comp_count++;

		if (txb->m_head) {
			if_inc_counter(ha->ifp, IFCOUNTER_OPACKETS, 1);

			bus_dmamap_sync(ha->tx_tag, txb->map,
				BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(ha->tx_tag, txb->map);
			m_freem(txb->m_head);

			txb->m_head = NULL;
		}
	}

	hw_tx_cntxt->txr_free += comp_count;

	if (hw_tx_cntxt->txr_free > NUM_TX_DESCRIPTORS)
		device_printf(ha->pci_dev, "%s [%d]: txr_idx = %d txr_free = %d"
			"txr_next = %d txr_comp = %d\n", __func__, __LINE__,
			txr_idx, hw_tx_cntxt->txr_free,
			hw_tx_cntxt->txr_next, hw_tx_cntxt->txr_comp);

	QL_ASSERT(ha, (hw_tx_cntxt->txr_free <= NUM_TX_DESCRIPTORS), \
		("%s [%d]: txr_idx = %d txr_free = %d txr_next = %d txr_comp = %d\n",\
		__func__, __LINE__, txr_idx, hw_tx_cntxt->txr_free, \
		hw_tx_cntxt->txr_next, hw_tx_cntxt->txr_comp));
	
	return;
}

void
ql_update_link_state(qla_host_t *ha)
{
	uint32_t link_state = 0;
	uint32_t prev_link_state;

	prev_link_state =  ha->hw.link_up;

	if (ha->ifp->if_drv_flags & IFF_DRV_RUNNING) {
		link_state = READ_REG32(ha, Q8_LINK_STATE);

		if (ha->pci_func == 0) {
			link_state = (((link_state & 0xF) == 1)? 1 : 0);
		} else {
			link_state = ((((link_state >> 4)& 0xF) == 1)? 1 : 0);
		}
	}

	atomic_store_rel_8(&ha->hw.link_up, (uint8_t)link_state);

	if (prev_link_state !=  ha->hw.link_up) {
		if (ha->hw.link_up) {
			if_link_state_change(ha->ifp, LINK_STATE_UP);
		} else {
			if_link_state_change(ha->ifp, LINK_STATE_DOWN);
		}
	}
	return;
}

int
ql_hw_check_health(qla_host_t *ha)
{
	uint32_t val;

	ha->hw.health_count++;

	if (ha->hw.health_count < 500)
		return 0;

	ha->hw.health_count = 0;

	val = READ_REG32(ha, Q8_ASIC_TEMPERATURE);

	if (((val & 0xFFFF) == 2) || ((val & 0xFFFF) == 3) ||
		(QL_ERR_INJECT(ha, INJCT_TEMPERATURE_FAILURE))) {
		device_printf(ha->pci_dev, "%s: Temperature Alert"
			" at ts_usecs %ld ts_reg = 0x%08x\n",
			__func__, qla_get_usec_timestamp(), val);

		if (ha->hw.sp_log_stop_events & Q8_SP_LOG_STOP_TEMP_FAILURE)
			ha->hw.sp_log_stop = -1;

		QL_INITIATE_RECOVERY(ha);
		return -1;
	}

	val = READ_REG32(ha, Q8_FIRMWARE_HEARTBEAT);

	if ((val != ha->hw.hbeat_value) &&
		(!(QL_ERR_INJECT(ha, INJCT_HEARTBEAT_FAILURE)))) {
		ha->hw.hbeat_value = val;
		ha->hw.hbeat_failure = 0;
		return 0;
	}

	ha->hw.hbeat_failure++;

	
	if ((ha->dbg_level & 0x8000) && (ha->hw.hbeat_failure == 1))
		device_printf(ha->pci_dev, "%s: Heartbeat Failue 1[0x%08x]\n",
			__func__, val);
	if (ha->hw.hbeat_failure < 2) /* we ignore the first failure */
		return 0;
	else {
		uint32_t peg_halt_status1;
		uint32_t peg_halt_status2;

		peg_halt_status1 = READ_REG32(ha, Q8_PEG_HALT_STATUS1);
		peg_halt_status2 = READ_REG32(ha, Q8_PEG_HALT_STATUS2);

		device_printf(ha->pci_dev,
			"%s: Heartbeat Failue at ts_usecs = %ld "
			"fw_heart_beat = 0x%08x "
			"peg_halt_status1 = 0x%08x "
			"peg_halt_status2 = 0x%08x\n",
			__func__, qla_get_usec_timestamp(), val,
			peg_halt_status1, peg_halt_status2);

		if (ha->hw.sp_log_stop_events & Q8_SP_LOG_STOP_HBEAT_FAILURE)
			ha->hw.sp_log_stop = -1;
	}
	QL_INITIATE_RECOVERY(ha);

	return -1;
}

static int
qla_init_nic_func(qla_host_t *ha)
{
        device_t                dev;
        q80_init_nic_func_t     *init_nic;
        q80_init_nic_func_rsp_t *init_nic_rsp;
        uint32_t                err;

        dev = ha->pci_dev;

        init_nic = (q80_init_nic_func_t *)ha->hw.mbox;
        bzero(init_nic, sizeof(q80_init_nic_func_t));

        init_nic->opcode = Q8_MBX_INIT_NIC_FUNC;
        init_nic->count_version = (sizeof (q80_init_nic_func_t) >> 2);
        init_nic->count_version |= Q8_MBX_CMD_VERSION;

        init_nic->options = Q8_INIT_NIC_REG_DCBX_CHNG_AEN;
        init_nic->options |= Q8_INIT_NIC_REG_SFP_CHNG_AEN;
        init_nic->options |= Q8_INIT_NIC_REG_IDC_AEN;

//qla_dump_buf8(ha, __func__, init_nic, sizeof (q80_init_nic_func_t));
        if (qla_mbx_cmd(ha, (uint32_t *)init_nic,
                (sizeof (q80_init_nic_func_t) >> 2),
                ha->hw.mbox, (sizeof (q80_init_nic_func_rsp_t) >> 2), 0)) {
                device_printf(dev, "%s: failed\n", __func__);
                return -1;
        }

        init_nic_rsp = (q80_init_nic_func_rsp_t *)ha->hw.mbox;
// qla_dump_buf8(ha, __func__, init_nic_rsp, sizeof (q80_init_nic_func_rsp_t));

        err = Q8_MBX_RSP_STATUS(init_nic_rsp->regcnt_status);

        if (err) {
                device_printf(dev, "%s: failed [0x%08x]\n", __func__, err);
        } else {
                device_printf(dev, "%s: successful\n", __func__);
	}

        return 0;
}

static int
qla_stop_nic_func(qla_host_t *ha)
{
        device_t                dev;
        q80_stop_nic_func_t     *stop_nic;
        q80_stop_nic_func_rsp_t *stop_nic_rsp;
        uint32_t                err;

        dev = ha->pci_dev;

        stop_nic = (q80_stop_nic_func_t *)ha->hw.mbox;
        bzero(stop_nic, sizeof(q80_stop_nic_func_t));

        stop_nic->opcode = Q8_MBX_STOP_NIC_FUNC;
        stop_nic->count_version = (sizeof (q80_stop_nic_func_t) >> 2);
        stop_nic->count_version |= Q8_MBX_CMD_VERSION;

        stop_nic->options = Q8_STOP_NIC_DEREG_DCBX_CHNG_AEN;
        stop_nic->options |= Q8_STOP_NIC_DEREG_SFP_CHNG_AEN;

//qla_dump_buf8(ha, __func__, stop_nic, sizeof (q80_stop_nic_func_t));
        if (qla_mbx_cmd(ha, (uint32_t *)stop_nic,
                (sizeof (q80_stop_nic_func_t) >> 2),
                ha->hw.mbox, (sizeof (q80_stop_nic_func_rsp_t) >> 2), 0)) {
                device_printf(dev, "%s: failed\n", __func__);
                return -1;
        }

        stop_nic_rsp = (q80_stop_nic_func_rsp_t *)ha->hw.mbox;
//qla_dump_buf8(ha, __func__, stop_nic_rsp, sizeof (q80_stop_nic_func_rsp_ t));

        err = Q8_MBX_RSP_STATUS(stop_nic_rsp->regcnt_status);

        if (err) {
                device_printf(dev, "%s: failed [0x%08x]\n", __func__, err);
        }

        return 0;
}

static int
qla_query_fw_dcbx_caps(qla_host_t *ha)
{
        device_t                        dev;
        q80_query_fw_dcbx_caps_t        *fw_dcbx;
        q80_query_fw_dcbx_caps_rsp_t    *fw_dcbx_rsp;
        uint32_t                        err;

        dev = ha->pci_dev;

        fw_dcbx = (q80_query_fw_dcbx_caps_t *)ha->hw.mbox;
        bzero(fw_dcbx, sizeof(q80_query_fw_dcbx_caps_t));

        fw_dcbx->opcode = Q8_MBX_GET_FW_DCBX_CAPS;
        fw_dcbx->count_version = (sizeof (q80_query_fw_dcbx_caps_t) >> 2);
        fw_dcbx->count_version |= Q8_MBX_CMD_VERSION;

        ql_dump_buf8(ha, __func__, fw_dcbx, sizeof (q80_query_fw_dcbx_caps_t));
        if (qla_mbx_cmd(ha, (uint32_t *)fw_dcbx,
                (sizeof (q80_query_fw_dcbx_caps_t) >> 2),
                ha->hw.mbox, (sizeof (q80_query_fw_dcbx_caps_rsp_t) >> 2), 0)) {
                device_printf(dev, "%s: failed\n", __func__);
                return -1;
        }

        fw_dcbx_rsp = (q80_query_fw_dcbx_caps_rsp_t *)ha->hw.mbox;
        ql_dump_buf8(ha, __func__, fw_dcbx_rsp,
                sizeof (q80_query_fw_dcbx_caps_rsp_t));

        err = Q8_MBX_RSP_STATUS(fw_dcbx_rsp->regcnt_status);

        if (err) {
                device_printf(dev, "%s: failed [0x%08x]\n", __func__, err);
        }

        return 0;
}

static int
qla_idc_ack(qla_host_t *ha, uint32_t aen_mb1, uint32_t aen_mb2,
        uint32_t aen_mb3, uint32_t aen_mb4)
{
        device_t                dev;
        q80_idc_ack_t           *idc_ack;
        q80_idc_ack_rsp_t       *idc_ack_rsp;
        uint32_t                err;
        int                     count = 300;

        dev = ha->pci_dev;

        idc_ack = (q80_idc_ack_t *)ha->hw.mbox;
        bzero(idc_ack, sizeof(q80_idc_ack_t));

        idc_ack->opcode = Q8_MBX_IDC_ACK;
        idc_ack->count_version = (sizeof (q80_idc_ack_t) >> 2);
        idc_ack->count_version |= Q8_MBX_CMD_VERSION;

        idc_ack->aen_mb1 = aen_mb1;
        idc_ack->aen_mb2 = aen_mb2;
        idc_ack->aen_mb3 = aen_mb3;
        idc_ack->aen_mb4 = aen_mb4;

        ha->hw.imd_compl= 0;

        if (qla_mbx_cmd(ha, (uint32_t *)idc_ack,
                (sizeof (q80_idc_ack_t) >> 2),
                ha->hw.mbox, (sizeof (q80_idc_ack_rsp_t) >> 2), 0)) {
                device_printf(dev, "%s: failed\n", __func__);
                return -1;
        }

        idc_ack_rsp = (q80_idc_ack_rsp_t *)ha->hw.mbox;

        err = Q8_MBX_RSP_STATUS(idc_ack_rsp->regcnt_status);

        if (err) {
                device_printf(dev, "%s: failed [0x%08x]\n", __func__, err);
                return(-1);
        }

        while (count && !ha->hw.imd_compl) {
                qla_mdelay(__func__, 100);
                count--;
        }

        if (!count)
                return -1;
        else
                device_printf(dev, "%s: count %d\n", __func__, count);

        return (0);
}

static int
qla_set_port_config(qla_host_t *ha, uint32_t cfg_bits)
{
        device_t                dev;
        q80_set_port_cfg_t      *pcfg;
        q80_set_port_cfg_rsp_t  *pfg_rsp;
        uint32_t                err;
        int                     count = 300;

        dev = ha->pci_dev;

        pcfg = (q80_set_port_cfg_t *)ha->hw.mbox;
        bzero(pcfg, sizeof(q80_set_port_cfg_t));

        pcfg->opcode = Q8_MBX_SET_PORT_CONFIG;
        pcfg->count_version = (sizeof (q80_set_port_cfg_t) >> 2);
        pcfg->count_version |= Q8_MBX_CMD_VERSION;

        pcfg->cfg_bits = cfg_bits;

        device_printf(dev, "%s: cfg_bits"
                " [STD_PAUSE_DIR, PAUSE_TYPE, DCBX]"
                " [0x%x, 0x%x, 0x%x]\n", __func__,
                ((cfg_bits & Q8_PORT_CFG_BITS_STDPAUSE_DIR_MASK)>>20),
                ((cfg_bits & Q8_PORT_CFG_BITS_PAUSE_CFG_MASK) >> 5),
                ((cfg_bits & Q8_PORT_CFG_BITS_DCBX_ENABLE) ? 1: 0));

        ha->hw.imd_compl= 0;

        if (qla_mbx_cmd(ha, (uint32_t *)pcfg,
                (sizeof (q80_set_port_cfg_t) >> 2),
                ha->hw.mbox, (sizeof (q80_set_port_cfg_rsp_t) >> 2), 0)) {
                device_printf(dev, "%s: failed\n", __func__);
                return -1;
        }

        pfg_rsp = (q80_set_port_cfg_rsp_t *)ha->hw.mbox;

        err = Q8_MBX_RSP_STATUS(pfg_rsp->regcnt_status);

        if (err == Q8_MBX_RSP_IDC_INTRMD_RSP) {
                while (count && !ha->hw.imd_compl) {
                        qla_mdelay(__func__, 100);
                        count--;
                }
                if (count) {
                        device_printf(dev, "%s: count %d\n", __func__, count);

                        err = 0;
                }
        }

        if (err) {
                device_printf(dev, "%s: failed [0x%08x]\n", __func__, err);
                return(-1);
        }

        return (0);
}


static int
qla_get_minidump_tmplt_size(qla_host_t *ha, uint32_t *size)
{
	uint32_t			err;
	device_t			dev = ha->pci_dev;
	q80_config_md_templ_size_t	*md_size;
	q80_config_md_templ_size_rsp_t	*md_size_rsp;

#ifndef QL_LDFLASH_FW

	ql_minidump_template_hdr_t *hdr;

	hdr = (ql_minidump_template_hdr_t *)ql83xx_minidump;
	*size = hdr->size_of_template;
	return (0);

#endif /* #ifdef QL_LDFLASH_FW */

	md_size = (q80_config_md_templ_size_t *) ha->hw.mbox;
	bzero(md_size, sizeof(q80_config_md_templ_size_t));

	md_size->opcode = Q8_MBX_GET_MINIDUMP_TMPLT_SIZE;
	md_size->count_version = (sizeof (q80_config_md_templ_size_t) >> 2);
	md_size->count_version |= Q8_MBX_CMD_VERSION;

	if (qla_mbx_cmd(ha, (uint32_t *) md_size,
		(sizeof(q80_config_md_templ_size_t) >> 2), ha->hw.mbox,
		(sizeof(q80_config_md_templ_size_rsp_t) >> 2), 0)) {

		device_printf(dev, "%s: failed\n", __func__);

		return (-1);
	}

	md_size_rsp = (q80_config_md_templ_size_rsp_t *) ha->hw.mbox;

	err = Q8_MBX_RSP_STATUS(md_size_rsp->regcnt_status);

        if (err) {
		device_printf(dev, "%s: failed [0x%08x]\n", __func__, err);
		return(-1);
        }

	*size = md_size_rsp->templ_size;

	return (0);
}

static int
qla_get_port_config(qla_host_t *ha, uint32_t *cfg_bits)
{
        device_t                dev;
        q80_get_port_cfg_t      *pcfg;
        q80_get_port_cfg_rsp_t  *pcfg_rsp;
        uint32_t                err;

        dev = ha->pci_dev;

        pcfg = (q80_get_port_cfg_t *)ha->hw.mbox;
        bzero(pcfg, sizeof(q80_get_port_cfg_t));

        pcfg->opcode = Q8_MBX_GET_PORT_CONFIG;
        pcfg->count_version = (sizeof (q80_get_port_cfg_t) >> 2);
        pcfg->count_version |= Q8_MBX_CMD_VERSION;

        if (qla_mbx_cmd(ha, (uint32_t *)pcfg,
                (sizeof (q80_get_port_cfg_t) >> 2),
                ha->hw.mbox, (sizeof (q80_get_port_cfg_rsp_t) >> 2), 0)) {
                device_printf(dev, "%s: failed\n", __func__);
                return -1;
        }

        pcfg_rsp = (q80_get_port_cfg_rsp_t *)ha->hw.mbox;

        err = Q8_MBX_RSP_STATUS(pcfg_rsp->regcnt_status);

        if (err) {
                device_printf(dev, "%s: failed [0x%08x]\n", __func__, err);
                return(-1);
        }

        device_printf(dev, "%s: [cfg_bits, port type]"
                " [0x%08x, 0x%02x] [STD_PAUSE_DIR, PAUSE_TYPE, DCBX]"
                " [0x%x, 0x%x, 0x%x]\n", __func__,
                pcfg_rsp->cfg_bits, pcfg_rsp->phys_port_type,
                ((pcfg_rsp->cfg_bits & Q8_PORT_CFG_BITS_STDPAUSE_DIR_MASK)>>20),
                ((pcfg_rsp->cfg_bits & Q8_PORT_CFG_BITS_PAUSE_CFG_MASK) >> 5),
                ((pcfg_rsp->cfg_bits & Q8_PORT_CFG_BITS_DCBX_ENABLE) ? 1: 0)
                );

        *cfg_bits = pcfg_rsp->cfg_bits;

        return (0);
}

int
ql_iscsi_pdu(qla_host_t *ha, struct mbuf *mp)
{
        struct ether_vlan_header        *eh;
        uint16_t                        etype;
        struct ip                       *ip = NULL;
        struct ip6_hdr                  *ip6 = NULL;
        struct tcphdr                   *th = NULL;
        uint32_t                        hdrlen;
        uint32_t                        offset;
        uint8_t                         buf[sizeof(struct ip6_hdr)];

        eh = mtod(mp, struct ether_vlan_header *);

        if (eh->evl_encap_proto == htons(ETHERTYPE_VLAN)) {
                hdrlen = ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN;
                etype = ntohs(eh->evl_proto);
        } else {
                hdrlen = ETHER_HDR_LEN;
                etype = ntohs(eh->evl_encap_proto);
        }

	if (etype == ETHERTYPE_IP) {

		offset = (hdrlen + sizeof (struct ip));

		if (mp->m_len >= offset) {
                        ip = (struct ip *)(mp->m_data + hdrlen);
		} else {
			m_copydata(mp, hdrlen, sizeof (struct ip), buf);
                        ip = (struct ip *)buf;
		}

                if (ip->ip_p == IPPROTO_TCP) {

			hdrlen += ip->ip_hl << 2;
			offset = hdrlen + 4;
	
			if (mp->m_len >= offset) {
				th = (struct tcphdr *)(mp->m_data + hdrlen);;
			} else {
                                m_copydata(mp, hdrlen, 4, buf);
				th = (struct tcphdr *)buf;
			}
                }

	} else if (etype == ETHERTYPE_IPV6) {

		offset = (hdrlen + sizeof (struct ip6_hdr));

		if (mp->m_len >= offset) {
                        ip6 = (struct ip6_hdr *)(mp->m_data + hdrlen);
		} else {
                        m_copydata(mp, hdrlen, sizeof (struct ip6_hdr), buf);
                        ip6 = (struct ip6_hdr *)buf;
		}

                if (ip6->ip6_nxt == IPPROTO_TCP) {

			hdrlen += sizeof(struct ip6_hdr);
			offset = hdrlen + 4;

			if (mp->m_len >= offset) {
				th = (struct tcphdr *)(mp->m_data + hdrlen);;
			} else {
				m_copydata(mp, hdrlen, 4, buf);
				th = (struct tcphdr *)buf;
			}
                }
	}

        if (th != NULL) {
                if ((th->th_sport == htons(3260)) ||
                        (th->th_dport == htons(3260)))
                        return 0;
        }
        return (-1);
}

void
qla_hw_async_event(qla_host_t *ha)
{
        switch (ha->hw.aen_mb0) {
        case 0x8101:
                (void)qla_idc_ack(ha, ha->hw.aen_mb1, ha->hw.aen_mb2,
                        ha->hw.aen_mb3, ha->hw.aen_mb4);

                break;

        default:
                break;
        }

        return;
}

#ifdef QL_LDFLASH_FW
static int
ql_get_minidump_template(qla_host_t *ha)
{
	uint32_t			err;
	device_t			dev = ha->pci_dev;
	q80_config_md_templ_cmd_t	*md_templ;
	q80_config_md_templ_cmd_rsp_t	*md_templ_rsp;

	md_templ = (q80_config_md_templ_cmd_t *) ha->hw.mbox;
	bzero(md_templ, (sizeof (q80_config_md_templ_cmd_t)));

	md_templ->opcode = Q8_MBX_GET_MINIDUMP_TMPLT;
	md_templ->count_version = ( sizeof(q80_config_md_templ_cmd_t) >> 2);
	md_templ->count_version |= Q8_MBX_CMD_VERSION;

	md_templ->buf_addr = ha->hw.dma_buf.minidump.dma_addr;
	md_templ->buff_size = ha->hw.dma_buf.minidump.size;

	if (qla_mbx_cmd(ha, (uint32_t *) md_templ,
		(sizeof(q80_config_md_templ_cmd_t) >> 2),
		 ha->hw.mbox,
		(sizeof(q80_config_md_templ_cmd_rsp_t) >> 2), 0)) {

		device_printf(dev, "%s: failed\n", __func__);

		return (-1);
	}

	md_templ_rsp = (q80_config_md_templ_cmd_rsp_t *) ha->hw.mbox;

	err = Q8_MBX_RSP_STATUS(md_templ_rsp->regcnt_status);

	if (err) {
		device_printf(dev, "%s: failed [0x%08x]\n", __func__, err);
		return (-1);
	}

	return (0);

}
#endif /* #ifdef QL_LDFLASH_FW */

/*
 * Minidump related functionality 
 */

static int ql_parse_template(qla_host_t *ha);

static uint32_t ql_rdcrb(qla_host_t *ha,
			ql_minidump_entry_rdcrb_t *crb_entry,
			uint32_t * data_buff);

static uint32_t ql_pollrd(qla_host_t *ha,
			ql_minidump_entry_pollrd_t *entry,
			uint32_t * data_buff);

static uint32_t ql_pollrd_modify_write(qla_host_t *ha,
			ql_minidump_entry_rd_modify_wr_with_poll_t *entry,
			uint32_t *data_buff);

static uint32_t ql_L2Cache(qla_host_t *ha,
			ql_minidump_entry_cache_t *cacheEntry,
			uint32_t * data_buff);

static uint32_t ql_L1Cache(qla_host_t *ha,
			ql_minidump_entry_cache_t *cacheEntry,
			uint32_t *data_buff);

static uint32_t ql_rdocm(qla_host_t *ha,
			ql_minidump_entry_rdocm_t *ocmEntry,
			uint32_t *data_buff);

static uint32_t ql_rdmem(qla_host_t *ha,
			ql_minidump_entry_rdmem_t *mem_entry,
			uint32_t *data_buff);

static uint32_t ql_rdrom(qla_host_t *ha,
			ql_minidump_entry_rdrom_t *romEntry,
			uint32_t *data_buff);

static uint32_t ql_rdmux(qla_host_t *ha,
			ql_minidump_entry_mux_t *muxEntry,
			uint32_t *data_buff);

static uint32_t ql_rdmux2(qla_host_t *ha,
			ql_minidump_entry_mux2_t *muxEntry,
			uint32_t *data_buff);

static uint32_t ql_rdqueue(qla_host_t *ha,
			ql_minidump_entry_queue_t *queueEntry,
			uint32_t *data_buff);

static uint32_t ql_cntrl(qla_host_t *ha,
			ql_minidump_template_hdr_t *template_hdr,
			ql_minidump_entry_cntrl_t *crbEntry);


static uint32_t
ql_minidump_size(qla_host_t *ha)
{
	uint32_t i, k;
	uint32_t size = 0;
	ql_minidump_template_hdr_t *hdr;

	hdr = (ql_minidump_template_hdr_t *)ha->hw.dma_buf.minidump.dma_b;

	i = 0x2;

	for (k = 1; k < QL_DBG_CAP_SIZE_ARRAY_LEN; k++) {
		if (i & ha->hw.mdump_capture_mask)
			size += hdr->capture_size_array[k];
		i = i << 1;
	}
	return (size);
}

static void
ql_free_minidump_buffer(qla_host_t *ha)
{
	if (ha->hw.mdump_buffer != NULL) {
		free(ha->hw.mdump_buffer, M_QLA83XXBUF);
		ha->hw.mdump_buffer = NULL;
		ha->hw.mdump_buffer_size = 0;
	}
	return;
}

static int
ql_alloc_minidump_buffer(qla_host_t *ha)
{
	ha->hw.mdump_buffer_size = ql_minidump_size(ha);

	if (!ha->hw.mdump_buffer_size)
		return (-1);

	ha->hw.mdump_buffer = malloc(ha->hw.mdump_buffer_size, M_QLA83XXBUF,
					M_NOWAIT);

	if (ha->hw.mdump_buffer == NULL)
		return (-1);

	return (0);
}

static void
ql_free_minidump_template_buffer(qla_host_t *ha)
{
	if (ha->hw.mdump_template != NULL) {
		free(ha->hw.mdump_template, M_QLA83XXBUF);
		ha->hw.mdump_template = NULL;
		ha->hw.mdump_template_size = 0;
	}
	return;
}

static int
ql_alloc_minidump_template_buffer(qla_host_t *ha)
{
	ha->hw.mdump_template_size = ha->hw.dma_buf.minidump.size;

	ha->hw.mdump_template = malloc(ha->hw.mdump_template_size,
					M_QLA83XXBUF, M_NOWAIT);

	if (ha->hw.mdump_template == NULL)
		return (-1);

	return (0);
}

static int
ql_alloc_minidump_buffers(qla_host_t *ha)
{
	int ret;

	ret = ql_alloc_minidump_template_buffer(ha);

	if (ret)
		return (ret);

	ret = ql_alloc_minidump_buffer(ha);

	if (ret)
		ql_free_minidump_template_buffer(ha);

	return (ret);
}


static uint32_t
ql_validate_minidump_checksum(qla_host_t *ha)
{
        uint64_t sum = 0;
	int count;
	uint32_t *template_buff;

	count = ha->hw.dma_buf.minidump.size / sizeof (uint32_t);
	template_buff = ha->hw.dma_buf.minidump.dma_b;

	while (count-- > 0) {
		sum += *template_buff++;
	}

	while (sum >> 32) {
		sum = (sum & 0xFFFFFFFF) + (sum >> 32);
	}

	return (~sum);
}

int
ql_minidump_init(qla_host_t *ha)
{
	int		ret = 0;
	uint32_t	template_size = 0;
	device_t	dev = ha->pci_dev;

	/*
	 * Get Minidump Template Size
 	 */
	ret = qla_get_minidump_tmplt_size(ha, &template_size);

	if (ret || (template_size == 0)) {
		device_printf(dev, "%s: failed [%d, %d]\n", __func__, ret,
			template_size);
		return (-1);
	}

	/*
	 * Allocate Memory for Minidump Template
	 */

	ha->hw.dma_buf.minidump.alignment = 8;
	ha->hw.dma_buf.minidump.size = template_size;

#ifdef QL_LDFLASH_FW
	if (ql_alloc_dmabuf(ha, &ha->hw.dma_buf.minidump)) {

		device_printf(dev, "%s: minidump dma alloc failed\n", __func__);

		return (-1);
	}
	ha->hw.dma_buf.flags.minidump = 1;

	/*
	 * Retrieve Minidump Template
	 */
	ret = ql_get_minidump_template(ha);
#else
	ha->hw.dma_buf.minidump.dma_b = ql83xx_minidump;

#endif /* #ifdef QL_LDFLASH_FW */

	if (ret == 0) {

		ret = ql_validate_minidump_checksum(ha);

		if (ret == 0) {

			ret = ql_alloc_minidump_buffers(ha);

			if (ret == 0)
		ha->hw.mdump_init = 1;
			else
				device_printf(dev,
					"%s: ql_alloc_minidump_buffers"
					" failed\n", __func__);
		} else {
			device_printf(dev, "%s: ql_validate_minidump_checksum"
				" failed\n", __func__);
		}
	} else {
		device_printf(dev, "%s: ql_get_minidump_template failed\n",
			 __func__);
	}

	if (ret)
		ql_minidump_free(ha);

	return (ret);
}

static void
ql_minidump_free(qla_host_t *ha)
{
	ha->hw.mdump_init = 0;
	if (ha->hw.dma_buf.flags.minidump) {
		ha->hw.dma_buf.flags.minidump = 0;
		ql_free_dmabuf(ha, &ha->hw.dma_buf.minidump);
	}

	ql_free_minidump_template_buffer(ha);
	ql_free_minidump_buffer(ha);

	return;
}

void
ql_minidump(qla_host_t *ha)
{
	if (!ha->hw.mdump_init)
		return;

	if (ha->hw.mdump_done)
		return;
	ha->hw.mdump_usec_ts = qla_get_usec_timestamp();
	ha->hw.mdump_start_seq_index = ql_stop_sequence(ha);

	bzero(ha->hw.mdump_buffer, ha->hw.mdump_buffer_size);
	bzero(ha->hw.mdump_template, ha->hw.mdump_template_size);

	bcopy(ha->hw.dma_buf.minidump.dma_b, ha->hw.mdump_template,
		ha->hw.mdump_template_size);

	ql_parse_template(ha);
 
	ql_start_sequence(ha, ha->hw.mdump_start_seq_index);

	ha->hw.mdump_done = 1;

	return;
}


/*
 * helper routines
 */
static void 
ql_entry_err_chk(ql_minidump_entry_t *entry, uint32_t esize)
{
	if (esize != entry->hdr.entry_capture_size) {
		entry->hdr.entry_capture_size = esize;
		entry->hdr.driver_flags |= QL_DBG_SIZE_ERR_FLAG;
	}
	return;
}


static int 
ql_parse_template(qla_host_t *ha)
{
	uint32_t num_of_entries, buff_level, e_cnt, esize;
	uint32_t end_cnt, rv = 0;
	char *dump_buff, *dbuff;
	int sane_start = 0, sane_end = 0;
	ql_minidump_template_hdr_t *template_hdr;
	ql_minidump_entry_t *entry;
	uint32_t capture_mask; 
	uint32_t dump_size; 

	/* Setup parameters */
	template_hdr = (ql_minidump_template_hdr_t *)ha->hw.mdump_template;

	if (template_hdr->entry_type == TLHDR)
		sane_start = 1;
	
	dump_buff = (char *) ha->hw.mdump_buffer;

	num_of_entries = template_hdr->num_of_entries;

	entry = (ql_minidump_entry_t *) ((char *)template_hdr 
			+ template_hdr->first_entry_offset );

	template_hdr->saved_state_array[QL_OCM0_ADDR_INDX] =
		template_hdr->ocm_window_array[ha->pci_func];
	template_hdr->saved_state_array[QL_PCIE_FUNC_INDX] = ha->pci_func;

	capture_mask = ha->hw.mdump_capture_mask;
	dump_size = ha->hw.mdump_buffer_size;

	template_hdr->driver_capture_mask = capture_mask;

	QL_DPRINT80(ha, (ha->pci_dev,
		"%s: sane_start = %d num_of_entries = %d "
		"capture_mask = 0x%x dump_size = %d \n", 
		__func__, sane_start, num_of_entries, capture_mask, dump_size));

	for (buff_level = 0, e_cnt = 0; e_cnt < num_of_entries; e_cnt++) {

		/*
		 * If the capture_mask of the entry does not match capture mask
		 * skip the entry after marking the driver_flags indicator.
		 */
		
		if (!(entry->hdr.entry_capture_mask & capture_mask)) {

			entry->hdr.driver_flags |= QL_DBG_SKIPPED_FLAG;
			entry = (ql_minidump_entry_t *) ((char *) entry
					+ entry->hdr.entry_size);
			continue;
		}

		/*
		 * This is ONLY needed in implementations where
		 * the capture buffer allocated is too small to capture
		 * all of the required entries for a given capture mask.
		 * We need to empty the buffer contents to a file
		 * if possible, before processing the next entry
		 * If the buff_full_flag is set, no further capture will happen
		 * and all remaining non-control entries will be skipped.
		 */
		if (entry->hdr.entry_capture_size != 0) {
			if ((buff_level + entry->hdr.entry_capture_size) >
				dump_size) {
				/*  Try to recover by emptying buffer to file */
				entry->hdr.driver_flags |= QL_DBG_SKIPPED_FLAG;
				entry = (ql_minidump_entry_t *) ((char *) entry
						+ entry->hdr.entry_size);
				continue;
			}
		}

		/*
		 * Decode the entry type and process it accordingly
		 */

		switch (entry->hdr.entry_type) {
		case RDNOP:
			break;

		case RDEND:
			if (sane_end == 0) {
				end_cnt = e_cnt;
			}
			sane_end++;
			break;

		case RDCRB:
			dbuff = dump_buff + buff_level;
			esize = ql_rdcrb(ha, (void *)entry, (void *)dbuff);
			ql_entry_err_chk(entry, esize);
			buff_level += esize;
			break;

                case POLLRD:
                        dbuff = dump_buff + buff_level;
                        esize = ql_pollrd(ha, (void *)entry, (void *)dbuff);
                        ql_entry_err_chk(entry, esize);
                        buff_level += esize;
                        break;

                case POLLRDMWR:
                        dbuff = dump_buff + buff_level;
                        esize = ql_pollrd_modify_write(ha, (void *)entry,
					(void *)dbuff);
                        ql_entry_err_chk(entry, esize);
                        buff_level += esize;
                        break;

		case L2ITG:
		case L2DTG:
		case L2DAT:
		case L2INS:
			dbuff = dump_buff + buff_level;
			esize = ql_L2Cache(ha, (void *)entry, (void *)dbuff);
			if (esize == -1) {
				entry->hdr.driver_flags |= QL_DBG_SKIPPED_FLAG;
			} else {
				ql_entry_err_chk(entry, esize);
				buff_level += esize;
			}
			break;

		case L1DAT:
		case L1INS:
			dbuff = dump_buff + buff_level;
			esize = ql_L1Cache(ha, (void *)entry, (void *)dbuff);
			ql_entry_err_chk(entry, esize);
			buff_level += esize;
			break;

		case RDOCM:
			dbuff = dump_buff + buff_level;
			esize = ql_rdocm(ha, (void *)entry, (void *)dbuff);
			ql_entry_err_chk(entry, esize);
			buff_level += esize;
			break;

		case RDMEM:
			dbuff = dump_buff + buff_level;
			esize = ql_rdmem(ha, (void *)entry, (void *)dbuff);
			ql_entry_err_chk(entry, esize);
			buff_level += esize;
			break;

		case BOARD:
		case RDROM:
			dbuff = dump_buff + buff_level;
			esize = ql_rdrom(ha, (void *)entry, (void *)dbuff);
			ql_entry_err_chk(entry, esize);
			buff_level += esize;
			break;

		case RDMUX:
			dbuff = dump_buff + buff_level;
			esize = ql_rdmux(ha, (void *)entry, (void *)dbuff);
			ql_entry_err_chk(entry, esize);
			buff_level += esize;
			break;

                case RDMUX2:
                        dbuff = dump_buff + buff_level;
                        esize = ql_rdmux2(ha, (void *)entry, (void *)dbuff);
                        ql_entry_err_chk(entry, esize);
                        buff_level += esize;
                        break;

		case QUEUE:
			dbuff = dump_buff + buff_level;
			esize = ql_rdqueue(ha, (void *)entry, (void *)dbuff);
			ql_entry_err_chk(entry, esize);
			buff_level += esize;
			break;

		case CNTRL:
			if ((rv = ql_cntrl(ha, template_hdr, (void *)entry))) {
				entry->hdr.driver_flags |= QL_DBG_SKIPPED_FLAG;
			}
			break;
		default:
			entry->hdr.driver_flags |= QL_DBG_SKIPPED_FLAG;
			break;
		}
		/*  next entry in the template */
		entry = (ql_minidump_entry_t *) ((char *) entry
						+ entry->hdr.entry_size);
	}

	if (!sane_start || (sane_end > 1)) {
		device_printf(ha->pci_dev,
			"\n%s: Template configuration error. Check Template\n",
			__func__);
	}
	
	QL_DPRINT80(ha, (ha->pci_dev, "%s: Minidump num of entries = %d\n",
		__func__, template_hdr->num_of_entries));

	return 0;
}

/*
 * Read CRB operation.
 */
static uint32_t
ql_rdcrb(qla_host_t *ha, ql_minidump_entry_rdcrb_t * crb_entry,
	uint32_t * data_buff)
{
	int loop_cnt;
	int ret;
	uint32_t op_count, addr, stride, value = 0;

	addr = crb_entry->addr;
	op_count = crb_entry->op_count;
	stride = crb_entry->addr_stride;

	for (loop_cnt = 0; loop_cnt < op_count; loop_cnt++) {

		ret = ql_rdwr_indreg32(ha, addr, &value, 1);

		if (ret)
			return (0);

		*data_buff++ = addr;
		*data_buff++ = value;
		addr = addr + stride;
	}

	/*
	 * for testing purpose we return amount of data written
	 */
	return (op_count * (2 * sizeof(uint32_t)));
}

/*
 * Handle L2 Cache.
 */

static uint32_t 
ql_L2Cache(qla_host_t *ha, ql_minidump_entry_cache_t *cacheEntry,
	uint32_t * data_buff)
{
	int i, k;
	int loop_cnt;
	int ret;

	uint32_t read_value;
	uint32_t addr, read_addr, cntrl_addr, tag_reg_addr, cntl_value_w;
	uint32_t tag_value, read_cnt;
	volatile uint8_t cntl_value_r;
	long timeout;
	uint32_t data;

	loop_cnt = cacheEntry->op_count;

	read_addr = cacheEntry->read_addr;
	cntrl_addr = cacheEntry->control_addr;
	cntl_value_w = (uint32_t) cacheEntry->write_value;

	tag_reg_addr = cacheEntry->tag_reg_addr;

	tag_value = cacheEntry->init_tag_value;
	read_cnt = cacheEntry->read_addr_cnt;

	for (i = 0; i < loop_cnt; i++) {

		ret = ql_rdwr_indreg32(ha, tag_reg_addr, &tag_value, 0);
		if (ret)
			return (0);

		if (cacheEntry->write_value != 0) { 

			ret = ql_rdwr_indreg32(ha, cntrl_addr,
					&cntl_value_w, 0);
			if (ret)
				return (0);
		}

		if (cacheEntry->poll_mask != 0) { 

			timeout = cacheEntry->poll_wait;

			ret = ql_rdwr_indreg32(ha, cntrl_addr, &data, 1);
			if (ret)
				return (0);

			cntl_value_r = (uint8_t)data;

			while ((cntl_value_r & cacheEntry->poll_mask) != 0) {

				if (timeout) {
					qla_mdelay(__func__, 1);
					timeout--;
				} else
					break;

				ret = ql_rdwr_indreg32(ha, cntrl_addr,
						&data, 1);
				if (ret)
					return (0);

				cntl_value_r = (uint8_t)data;
			}
			if (!timeout) {
				/* Report timeout error. 
				 * core dump capture failed
				 * Skip remaining entries.
				 * Write buffer out to file
				 * Use driver specific fields in template header
				 * to report this error.
				 */
				return (-1);
			}
		}

		addr = read_addr;
		for (k = 0; k < read_cnt; k++) {

			ret = ql_rdwr_indreg32(ha, addr, &read_value, 1);
			if (ret)
				return (0);

			*data_buff++ = read_value;
			addr += cacheEntry->read_addr_stride;
		}

		tag_value += cacheEntry->tag_value_stride;
	}

	return (read_cnt * loop_cnt * sizeof(uint32_t));
}

/*
 * Handle L1 Cache.
 */

static uint32_t 
ql_L1Cache(qla_host_t *ha,
	ql_minidump_entry_cache_t *cacheEntry,
	uint32_t *data_buff)
{
	int ret;
	int i, k;
	int loop_cnt;

	uint32_t read_value;
	uint32_t addr, read_addr, cntrl_addr, tag_reg_addr;
	uint32_t tag_value, read_cnt;
	uint32_t cntl_value_w;

	loop_cnt = cacheEntry->op_count;

	read_addr = cacheEntry->read_addr;
	cntrl_addr = cacheEntry->control_addr;
	cntl_value_w = (uint32_t) cacheEntry->write_value;

	tag_reg_addr = cacheEntry->tag_reg_addr;

	tag_value = cacheEntry->init_tag_value;
	read_cnt = cacheEntry->read_addr_cnt;

	for (i = 0; i < loop_cnt; i++) {

		ret = ql_rdwr_indreg32(ha, tag_reg_addr, &tag_value, 0);
		if (ret)
			return (0);

		ret = ql_rdwr_indreg32(ha, cntrl_addr, &cntl_value_w, 0);
		if (ret)
			return (0);

		addr = read_addr;
		for (k = 0; k < read_cnt; k++) {

			ret = ql_rdwr_indreg32(ha, addr, &read_value, 1);
			if (ret)
				return (0);

			*data_buff++ = read_value;
			addr += cacheEntry->read_addr_stride;
		}

		tag_value += cacheEntry->tag_value_stride;
	}

	return (read_cnt * loop_cnt * sizeof(uint32_t));
}

/*
 * Reading OCM memory
 */

static uint32_t 
ql_rdocm(qla_host_t *ha,
	ql_minidump_entry_rdocm_t *ocmEntry,
	uint32_t *data_buff)
{
	int i, loop_cnt;
	volatile uint32_t addr;
	volatile uint32_t value;

	addr = ocmEntry->read_addr;
	loop_cnt = ocmEntry->op_count;

	for (i = 0; i < loop_cnt; i++) {
		value = READ_REG32(ha, addr);
		*data_buff++ = value;
		addr += ocmEntry->read_addr_stride;
	}
	return (loop_cnt * sizeof(value));
}

/*
 * Read memory
 */

static uint32_t 
ql_rdmem(qla_host_t *ha,
	ql_minidump_entry_rdmem_t *mem_entry,
	uint32_t *data_buff)
{
	int ret;
        int i, loop_cnt;
        volatile uint32_t addr;
	q80_offchip_mem_val_t val;

        addr = mem_entry->read_addr;

	/* size in bytes / 16 */
        loop_cnt = mem_entry->read_data_size / (sizeof(uint32_t) * 4);

        for (i = 0; i < loop_cnt; i++) {

		ret = ql_rdwr_offchip_mem(ha, (addr & 0x0ffffffff), &val, 1);
		if (ret)
			return (0);

                *data_buff++ = val.data_lo;
                *data_buff++ = val.data_hi;
                *data_buff++ = val.data_ulo;
                *data_buff++ = val.data_uhi;

                addr += (sizeof(uint32_t) * 4);
        }

        return (loop_cnt * (sizeof(uint32_t) * 4));
}

/*
 * Read Rom
 */

static uint32_t 
ql_rdrom(qla_host_t *ha,
	ql_minidump_entry_rdrom_t *romEntry,
	uint32_t *data_buff)
{
	int ret;
	int i, loop_cnt;
	uint32_t addr;
	uint32_t value;

	addr = romEntry->read_addr;
	loop_cnt = romEntry->read_data_size; /* This is size in bytes */
	loop_cnt /= sizeof(value);

	for (i = 0; i < loop_cnt; i++) {

		ret = ql_rd_flash32(ha, addr, &value);
		if (ret)
			return (0);

		*data_buff++ = value;
		addr += sizeof(value);
	}

	return (loop_cnt * sizeof(value));
}

/*
 * Read MUX data
 */

static uint32_t 
ql_rdmux(qla_host_t *ha,
	ql_minidump_entry_mux_t *muxEntry,
	uint32_t *data_buff)
{
	int ret;
	int loop_cnt;
	uint32_t read_value, sel_value;
	uint32_t read_addr, select_addr;

	select_addr = muxEntry->select_addr;
	sel_value = muxEntry->select_value;
	read_addr = muxEntry->read_addr;

	for (loop_cnt = 0; loop_cnt < muxEntry->op_count; loop_cnt++) {

		ret = ql_rdwr_indreg32(ha, select_addr, &sel_value, 0);
		if (ret)
			return (0);

		ret = ql_rdwr_indreg32(ha, read_addr, &read_value, 1);
		if (ret)
			return (0);

		*data_buff++ = sel_value;
		*data_buff++ = read_value;

		sel_value += muxEntry->select_value_stride;
	}

	return (loop_cnt * (2 * sizeof(uint32_t)));
}

static uint32_t
ql_rdmux2(qla_host_t *ha,
	ql_minidump_entry_mux2_t *muxEntry,
	uint32_t *data_buff)
{
	int ret;
        int loop_cnt;

        uint32_t select_addr_1, select_addr_2;
        uint32_t select_value_1, select_value_2;
        uint32_t select_value_count, select_value_mask;
        uint32_t read_addr, read_value;

        select_addr_1 = muxEntry->select_addr_1;
        select_addr_2 = muxEntry->select_addr_2;
        select_value_1 = muxEntry->select_value_1;
        select_value_2 = muxEntry->select_value_2;
        select_value_count = muxEntry->select_value_count;
        select_value_mask  = muxEntry->select_value_mask;

        read_addr = muxEntry->read_addr;

        for (loop_cnt = 0; loop_cnt < muxEntry->select_value_count;
		loop_cnt++) {

                uint32_t temp_sel_val;

		ret = ql_rdwr_indreg32(ha, select_addr_1, &select_value_1, 0);
		if (ret)
			return (0);

                temp_sel_val = select_value_1 & select_value_mask;

		ret = ql_rdwr_indreg32(ha, select_addr_2, &temp_sel_val, 0);
		if (ret)
			return (0);

		ret = ql_rdwr_indreg32(ha, read_addr, &read_value, 1);
		if (ret)
			return (0);

                *data_buff++ = temp_sel_val;
                *data_buff++ = read_value;

		ret = ql_rdwr_indreg32(ha, select_addr_1, &select_value_2, 0);
		if (ret)
			return (0);

                temp_sel_val = select_value_2 & select_value_mask;

		ret = ql_rdwr_indreg32(ha, select_addr_2, &temp_sel_val, 0);
		if (ret)
			return (0);

		ret = ql_rdwr_indreg32(ha, read_addr, &read_value, 1);
		if (ret)
			return (0);

                *data_buff++ = temp_sel_val;
                *data_buff++ = read_value;

                select_value_1 += muxEntry->select_value_stride;
                select_value_2 += muxEntry->select_value_stride;
        }

        return (loop_cnt * (4 * sizeof(uint32_t)));
}

/*
 * Handling Queue State Reads.
 */

static uint32_t 
ql_rdqueue(qla_host_t *ha,
	ql_minidump_entry_queue_t *queueEntry,
	uint32_t *data_buff)
{
	int ret;
	int loop_cnt, k;
	uint32_t read_value;
	uint32_t read_addr, read_stride, select_addr;
	uint32_t queue_id, read_cnt;

	read_cnt = queueEntry->read_addr_cnt;
	read_stride = queueEntry->read_addr_stride;
	select_addr = queueEntry->select_addr;

	for (loop_cnt = 0, queue_id = 0; loop_cnt < queueEntry->op_count;
		loop_cnt++) {

		ret = ql_rdwr_indreg32(ha, select_addr, &queue_id, 0);
		if (ret)
			return (0);

		read_addr = queueEntry->read_addr;

		for (k = 0; k < read_cnt; k++) {

			ret = ql_rdwr_indreg32(ha, read_addr, &read_value, 1);
			if (ret)
				return (0);

			*data_buff++ = read_value;
			read_addr += read_stride;
		}

		queue_id += queueEntry->queue_id_stride;
	}

	return (loop_cnt * (read_cnt * sizeof(uint32_t)));
}

/*
 * Handling control entries.
 */

static uint32_t 
ql_cntrl(qla_host_t *ha,
	ql_minidump_template_hdr_t *template_hdr,
	ql_minidump_entry_cntrl_t *crbEntry)
{
	int ret;
	int count;
	uint32_t opcode, read_value, addr, entry_addr;
	long timeout;

	entry_addr = crbEntry->addr;

	for (count = 0; count < crbEntry->op_count; count++) {
		opcode = crbEntry->opcode;

		if (opcode & QL_DBG_OPCODE_WR) {

                	ret = ql_rdwr_indreg32(ha, entry_addr,
					&crbEntry->value_1, 0);
			if (ret)
				return (0);

			opcode &= ~QL_DBG_OPCODE_WR;
		}

		if (opcode & QL_DBG_OPCODE_RW) {

                	ret = ql_rdwr_indreg32(ha, entry_addr, &read_value, 1);
			if (ret)
				return (0);

                	ret = ql_rdwr_indreg32(ha, entry_addr, &read_value, 0);
			if (ret)
				return (0);

			opcode &= ~QL_DBG_OPCODE_RW;
		}

		if (opcode & QL_DBG_OPCODE_AND) {

                	ret = ql_rdwr_indreg32(ha, entry_addr, &read_value, 1);
			if (ret)
				return (0);

			read_value &= crbEntry->value_2;
			opcode &= ~QL_DBG_OPCODE_AND;

			if (opcode & QL_DBG_OPCODE_OR) {
				read_value |= crbEntry->value_3;
				opcode &= ~QL_DBG_OPCODE_OR;
			}

                	ret = ql_rdwr_indreg32(ha, entry_addr, &read_value, 0);
			if (ret)
				return (0);
		}

		if (opcode & QL_DBG_OPCODE_OR) {

                	ret = ql_rdwr_indreg32(ha, entry_addr, &read_value, 1);
			if (ret)
				return (0);

			read_value |= crbEntry->value_3;

                	ret = ql_rdwr_indreg32(ha, entry_addr, &read_value, 0);
			if (ret)
				return (0);

			opcode &= ~QL_DBG_OPCODE_OR;
		}

		if (opcode & QL_DBG_OPCODE_POLL) {

			opcode &= ~QL_DBG_OPCODE_POLL;
			timeout = crbEntry->poll_timeout;
			addr = entry_addr;

                	ret = ql_rdwr_indreg32(ha, addr, &read_value, 1);
			if (ret)
				return (0);

			while ((read_value & crbEntry->value_2)
				!= crbEntry->value_1) {

				if (timeout) {
					qla_mdelay(__func__, 1);
					timeout--;
				} else
					break;

                		ret = ql_rdwr_indreg32(ha, addr,
						&read_value, 1);
				if (ret)
					return (0);
			}

			if (!timeout) {
				/*
				 * Report timeout error.
				 * core dump capture failed
				 * Skip remaining entries.
				 * Write buffer out to file
				 * Use driver specific fields in template header
				 * to report this error.
				 */
				return (-1);
			}
		}

		if (opcode & QL_DBG_OPCODE_RDSTATE) {
			/*
			 * decide which address to use.
			 */
			if (crbEntry->state_index_a) {
				addr = template_hdr->saved_state_array[
						crbEntry-> state_index_a];
			} else {
				addr = entry_addr;
			}

                	ret = ql_rdwr_indreg32(ha, addr, &read_value, 1);
			if (ret)
				return (0);

			template_hdr->saved_state_array[crbEntry->state_index_v]
					= read_value;
			opcode &= ~QL_DBG_OPCODE_RDSTATE;
		}

		if (opcode & QL_DBG_OPCODE_WRSTATE) {
			/*
			 * decide which value to use.
			 */
			if (crbEntry->state_index_v) {
				read_value = template_hdr->saved_state_array[
						crbEntry->state_index_v];
			} else {
				read_value = crbEntry->value_1;
			}
			/*
			 * decide which address to use.
			 */
			if (crbEntry->state_index_a) {
				addr = template_hdr->saved_state_array[
						crbEntry-> state_index_a];
			} else {
				addr = entry_addr;
			}

                	ret = ql_rdwr_indreg32(ha, addr, &read_value, 0);
			if (ret)
				return (0);

			opcode &= ~QL_DBG_OPCODE_WRSTATE;
		}

		if (opcode & QL_DBG_OPCODE_MDSTATE) {
			/*  Read value from saved state using index */
			read_value = template_hdr->saved_state_array[
						crbEntry->state_index_v];

			read_value <<= crbEntry->shl; /*Shift left operation */
			read_value >>= crbEntry->shr; /*Shift right operation */

			if (crbEntry->value_2) {
				/* check if AND mask is provided */
				read_value &= crbEntry->value_2;
			}

			read_value |= crbEntry->value_3; /* OR operation */
			read_value += crbEntry->value_1; /* increment op */

			/* Write value back to state area. */

			template_hdr->saved_state_array[crbEntry->state_index_v]
					= read_value;
			opcode &= ~QL_DBG_OPCODE_MDSTATE;
		}

		entry_addr += crbEntry->addr_stride;
	}

	return (0);
}

/*
 * Handling rd poll entry.
 */

static uint32_t 
ql_pollrd(qla_host_t *ha, ql_minidump_entry_pollrd_t *entry,
	uint32_t *data_buff)
{
        int ret;
        int loop_cnt;
        uint32_t op_count, select_addr, select_value_stride, select_value;
        uint32_t read_addr, poll, mask, data_size, data;
        uint32_t wait_count = 0;

        select_addr            = entry->select_addr;
        read_addr              = entry->read_addr;
        select_value           = entry->select_value;
        select_value_stride    = entry->select_value_stride;
        op_count               = entry->op_count;
        poll                   = entry->poll;
        mask                   = entry->mask;
        data_size              = entry->data_size;

        for (loop_cnt = 0; loop_cnt < op_count; loop_cnt++) {

                ret = ql_rdwr_indreg32(ha, select_addr, &select_value, 0);
		if (ret)
			return (0);

                wait_count = 0;

                while (wait_count < poll) {

                        uint32_t temp;

			ret = ql_rdwr_indreg32(ha, select_addr, &temp, 1);
			if (ret)
				return (0);

                        if ( (temp & mask) != 0 ) {
                                break;
                        }
                        wait_count++;
                }

                if (wait_count == poll) {
                        device_printf(ha->pci_dev,
				"%s: Error in processing entry\n", __func__);
                        device_printf(ha->pci_dev,
				"%s: wait_count <0x%x> poll <0x%x>\n",
				__func__, wait_count, poll);
                        return 0;
                }

		ret = ql_rdwr_indreg32(ha, read_addr, &data, 1);
		if (ret)
			return (0);

                *data_buff++ = select_value;
                *data_buff++ = data;
                select_value = select_value + select_value_stride;
        }

        /*
         * for testing purpose we return amount of data written
         */
        return (loop_cnt * (2 * sizeof(uint32_t)));
}


/*
 * Handling rd modify write poll entry.
 */

static uint32_t 
ql_pollrd_modify_write(qla_host_t *ha,
	ql_minidump_entry_rd_modify_wr_with_poll_t *entry,
	uint32_t *data_buff)
{
	int ret;
        uint32_t addr_1, addr_2, value_1, value_2, data;
        uint32_t poll, mask, data_size, modify_mask;
        uint32_t wait_count = 0;

        addr_1		= entry->addr_1;
        addr_2		= entry->addr_2;
        value_1		= entry->value_1;
        value_2		= entry->value_2;

        poll		= entry->poll;
        mask		= entry->mask;
        modify_mask	= entry->modify_mask;
        data_size	= entry->data_size;


	ret = ql_rdwr_indreg32(ha, addr_1, &value_1, 0);
	if (ret)
		return (0);

        wait_count = 0;
        while (wait_count < poll) {

		uint32_t temp;

		ret = ql_rdwr_indreg32(ha, addr_1, &temp, 1);
		if (ret)
			return (0);

                if ( (temp & mask) != 0 ) {
                        break;
                }
                wait_count++;
        }

        if (wait_count == poll) {
                device_printf(ha->pci_dev, "%s Error in processing entry\n",
			__func__);
        } else {

		ret = ql_rdwr_indreg32(ha, addr_2, &data, 1);
		if (ret)
			return (0);

                data = (data & modify_mask);

		ret = ql_rdwr_indreg32(ha, addr_2, &data, 0);
		if (ret)
			return (0);

		ret = ql_rdwr_indreg32(ha, addr_1, &value_2, 0);
		if (ret)
			return (0);

                /* Poll again */
                wait_count = 0;
                while (wait_count < poll) {

                        uint32_t temp;

			ret = ql_rdwr_indreg32(ha, addr_1, &temp, 1);
			if (ret)
				return (0);

                        if ( (temp & mask) != 0 ) {
                                break;
                        }
                        wait_count++;
                }
                *data_buff++ = addr_2;
                *data_buff++ = data;
        }

        /*
         * for testing purpose we return amount of data written
         */
        return (2 * sizeof(uint32_t));
}


