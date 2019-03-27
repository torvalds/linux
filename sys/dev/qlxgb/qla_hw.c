/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011-2012 Qlogic Corporation
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
 * File: qla_hw.c
 * Author : David C Somayajulu, Qlogic Corporation, Aliso Viejo, CA 92656.
 * Content: Contains Hardware dependent functions
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "qla_os.h"
#include "qla_reg.h"
#include "qla_hw.h"
#include "qla_def.h"
#include "qla_inline.h"
#include "qla_ver.h"
#include "qla_glbl.h"
#include "qla_dbg.h"

static uint32_t sysctl_num_rds_rings = 2;
static uint32_t sysctl_num_sds_rings = 4;

/*
 * Static Functions
 */

static void qla_init_cntxt_regions(qla_host_t *ha);
static int qla_issue_cmd(qla_host_t *ha, qla_cdrp_t *cdrp);
static int qla_fw_cmd(qla_host_t *ha, void *fw_cmd, uint32_t size);
static int qla_config_mac_addr(qla_host_t *ha, uint8_t *mac_addr,
		uint16_t cntxt_id, uint32_t add_multi);
static void qla_del_rcv_cntxt(qla_host_t *ha);
static int qla_init_rcv_cntxt(qla_host_t *ha);
static void qla_del_xmt_cntxt(qla_host_t *ha);
static int qla_init_xmt_cntxt(qla_host_t *ha);
static int qla_get_max_rds(qla_host_t *ha);
static int qla_get_max_sds(qla_host_t *ha);
static int qla_get_max_rules(qla_host_t *ha);
static int qla_get_max_rcv_cntxts(qla_host_t *ha);
static int qla_get_max_tx_cntxts(qla_host_t *ha);
static int qla_get_max_mtu(qla_host_t *ha);
static int qla_get_max_lro(qla_host_t *ha);
static int qla_get_flow_control(qla_host_t *ha);
static void qla_hw_tx_done_locked(qla_host_t *ha);

int
qla_get_msix_count(qla_host_t *ha)
{
	return (sysctl_num_sds_rings);
}

/*
 * Name: qla_hw_add_sysctls
 * Function: Add P3Plus specific sysctls
 */
void
qla_hw_add_sysctls(qla_host_t *ha)
{
        device_t	dev;

        dev = ha->pci_dev;

        SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "num_rds_rings", CTLFLAG_RD, &sysctl_num_rds_rings,
		sysctl_num_rds_rings, "Number of Rcv Descriptor Rings");

        SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "num_sds_rings", CTLFLAG_RD, &sysctl_num_sds_rings,
		sysctl_num_sds_rings, "Number of Status Descriptor Rings");
}

/*
 * Name: qla_free_dma
 * Function: Frees the DMA'able memory allocated in qla_alloc_dma()
 */
void
qla_free_dma(qla_host_t *ha)
{
	uint32_t i;

        if (ha->hw.dma_buf.flags.context) {
		qla_free_dmabuf(ha, &ha->hw.dma_buf.context);
        	ha->hw.dma_buf.flags.context = 0;
	}

        if (ha->hw.dma_buf.flags.sds_ring) {
		for (i = 0; i < ha->hw.num_sds_rings; i++)
			qla_free_dmabuf(ha, &ha->hw.dma_buf.sds_ring[i]);
        	ha->hw.dma_buf.flags.sds_ring = 0;
	}

        if (ha->hw.dma_buf.flags.rds_ring) {
		for (i = 0; i < ha->hw.num_rds_rings; i++)
			qla_free_dmabuf(ha, &ha->hw.dma_buf.rds_ring[i]);
        	ha->hw.dma_buf.flags.rds_ring = 0;
	}

        if (ha->hw.dma_buf.flags.tx_ring) {
		qla_free_dmabuf(ha, &ha->hw.dma_buf.tx_ring);
        	ha->hw.dma_buf.flags.tx_ring = 0;
	}
}

/*
 * Name: qla_alloc_dma
 * Function: Allocates DMA'able memory for Tx/Rx Rings, Tx/Rx Contexts.
 */
int
qla_alloc_dma(qla_host_t *ha)
{
        device_t                dev;
	uint32_t		i, j, size;

        dev = ha->pci_dev;

        QL_DPRINT2((dev, "%s: enter\n", __func__));

	ha->hw.num_rds_rings = (uint16_t)sysctl_num_rds_rings;
	ha->hw.num_sds_rings = (uint16_t)sysctl_num_sds_rings;

	/*
	 * Allocate Transmit Ring
	 */

	ha->hw.dma_buf.tx_ring.alignment = 8;
	ha->hw.dma_buf.tx_ring.size =
		(sizeof(q80_tx_cmd_t)) * NUM_TX_DESCRIPTORS;
	
        if (qla_alloc_dmabuf(ha, &ha->hw.dma_buf.tx_ring)) {
                device_printf(dev, "%s: tx ring alloc failed\n", __func__);
                goto qla_alloc_dma_exit;
        }
        ha->hw.dma_buf.flags.tx_ring = 1;

	QL_DPRINT2((dev, "%s: tx_ring phys %p virt %p\n",
		__func__, (void *)(ha->hw.dma_buf.tx_ring.dma_addr),
		ha->hw.dma_buf.tx_ring.dma_b));
	/*
	 * Allocate Receive Descriptor Rings
	 */

	for (i = 0; i < ha->hw.num_rds_rings; i++) {
		ha->hw.dma_buf.rds_ring[i].alignment = 8;

		if (i == RDS_RING_INDEX_NORMAL) {
			ha->hw.dma_buf.rds_ring[i].size =
				(sizeof(q80_recv_desc_t)) * NUM_RX_DESCRIPTORS;
		} else if (i == RDS_RING_INDEX_JUMBO) {
			ha->hw.dma_buf.rds_ring[i].size = 
				(sizeof(q80_recv_desc_t)) *
					NUM_RX_JUMBO_DESCRIPTORS;
		} else
			break;
	
		if (qla_alloc_dmabuf(ha, &ha->hw.dma_buf.rds_ring[i])) {
			QL_DPRINT4((dev, "%s: rds ring alloc failed\n",
				__func__));

			for (j = 0; j < i; j++)
				qla_free_dmabuf(ha,
					&ha->hw.dma_buf.rds_ring[j]);

			goto qla_alloc_dma_exit;
		}
		QL_DPRINT4((dev, "%s: rx_ring[%d] phys %p virt %p\n",
			__func__, i,
			(void *)(ha->hw.dma_buf.rds_ring[i].dma_addr),
			ha->hw.dma_buf.rds_ring[i].dma_b));
	}
	ha->hw.dma_buf.flags.rds_ring = 1;

	/*
	 * Allocate Status Descriptor Rings
	 */

	for (i = 0; i < ha->hw.num_sds_rings; i++) {
		ha->hw.dma_buf.sds_ring[i].alignment = 8;
		ha->hw.dma_buf.sds_ring[i].size =
			(sizeof(q80_stat_desc_t)) * NUM_STATUS_DESCRIPTORS;

		if (qla_alloc_dmabuf(ha, &ha->hw.dma_buf.sds_ring[i])) {
			device_printf(dev, "%s: sds ring alloc failed\n",
				__func__);

			for (j = 0; j < i; j++)
				qla_free_dmabuf(ha,
					&ha->hw.dma_buf.sds_ring[j]);

			goto qla_alloc_dma_exit;
		}
		QL_DPRINT4((dev, "%s: sds_ring[%d] phys %p virt %p\n",
			__func__, i,
			(void *)(ha->hw.dma_buf.sds_ring[i].dma_addr),
			ha->hw.dma_buf.sds_ring[i].dma_b));
	}
	ha->hw.dma_buf.flags.sds_ring = 1;

	/*
	 * Allocate Context Area
	 */
	size = QL_ALIGN((sizeof (q80_tx_cntxt_req_t)), QL_BUFFER_ALIGN);

	size += QL_ALIGN((sizeof (q80_tx_cntxt_rsp_t)), QL_BUFFER_ALIGN);

	size += QL_ALIGN((sizeof (q80_rcv_cntxt_req_t)), QL_BUFFER_ALIGN);

	size += QL_ALIGN((sizeof (q80_rcv_cntxt_rsp_t)), QL_BUFFER_ALIGN);

	size += sizeof (uint32_t); /* for tx consumer index */

	size = QL_ALIGN(size, PAGE_SIZE);
	
	ha->hw.dma_buf.context.alignment = 8;
	ha->hw.dma_buf.context.size = size;
	
        if (qla_alloc_dmabuf(ha, &ha->hw.dma_buf.context)) {
                device_printf(dev, "%s: context alloc failed\n", __func__);
                goto qla_alloc_dma_exit;
        }
        ha->hw.dma_buf.flags.context = 1;
	QL_DPRINT2((dev, "%s: context phys %p virt %p\n",
		__func__, (void *)(ha->hw.dma_buf.context.dma_addr),
		ha->hw.dma_buf.context.dma_b));

	qla_init_cntxt_regions(ha);

	return 0;

qla_alloc_dma_exit:
	qla_free_dma(ha);
	return -1;
}

/*
 * Name: qla_init_cntxt_regions
 * Function: Initializes Tx/Rx Contexts.
 */
static void
qla_init_cntxt_regions(qla_host_t *ha)
{
	qla_hw_t		*hw;
	q80_tx_cntxt_req_t	*tx_cntxt_req;
	q80_rcv_cntxt_req_t	*rx_cntxt_req;
	bus_addr_t		phys_addr;
	uint32_t		i;
        device_t                dev;
	uint32_t		size;

        dev = ha->pci_dev;

	hw = &ha->hw;

	hw->tx_ring_base = hw->dma_buf.tx_ring.dma_b;
	
	for (i = 0; i < ha->hw.num_sds_rings; i++)
		hw->sds[i].sds_ring_base =
			(q80_stat_desc_t *)hw->dma_buf.sds_ring[i].dma_b;


	phys_addr = hw->dma_buf.context.dma_addr;

	memset((void *)hw->dma_buf.context.dma_b, 0,
		ha->hw.dma_buf.context.size);

	hw->tx_cntxt_req	=
		(q80_tx_cntxt_req_t *)hw->dma_buf.context.dma_b;
	hw->tx_cntxt_req_paddr	= phys_addr;

	size = QL_ALIGN((sizeof (q80_tx_cntxt_req_t)), QL_BUFFER_ALIGN);

	hw->tx_cntxt_rsp	=
		(q80_tx_cntxt_rsp_t *)((uint8_t *)hw->tx_cntxt_req + size);
	hw->tx_cntxt_rsp_paddr	= hw->tx_cntxt_req_paddr + size;

	size = QL_ALIGN((sizeof (q80_tx_cntxt_rsp_t)), QL_BUFFER_ALIGN);

	hw->rx_cntxt_req =
		(q80_rcv_cntxt_req_t *)((uint8_t *)hw->tx_cntxt_rsp + size);
	hw->rx_cntxt_req_paddr = hw->tx_cntxt_rsp_paddr + size;

	size = QL_ALIGN((sizeof (q80_rcv_cntxt_req_t)), QL_BUFFER_ALIGN);

	hw->rx_cntxt_rsp =
		(q80_rcv_cntxt_rsp_t *)((uint8_t *)hw->rx_cntxt_req + size);
	hw->rx_cntxt_rsp_paddr = hw->rx_cntxt_req_paddr + size;

	size = QL_ALIGN((sizeof (q80_rcv_cntxt_rsp_t)), QL_BUFFER_ALIGN);

	hw->tx_cons = (uint32_t *)((uint8_t *)hw->rx_cntxt_rsp + size);
	hw->tx_cons_paddr = hw->rx_cntxt_rsp_paddr + size;

	/*
	 * Initialize the Transmit Context Request so that we don't need to
	 * do it every time we need to create a context
	 */
	tx_cntxt_req = hw->tx_cntxt_req;

	tx_cntxt_req->rsp_dma_addr = qla_host_to_le64(hw->tx_cntxt_rsp_paddr);

	tx_cntxt_req->cmd_cons_dma_addr = qla_host_to_le64(hw->tx_cons_paddr);

	tx_cntxt_req->caps[0] = qla_host_to_le32((CNTXT_CAP0_BASEFW |
					CNTXT_CAP0_LEGACY_MN | CNTXT_CAP0_LSO));
	
	tx_cntxt_req->intr_mode = qla_host_to_le32(CNTXT_INTR_MODE_SHARED);

	tx_cntxt_req->phys_addr =
		qla_host_to_le64(hw->dma_buf.tx_ring.dma_addr);

	tx_cntxt_req->num_entries = qla_host_to_le32(NUM_TX_DESCRIPTORS);

	/*
	 * Initialize the Receive Context Request
	 */

	rx_cntxt_req = hw->rx_cntxt_req;

	rx_cntxt_req->rx_req.rsp_dma_addr =
		qla_host_to_le64(hw->rx_cntxt_rsp_paddr);

	rx_cntxt_req->rx_req.caps[0] = qla_host_to_le32(CNTXT_CAP0_BASEFW |
						CNTXT_CAP0_LEGACY_MN |
						CNTXT_CAP0_JUMBO |
						CNTXT_CAP0_LRO|
						CNTXT_CAP0_HW_LRO);

	rx_cntxt_req->rx_req.intr_mode =
		qla_host_to_le32(CNTXT_INTR_MODE_SHARED);

	rx_cntxt_req->rx_req.rds_intr_mode =
		qla_host_to_le32(CNTXT_INTR_MODE_UNIQUE);

	rx_cntxt_req->rx_req.rds_ring_offset = 0;
	rx_cntxt_req->rx_req.sds_ring_offset = qla_host_to_le32(
		(hw->num_rds_rings * sizeof(q80_rq_rds_ring_t)));
	rx_cntxt_req->rx_req.num_rds_rings =
		qla_host_to_le16(hw->num_rds_rings);
	rx_cntxt_req->rx_req.num_sds_rings =
		qla_host_to_le16(hw->num_sds_rings);

	for (i = 0; i < hw->num_rds_rings; i++) {
		rx_cntxt_req->rds_req[i].phys_addr =
			qla_host_to_le64(hw->dma_buf.rds_ring[i].dma_addr);

		if (i == RDS_RING_INDEX_NORMAL) {
			rx_cntxt_req->rds_req[i].buf_size =
				qla_host_to_le64(MCLBYTES);
			rx_cntxt_req->rds_req[i].size =
				qla_host_to_le32(NUM_RX_DESCRIPTORS);
		} else {
			rx_cntxt_req->rds_req[i].buf_size =
				qla_host_to_le64(MJUM9BYTES);
			rx_cntxt_req->rds_req[i].size =
				qla_host_to_le32(NUM_RX_JUMBO_DESCRIPTORS);
		}
	}

	for (i = 0; i < hw->num_sds_rings; i++) {
		rx_cntxt_req->sds_req[i].phys_addr =
			qla_host_to_le64(hw->dma_buf.sds_ring[i].dma_addr);
		rx_cntxt_req->sds_req[i].size =
			qla_host_to_le32(NUM_STATUS_DESCRIPTORS);
		rx_cntxt_req->sds_req[i].msi_index = qla_host_to_le16(i);
	}

	QL_DPRINT2((ha->pci_dev, "%s: tx_cntxt_req = %p paddr %p\n",
		__func__, hw->tx_cntxt_req, (void *)hw->tx_cntxt_req_paddr));
	QL_DPRINT2((ha->pci_dev, "%s: tx_cntxt_rsp = %p paddr %p\n",
		__func__, hw->tx_cntxt_rsp, (void *)hw->tx_cntxt_rsp_paddr));
	QL_DPRINT2((ha->pci_dev, "%s: rx_cntxt_req = %p paddr %p\n",
		__func__, hw->rx_cntxt_req, (void *)hw->rx_cntxt_req_paddr));
	QL_DPRINT2((ha->pci_dev, "%s: rx_cntxt_rsp = %p paddr %p\n",
		__func__, hw->rx_cntxt_rsp, (void *)hw->rx_cntxt_rsp_paddr));
	QL_DPRINT2((ha->pci_dev, "%s: tx_cons      = %p paddr %p\n",
		__func__, hw->tx_cons, (void *)hw->tx_cons_paddr));
}

/*
 * Name: qla_issue_cmd
 * Function: Issues commands on the CDRP interface and returns responses.
 */
static int
qla_issue_cmd(qla_host_t *ha, qla_cdrp_t *cdrp)
{
	int	ret = 0;
	uint32_t signature;
	uint32_t count = 400; /* 4 seconds or 400 10ms intervals */
	uint32_t data;
	device_t dev;

	dev = ha->pci_dev;

	signature = 0xcafe0000 | 0x0100 | ha->pci_func;

	ret = qla_sem_lock(ha, Q8_SEM5_LOCK, 0, (uint32_t)ha->pci_func);
	
	if (ret) {
		device_printf(dev, "%s: SEM5_LOCK lock failed\n", __func__);
		return (ret);
	}

	WRITE_OFFSET32(ha, Q8_NX_CDRP_SIGNATURE, signature);

	WRITE_OFFSET32(ha, Q8_NX_CDRP_ARG1, (cdrp->cmd_arg1));
	WRITE_OFFSET32(ha, Q8_NX_CDRP_ARG2, (cdrp->cmd_arg2));
	WRITE_OFFSET32(ha, Q8_NX_CDRP_ARG3, (cdrp->cmd_arg3));

	WRITE_OFFSET32(ha, Q8_NX_CDRP_CMD_RSP, cdrp->cmd);

	while (count) {
		qla_mdelay(__func__, 10);

		data = READ_REG32(ha, Q8_NX_CDRP_CMD_RSP);

		if ((!(data & 0x80000000)))
			break;
		count--;
	}
	if ((!count) || (data != 1))
		ret = -1;

	cdrp->rsp = READ_REG32(ha, Q8_NX_CDRP_CMD_RSP);
	cdrp->rsp_arg1 = READ_REG32(ha, Q8_NX_CDRP_ARG1);
	cdrp->rsp_arg2 = READ_REG32(ha, Q8_NX_CDRP_ARG2);
	cdrp->rsp_arg3 = READ_REG32(ha, Q8_NX_CDRP_ARG3);

	qla_sem_unlock(ha, Q8_SEM5_UNLOCK);

	if (ret) {
		device_printf(dev, "%s: "
			"cmd[0x%08x] = 0x%08x\n"
			"\tsig[0x%08x] = 0x%08x\n"
			"\targ1[0x%08x] = 0x%08x\n"
			"\targ2[0x%08x] = 0x%08x\n"
			"\targ3[0x%08x] = 0x%08x\n",
			__func__, Q8_NX_CDRP_CMD_RSP, cdrp->cmd,
			Q8_NX_CDRP_SIGNATURE, signature,
			Q8_NX_CDRP_ARG1, cdrp->cmd_arg1,
			Q8_NX_CDRP_ARG2, cdrp->cmd_arg2,
			Q8_NX_CDRP_ARG3, cdrp->cmd_arg3);
		
		device_printf(dev, "%s: exit (ret = 0x%x)\n"
			"\t\t rsp = 0x%08x\n"
			"\t\t arg1 = 0x%08x\n"
			"\t\t arg2 = 0x%08x\n"
			"\t\t arg3 = 0x%08x\n",
			__func__, ret, cdrp->rsp,
			cdrp->rsp_arg1, cdrp->rsp_arg2, cdrp->rsp_arg3);
	}

	return (ret);
}

#define QLA_TX_MIN_FREE	2

/*
 * Name: qla_fw_cmd
 * Function: Issues firmware control commands on the Tx Ring.
 */
static int
qla_fw_cmd(qla_host_t *ha, void *fw_cmd, uint32_t size)
{
	device_t dev;
        q80_tx_cmd_t *tx_cmd;
        qla_hw_t *hw = &ha->hw;
	int count = 100;

	dev = ha->pci_dev;

	QLA_TX_LOCK(ha);

        if (hw->txr_free <= QLA_TX_MIN_FREE) {
		while (count--) {
			qla_hw_tx_done_locked(ha);
			if (hw->txr_free > QLA_TX_MIN_FREE)
				break;

			QLA_TX_UNLOCK(ha);
			qla_mdelay(__func__, 10);
			QLA_TX_LOCK(ha);
		}
        	if (hw->txr_free <= QLA_TX_MIN_FREE) {
			QLA_TX_UNLOCK(ha);
			device_printf(dev, "%s: xmit queue full\n", __func__);
                	return (-1);
		}
        }
        tx_cmd = &hw->tx_ring_base[hw->txr_next];

        bzero((void *)tx_cmd, sizeof(q80_tx_cmd_t));

	bcopy(fw_cmd, tx_cmd, size);

	hw->txr_next = (hw->txr_next + 1) & (NUM_TX_DESCRIPTORS - 1);
	hw->txr_free--;

	QL_UPDATE_TX_PRODUCER_INDEX(ha, hw->txr_next);

	QLA_TX_UNLOCK(ha);

	return (0);
}

/*
 * Name: qla_config_rss
 * Function: Configure RSS for the context/interface.
 */
const uint64_t rss_key[] = { 0xbeac01fa6a42b73bULL, 0x8030f20c77cb2da3ULL,
			0xae7b30b4d0ca2bcbULL, 0x43a38fb04167253dULL,
			0x255b0ec26d5a56daULL };

static int
qla_config_rss(qla_host_t *ha, uint16_t cntxt_id)
{
	qla_fw_cds_config_rss_t rss_config;
	int ret, i;

	bzero(&rss_config, sizeof(qla_fw_cds_config_rss_t));

	rss_config.hdr.cmd = Q8_FWCD_CNTRL_REQ;
	rss_config.hdr.opcode = Q8_FWCD_OPCODE_CONFIG_RSS;
	rss_config.hdr.cntxt_id = cntxt_id;

	rss_config.hash_type = (Q8_FWCD_RSS_HASH_TYPE_IPV4_TCP_IP |
					Q8_FWCD_RSS_HASH_TYPE_IPV6_TCP_IP);
	rss_config.flags = Q8_FWCD_RSS_FLAGS_ENABLE_RSS;

	rss_config.ind_tbl_mask = 0x7;
	
	for (i = 0; i < 5; i++)
		rss_config.rss_key[i] = rss_key[i];

	ret = qla_fw_cmd(ha, &rss_config, sizeof(qla_fw_cds_config_rss_t));

	return ret;
}

/*
 * Name: qla_config_intr_coalesce
 * Function: Configure Interrupt Coalescing.
 */
static int
qla_config_intr_coalesce(qla_host_t *ha, uint16_t cntxt_id, int tenable)
{
	qla_fw_cds_config_intr_coalesc_t intr_coalesce;
	int ret;

	bzero(&intr_coalesce, sizeof(qla_fw_cds_config_intr_coalesc_t));

	intr_coalesce.hdr.cmd = Q8_FWCD_CNTRL_REQ;
	intr_coalesce.hdr.opcode = Q8_FWCD_OPCODE_CONFIG_INTR_COALESCING;
	intr_coalesce.hdr.cntxt_id = cntxt_id;
	
	intr_coalesce.flags = 0x04;
	intr_coalesce.max_rcv_pkts = 256;
	intr_coalesce.max_rcv_usecs = 3;
	intr_coalesce.max_snd_pkts = 64;
	intr_coalesce.max_snd_usecs = 4;

	if (tenable) {
		intr_coalesce.usecs_to = 1000; /* 1 millisecond */
		intr_coalesce.timer_type = Q8_FWCMD_INTR_COALESC_TIMER_PERIODIC;
		intr_coalesce.sds_ring_bitmask =
			Q8_FWCMD_INTR_COALESC_SDS_RING_0;
	}

	ret = qla_fw_cmd(ha, &intr_coalesce,
			sizeof(qla_fw_cds_config_intr_coalesc_t));

	return ret;
}


/*
 * Name: qla_config_mac_addr
 * Function: binds a MAC address to the context/interface.
 *	Can be unicast, multicast or broadcast.
 */
static int
qla_config_mac_addr(qla_host_t *ha, uint8_t *mac_addr, uint16_t cntxt_id,
	uint32_t add_multi)
{
	qla_fw_cds_config_mac_addr_t mac_config;
	int ret;

//	device_printf(ha->pci_dev,
//		"%s: mac_addr %02x:%02x:%02x:%02x:%02x:%02x\n", __func__,
//		mac_addr[0], mac_addr[1], mac_addr[2],
//		mac_addr[3], mac_addr[4], mac_addr[5]);

	bzero(&mac_config, sizeof(qla_fw_cds_config_mac_addr_t));

	mac_config.hdr.cmd = Q8_FWCD_CNTRL_REQ;
	mac_config.hdr.opcode = Q8_FWCD_OPCODE_CONFIG_MAC_ADDR;
	mac_config.hdr.cntxt_id = cntxt_id;
	
	if (add_multi)
		mac_config.cmd = Q8_FWCD_ADD_MAC_ADDR;
	else
		mac_config.cmd = Q8_FWCD_DEL_MAC_ADDR;
	bcopy(mac_addr, mac_config.mac_addr,6); 

	ret = qla_fw_cmd(ha, &mac_config, sizeof(qla_fw_cds_config_mac_addr_t));

	return ret;
}


/*
 * Name: qla_set_mac_rcv_mode
 * Function: Enable/Disable AllMulticast and Promiscuous Modes.
 */
static int
qla_set_mac_rcv_mode(qla_host_t *ha, uint16_t cntxt_id, uint32_t mode)
{
	qla_set_mac_rcv_mode_t rcv_mode;
	int ret;

	bzero(&rcv_mode, sizeof(qla_set_mac_rcv_mode_t));

	rcv_mode.hdr.cmd = Q8_FWCD_CNTRL_REQ;
	rcv_mode.hdr.opcode = Q8_FWCD_OPCODE_CONFIG_MAC_RCV_MODE;
	rcv_mode.hdr.cntxt_id = cntxt_id;
	
	rcv_mode.mode = mode;

	ret = qla_fw_cmd(ha, &rcv_mode, sizeof(qla_set_mac_rcv_mode_t));

	return ret;
}

void
qla_set_promisc(qla_host_t *ha)
{
	(void)qla_set_mac_rcv_mode(ha,
		(ha->hw.rx_cntxt_rsp)->rx_rsp.cntxt_id,
		Q8_MAC_RCV_ENABLE_PROMISCUOUS);
}

void
qla_set_allmulti(qla_host_t *ha)
{
	(void)qla_set_mac_rcv_mode(ha,
		(ha->hw.rx_cntxt_rsp)->rx_rsp.cntxt_id,
		Q8_MAC_RCV_ENABLE_ALLMULTI);
}

void
qla_reset_promisc_allmulti(qla_host_t *ha)
{
	(void)qla_set_mac_rcv_mode(ha,
		(ha->hw.rx_cntxt_rsp)->rx_rsp.cntxt_id,
		Q8_MAC_RCV_RESET_PROMISC_ALLMULTI);
}

/*
 * Name: qla_config_ipv4_addr
 * Function: Configures the Destination IP Addr for LRO.
 */
void
qla_config_ipv4_addr(qla_host_t *ha, uint32_t ipv4_addr)
{
	qla_config_ipv4_t ip_conf;

	bzero(&ip_conf, sizeof(qla_config_ipv4_t));

	ip_conf.hdr.cmd = Q8_FWCD_CNTRL_REQ;
	ip_conf.hdr.opcode = Q8_FWCD_OPCODE_CONFIG_IPADDR;
	ip_conf.hdr.cntxt_id = (ha->hw.rx_cntxt_rsp)->rx_rsp.cntxt_id;
	
	ip_conf.cmd = (uint64_t)Q8_CONFIG_CMD_IP_ENABLE;
	ip_conf.ipv4_addr = (uint64_t)ipv4_addr;

	(void)qla_fw_cmd(ha, &ip_conf, sizeof(qla_config_ipv4_t));

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
	struct tcphdr *th = NULL;
	uint32_t ehdrlen,  hdrlen = 0, ip_hlen, tcp_hlen, tcp_opt_off;
	uint16_t etype, opcode, offload = 1;
	uint8_t *tcp_opt;
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

	switch (etype) {
		case ETHERTYPE_IP:

			tcp_opt_off = ehdrlen + sizeof(struct ip) +
					sizeof(struct tcphdr);

			if (mp->m_len < tcp_opt_off) {
				m_copydata(mp, 0, tcp_opt_off, hdr);
				ip = (struct ip *)hdr;
			} else {
				ip = (struct ip *)(mp->m_data + ehdrlen);
			}

			ip_hlen = ip->ip_hl << 2;
			opcode = Q8_TX_CMD_OP_XMT_TCP_LSO;

			if ((ip->ip_p != IPPROTO_TCP) ||
				(ip_hlen != sizeof (struct ip))) {
				offload = 0;
			} else {
				th = (struct tcphdr *)((caddr_t)ip + ip_hlen);
			}
		break;

		default:
			QL_DPRINT8((dev, "%s: type!=ip\n", __func__));
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

	if ((mp->m_pkthdr.csum_flags & CSUM_TSO) == 0) {

		/* If TCP options are preset only time stamp option is supported */
		if ((tcp_hlen - sizeof(struct tcphdr)) != 10) 
			return -1;
		else {

			if (mp->m_len < hdrlen) {
				tcp_opt = &hdr[tcp_opt_off];
			} else {
				tcp_opt = (uint8_t *)(mp->m_data + tcp_opt_off);
			}

			if ((*tcp_opt != 0x01) || (*(tcp_opt + 1) != 0x01) ||
				(*(tcp_opt + 2) != 0x08) ||
				(*(tcp_opt + 3) != 10)) {
				return -1;
			}
		}

		tx_cmd->mss = ha->max_frame_size - ETHER_CRC_LEN - hdrlen;
	} else {
		tx_cmd->mss = mp->m_pkthdr.tso_segsz;
	}

	tx_cmd->flags_opcode = opcode ;
	tx_cmd->tcp_hdr_off = ip_hlen + ehdrlen;
	tx_cmd->ip_hdr_off = ehdrlen;
	tx_cmd->mss = mp->m_pkthdr.tso_segsz;
	tx_cmd->total_hdr_len = hdrlen;

	/* Check for Multicast least significant bit of MSB == 1 */
	if (eh->evl_dhost[0] & 0x01) {
		tx_cmd->flags_opcode = Q8_TX_CMD_FLAGS_MULTICAST;
	}

	if (mp->m_len < hdrlen) {
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
qla_tx_chksum(qla_host_t *ha, struct mbuf *mp, q80_tx_cmd_t *tx_cmd)
{
	struct ether_vlan_header *eh;
	struct ip *ip;
	struct ip6_hdr *ip6;
	uint32_t ehdrlen, ip_hlen;
	uint16_t etype, opcode, offload = 1;
	device_t dev;

	dev = ha->pci_dev;

	if ((mp->m_pkthdr.csum_flags & (CSUM_TCP|CSUM_UDP)) == 0)
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
				device_printf(dev, "%s: ipv4 mlen\n", __func__);
				offload = 0;
				break;
			}

			if (ip->ip_p == IPPROTO_TCP)
				opcode = Q8_TX_CMD_OP_XMT_TCP_CHKSUM;
			else if (ip->ip_p == IPPROTO_UDP)
				opcode = Q8_TX_CMD_OP_XMT_UDP_CHKSUM;
			else {
				device_printf(dev, "%s: ipv4\n", __func__);
				offload = 0;
			}
		break;

		case ETHERTYPE_IPV6:
			ip6 = (struct ip6_hdr *)(mp->m_data + ehdrlen);

			ip_hlen = sizeof(struct ip6_hdr);

			if (mp->m_len < (ehdrlen + ip_hlen)) {
				device_printf(dev, "%s: ipv6 mlen\n", __func__);
				offload = 0;
				break;
			}

			if (ip6->ip6_nxt == IPPROTO_TCP)
				opcode = Q8_TX_CMD_OP_XMT_TCP_CHKSUM_IPV6;
			else if (ip6->ip6_nxt == IPPROTO_UDP)
				opcode = Q8_TX_CMD_OP_XMT_UDP_CHKSUM_IPV6;
			else {
				device_printf(dev, "%s: ipv6\n", __func__);
				offload = 0;
			}
		break;

		default:
			offload = 0;
		break;
	}
	if (!offload)
		return (-1);

	tx_cmd->flags_opcode = opcode;

	tx_cmd->tcp_hdr_off = ip_hlen + ehdrlen;

	return (0);
}

/*
 * Name: qla_hw_send
 * Function: Transmits a packet. It first checks if the packet is a
 *	candidate for Large TCP Segment Offload and then for UDP/TCP checksum
 *	offload. If either of these creteria are not met, it is transmitted
 *	as a regular ethernet frame.
 */
int
qla_hw_send(qla_host_t *ha, bus_dma_segment_t *segs, int nsegs,
	uint32_t *tx_idx,  struct mbuf *mp)
{
	struct ether_vlan_header *eh;
	qla_hw_t *hw = &ha->hw;
	q80_tx_cmd_t *tx_cmd, tso_cmd;
	bus_dma_segment_t *c_seg;
	uint32_t num_tx_cmds, hdr_len = 0;
	uint32_t total_length = 0, bytes, tx_cmd_count = 0;
	device_t dev;
	int i, ret;
	uint8_t *src = NULL, *dst = NULL;

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
		return (-1);
	}
	eh = mtod(mp, struct ether_vlan_header *);

	if ((mp->m_pkthdr.len > ha->max_frame_size)||(nsegs > Q8_TX_MAX_SEGMENTS)) {

		bzero((void *)&tso_cmd, sizeof(q80_tx_cmd_t));

		src = ha->hw.frame_hdr;
		ret = qla_tx_tso(ha, mp, &tso_cmd, src);

		if (!(ret & ~1)) {
			/* find the additional tx_cmd descriptors required */

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
		}
	}

	if (hw->txr_free <= (num_tx_cmds + QLA_TX_MIN_FREE)) {
		qla_hw_tx_done_locked(ha);
		if (hw->txr_free <= (num_tx_cmds + QLA_TX_MIN_FREE)) {
        		QL_DPRINT8((dev, "%s: (hw->txr_free <= "
				"(num_tx_cmds + QLA_TX_MIN_FREE))\n",
				__func__));
			return (-1);
		}
	}

	*tx_idx = hw->txr_next;

        tx_cmd = &hw->tx_ring_base[hw->txr_next];

	if (hdr_len == 0) {
		if ((nsegs > Q8_TX_MAX_SEGMENTS) ||
			(mp->m_pkthdr.len > ha->max_frame_size)){
        		device_printf(dev,
				"%s: (nsegs[%d, %d, 0x%b] > Q8_TX_MAX_SEGMENTS)\n",
				__func__, nsegs, mp->m_pkthdr.len,
				(int)mp->m_pkthdr.csum_flags, CSUM_BITS);
			qla_dump_buf8(ha, "qla_hw_send: wrong pkt",
				mtod(mp, char *), mp->m_len);
			return (EINVAL);
		}
		bzero((void *)tx_cmd, sizeof(q80_tx_cmd_t));
		if (qla_tx_chksum(ha, mp, tx_cmd) != 0) 
        		tx_cmd->flags_opcode = Q8_TX_CMD_OP_XMT_ETHER;
	} else {
		bcopy(&tso_cmd, tx_cmd, sizeof(q80_tx_cmd_t));
	}

	if (eh->evl_encap_proto == htons(ETHERTYPE_VLAN))
        	tx_cmd->flags_opcode |= Q8_TX_CMD_FLAGS_VLAN_TAGGED;
	else if (mp->m_flags & M_VLANTAG) {
        	tx_cmd->flags_opcode |= (Q8_TX_CMD_FLAGS_VLAN_TAGGED |
						Q8_TX_CMD_FLAGS_HW_VLAN_ID);
		tx_cmd->vlan_tci = mp->m_pkthdr.ether_vtag;
	}


        tx_cmd->n_bufs = (uint8_t)nsegs;
        tx_cmd->data_len_lo = (uint8_t)(total_length & 0xFF);
        tx_cmd->data_len_hi = qla_host_to_le16(((uint16_t)(total_length >> 8)));
	tx_cmd->port_cntxtid = Q8_TX_CMD_PORT_CNXTID(ha->pci_func);

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

		hw->txr_next = (hw->txr_next + 1) & (NUM_TX_DESCRIPTORS - 1);
		tx_cmd_count++;

		if (!nsegs)
			break;
		
        	tx_cmd = &hw->tx_ring_base[hw->txr_next];
		bzero((void *)tx_cmd, sizeof(q80_tx_cmd_t));
	}

	if (hdr_len) {
		/* TSO : Copy the header in the following tx cmd descriptors */

		tx_cmd = &hw->tx_ring_base[hw->txr_next];
		bzero((void *)tx_cmd, sizeof(q80_tx_cmd_t));

		bytes = sizeof(q80_tx_cmd_t) - Q8_TX_CMD_TSO_ALIGN;
		bytes = QL_MIN(bytes, hdr_len);

		dst = (uint8_t *)tx_cmd + Q8_TX_CMD_TSO_ALIGN;

		if (mp->m_flags & M_VLANTAG) {
			/* first copy the src/dst MAC addresses */
			bcopy(src, dst, (ETHER_ADDR_LEN * 2));
			dst += (ETHER_ADDR_LEN * 2);
			src += (ETHER_ADDR_LEN * 2);
			
			hdr_len -= (ETHER_ADDR_LEN * 2);

			*((uint16_t *)dst) = htons(ETHERTYPE_VLAN);
			dst += 2;
			*((uint16_t *)dst) = mp->m_pkthdr.ether_vtag;
			dst += 2;

			bytes -= ((ETHER_ADDR_LEN * 2) + 4);

			bcopy(src, dst, bytes);
			src += bytes;
			hdr_len -= bytes;
		} else {
			bcopy(src, dst, bytes);
			src += bytes;
			hdr_len -= bytes;
		}

		hw->txr_next = (hw->txr_next + 1) & (NUM_TX_DESCRIPTORS - 1);
		tx_cmd_count++;
		
		while (hdr_len) {
			tx_cmd = &hw->tx_ring_base[hw->txr_next];
			bzero((void *)tx_cmd, sizeof(q80_tx_cmd_t));

			bytes = QL_MIN((sizeof(q80_tx_cmd_t)), hdr_len);

			bcopy(src, tx_cmd, bytes);
			src += bytes;
			hdr_len -= bytes;
			hw->txr_next =
				(hw->txr_next + 1) & (NUM_TX_DESCRIPTORS - 1);
			tx_cmd_count++;
		}
	}

	hw->txr_free = hw->txr_free - tx_cmd_count;

	QL_UPDATE_TX_PRODUCER_INDEX(ha, hw->txr_next);
       	QL_DPRINT8((dev, "%s: return\n", __func__));
	return (0);
}

/*
 * Name: qla_del_hw_if
 * Function: Destroys the hardware specific entities corresponding to an
 *	Ethernet Interface
 */
void
qla_del_hw_if(qla_host_t *ha)
{
	int	i;

	for (i = 0; i < ha->hw.num_sds_rings; i++)
		QL_DISABLE_INTERRUPTS(ha, i);
	
	qla_del_rcv_cntxt(ha);
	qla_del_xmt_cntxt(ha);
	
	ha->hw.flags.lro = 0;
}

/*
 * Name: qla_init_hw_if
 * Function: Creates the hardware specific entities corresponding to an
 *	Ethernet Interface - Transmit and Receive Contexts. Sets the MAC Address
 *	corresponding to the interface. Enables LRO if allowed.
 */
int
qla_init_hw_if(qla_host_t *ha)
{
	device_t	dev;
	int		i;
	uint8_t		bcast_mac[6];

	qla_get_hw_caps(ha);

	dev = ha->pci_dev;

	for (i = 0; i < ha->hw.num_sds_rings; i++) {
		bzero(ha->hw.dma_buf.sds_ring[i].dma_b,
			ha->hw.dma_buf.sds_ring[i].size);
	}
	/*
	 * Create Receive Context
	 */
	if (qla_init_rcv_cntxt(ha)) {
		return (-1);
	}

	ha->hw.rx_next = NUM_RX_DESCRIPTORS - 2;
	ha->hw.rxj_next = NUM_RX_JUMBO_DESCRIPTORS - 2;
	ha->hw.rx_in = ha->hw.rxj_in = 0;

	/* Update the RDS Producer Indices */
	QL_UPDATE_RDS_PRODUCER_INDEX(ha, 0, ha->hw.rx_next);
	QL_UPDATE_RDS_PRODUCER_INDEX(ha, 1, ha->hw.rxj_next);

	/*
	 * Create Transmit Context
	 */
	if (qla_init_xmt_cntxt(ha)) {
		qla_del_rcv_cntxt(ha);
		return (-1);
	}

	qla_config_mac_addr(ha, ha->hw.mac_addr,
		(ha->hw.rx_cntxt_rsp)->rx_rsp.cntxt_id, 1);

	bcast_mac[0] = 0xFF; bcast_mac[1] = 0xFF; bcast_mac[2] = 0xFF;
	bcast_mac[3] = 0xFF; bcast_mac[4] = 0xFF; bcast_mac[5] = 0xFF;
	qla_config_mac_addr(ha, bcast_mac,
		(ha->hw.rx_cntxt_rsp)->rx_rsp.cntxt_id, 1);

	qla_config_rss(ha, (ha->hw.rx_cntxt_rsp)->rx_rsp.cntxt_id);

	qla_config_intr_coalesce(ha, (ha->hw.rx_cntxt_rsp)->rx_rsp.cntxt_id, 0);

	for (i = 0; i < ha->hw.num_sds_rings; i++)
		QL_ENABLE_INTERRUPTS(ha, i);

	return (0);
}

/*
 * Name: qla_init_rcv_cntxt
 * Function: Creates the Receive Context.
 */
static int
qla_init_rcv_cntxt(qla_host_t *ha)
{
	device_t		dev;
	qla_cdrp_t		cdrp;
	q80_rcv_cntxt_rsp_t	*rsp;
	q80_stat_desc_t		*sdesc;
	bus_addr_t		phys_addr;
	int			i, j;
        qla_hw_t		*hw = &ha->hw;

	dev = ha->pci_dev;

	/*
	 * Create Receive Context
	 */

	for (i = 0; i < hw->num_sds_rings; i++) {
		sdesc = (q80_stat_desc_t *)&hw->sds[i].sds_ring_base[0];
		for (j = 0; j < NUM_STATUS_DESCRIPTORS; j++) {
			sdesc->data[0] =
				Q8_STAT_DESC_SET_OWNER(Q8_STAT_DESC_OWNER_FW);
		}
	}

	phys_addr = ha->hw.rx_cntxt_req_paddr;

	bzero(&cdrp, sizeof(qla_cdrp_t));

	cdrp.cmd = Q8_CMD_CREATE_RX_CNTXT;
	cdrp.cmd_arg1 = (uint32_t)(phys_addr >> 32);
	cdrp.cmd_arg2 = (uint32_t)(phys_addr);
	cdrp.cmd_arg3 = (uint32_t)(sizeof (q80_rcv_cntxt_req_t));
	
	if (qla_issue_cmd(ha, &cdrp)) {
		device_printf(dev, "%s: Q8_CMD_CREATE_RX_CNTXT failed\n",
			__func__);
		return (-1);
	} else {
		rsp = ha->hw.rx_cntxt_rsp;

		QL_DPRINT2((dev, "%s: rcv cntxt successful"
			" rds_ring_offset = 0x%08x"
			" sds_ring_offset = 0x%08x"
			" cntxt_state = 0x%08x"
			" funcs_per_port = 0x%08x"
			" num_rds_rings = 0x%04x"
			" num_sds_rings = 0x%04x"
			" cntxt_id = 0x%04x"
			" phys_port = 0x%02x"
			" virt_port = 0x%02x\n",
			__func__,
			rsp->rx_rsp.rds_ring_offset,
			rsp->rx_rsp.sds_ring_offset,
			rsp->rx_rsp.cntxt_state,
			rsp->rx_rsp.funcs_per_port,
			rsp->rx_rsp.num_rds_rings,
			rsp->rx_rsp.num_sds_rings,
			rsp->rx_rsp.cntxt_id,
			rsp->rx_rsp.phys_port,
			rsp->rx_rsp.virt_port));

		for (i = 0; i < ha->hw.num_rds_rings; i++) {
			QL_DPRINT2((dev,
				"%s: rcv cntxt rds[%i].producer_reg = 0x%08x\n",
				__func__, i, rsp->rds_rsp[i].producer_reg));
		}
		for (i = 0; i < ha->hw.num_sds_rings; i++) {
			QL_DPRINT2((dev,
				"%s: rcv cntxt sds[%i].consumer_reg = 0x%08x"
				" sds[%i].intr_mask_reg = 0x%08x\n",
				__func__, i, rsp->sds_rsp[i].consumer_reg,
				i, rsp->sds_rsp[i].intr_mask_reg));
		}
	}
	ha->hw.flags.init_rx_cnxt = 1;
	return (0);
}

/*
 * Name: qla_del_rcv_cntxt
 * Function: Destroys the Receive Context.
 */
void
qla_del_rcv_cntxt(qla_host_t *ha)
{
	qla_cdrp_t	cdrp;
	device_t	dev = ha->pci_dev;

	if (!ha->hw.flags.init_rx_cnxt)
		return;

	bzero(&cdrp, sizeof(qla_cdrp_t));

	cdrp.cmd = Q8_CMD_DESTROY_RX_CNTXT;
	cdrp.cmd_arg1 = (uint32_t) (ha->hw.rx_cntxt_rsp)->rx_rsp.cntxt_id;

	if (qla_issue_cmd(ha, &cdrp)) {
		device_printf(dev, "%s: Q8_CMD_DESTROY_RX_CNTXT failed\n",
			__func__);
	}
	ha->hw.flags.init_rx_cnxt = 0;
}

/*
 * Name: qla_init_xmt_cntxt
 * Function: Creates the Transmit Context.
 */
static int
qla_init_xmt_cntxt(qla_host_t *ha)
{
	bus_addr_t		phys_addr;
	device_t		dev;
	q80_tx_cntxt_rsp_t	*tx_rsp;
	qla_cdrp_t		cdrp;
        qla_hw_t		*hw = &ha->hw;

	dev = ha->pci_dev;

	/*
	 * Create Transmit Context
	 */
	phys_addr = ha->hw.tx_cntxt_req_paddr;
	tx_rsp = ha->hw.tx_cntxt_rsp;

	hw->txr_comp = hw->txr_next = 0;
	*(hw->tx_cons) = 0;

	bzero(&cdrp, sizeof(qla_cdrp_t));

	cdrp.cmd = Q8_CMD_CREATE_TX_CNTXT;
	cdrp.cmd_arg1 = (uint32_t)(phys_addr >> 32);
	cdrp.cmd_arg2 = (uint32_t)(phys_addr);
	cdrp.cmd_arg3 = (uint32_t)(sizeof (q80_tx_cntxt_req_t));
	
	if (qla_issue_cmd(ha, &cdrp)) {
		device_printf(dev, "%s: Q8_CMD_CREATE_TX_CNTXT failed\n",
			__func__);
		return (-1);
	} else {
		ha->hw.tx_prod_reg = tx_rsp->producer_reg;

		QL_DPRINT2((dev, "%s: tx cntxt successful"
			" cntxt_state = 0x%08x "
			" cntxt_id = 0x%04x "
			" phys_port_id = 0x%02x "
			" virt_port_id = 0x%02x "
			" producer_reg = 0x%08x "
			" intr_mask_reg = 0x%08x\n",
			__func__, tx_rsp->cntxt_state, tx_rsp->cntxt_id,
			tx_rsp->phys_port_id, tx_rsp->virt_port_id,
			tx_rsp->producer_reg, tx_rsp->intr_mask_reg));
	}
	ha->hw.txr_free = NUM_TX_DESCRIPTORS;

	ha->hw.flags.init_tx_cnxt = 1;
	return (0);
}

/*
 * Name: qla_del_xmt_cntxt
 * Function: Destroys the Transmit Context.
 */
static void
qla_del_xmt_cntxt(qla_host_t *ha)
{
	qla_cdrp_t	cdrp;
	device_t	dev = ha->pci_dev;

	if (!ha->hw.flags.init_tx_cnxt)
		return;

	bzero(&cdrp, sizeof(qla_cdrp_t));

	cdrp.cmd = Q8_CMD_DESTROY_TX_CNTXT;
	cdrp.cmd_arg1 = (uint32_t) (ha->hw.tx_cntxt_rsp)->cntxt_id;

	if (qla_issue_cmd(ha, &cdrp)) {
		device_printf(dev, "%s: Q8_CMD_DESTROY_TX_CNTXT failed\n",
			__func__);
	}
	ha->hw.flags.init_tx_cnxt = 0;
}

/*
 * Name: qla_get_max_rds
 * Function: Returns the maximum number of Receive Descriptor Rings per context.
 */
static int
qla_get_max_rds(qla_host_t *ha)
{
	qla_cdrp_t	cdrp;
	device_t	dev;

	dev = ha->pci_dev;

	bzero(&cdrp, sizeof(qla_cdrp_t));

	cdrp.cmd = Q8_CMD_RD_MAX_RDS_PER_CNTXT;

	if (qla_issue_cmd(ha, &cdrp)) {
		device_printf(dev, "%s: Q8_CMD_RD_MAX_RDS_PER_CNTXT failed\n",
			__func__);
		return (-1);
	} else {
		ha->hw.max_rds_per_cntxt = cdrp.rsp_arg1;
		QL_DPRINT2((dev, "%s: max_rds_per_context 0x%08x\n",
			__func__, ha->hw.max_rds_per_cntxt));
	}
	return 0;
}

/*
 * Name: qla_get_max_sds
 * Function: Returns the maximum number of Status Descriptor Rings per context.
 */
static int
qla_get_max_sds(qla_host_t *ha)
{
	qla_cdrp_t	cdrp;
	device_t	dev;

	dev = ha->pci_dev;

	bzero(&cdrp, sizeof(qla_cdrp_t));

	cdrp.cmd = Q8_CMD_RD_MAX_SDS_PER_CNTXT;

	if (qla_issue_cmd(ha, &cdrp)) {
		device_printf(dev, "%s: Q8_CMD_RD_MAX_RDS_PER_CNTXT failed\n",
			__func__);
		return (-1);
	} else {
		ha->hw.max_sds_per_cntxt = cdrp.rsp_arg1;
		QL_DPRINT2((dev, "%s: max_sds_per_context 0x%08x\n",
			__func__, ha->hw.max_sds_per_cntxt));
	}
	return 0;
}

/*
 * Name: qla_get_max_rules
 * Function: Returns the maximum number of Rules per context.
 */
static int
qla_get_max_rules(qla_host_t *ha)
{
	qla_cdrp_t	cdrp;
	device_t	dev;

	dev = ha->pci_dev;

	bzero(&cdrp, sizeof(qla_cdrp_t));

	cdrp.cmd = Q8_CMD_RD_MAX_RULES_PER_CNTXT;

	if (qla_issue_cmd(ha, &cdrp)) {
		device_printf(dev, "%s: Q8_CMD_RD_MAX_RULES_PER_CNTXT failed\n",
			__func__);
		return (-1);
	} else {
		ha->hw.max_rules_per_cntxt = cdrp.rsp_arg1;
		QL_DPRINT2((dev, "%s: max_rules_per_cntxt 0x%08x\n",
			__func__, ha->hw.max_rules_per_cntxt));
	}
	return 0;
}

/*
 * Name: qla_get_max_rcv_cntxts
 * Function: Returns the maximum number of Receive Contexts supported.
 */
static int
qla_get_max_rcv_cntxts(qla_host_t *ha)
{
	qla_cdrp_t	cdrp;
	device_t	dev;

	dev = ha->pci_dev;

	bzero(&cdrp, sizeof(qla_cdrp_t));

	cdrp.cmd = Q8_CMD_RD_MAX_RX_CNTXT;

	if (qla_issue_cmd(ha, &cdrp)) {
		device_printf(dev, "%s: Q8_CMD_RD_MAX_RX_CNTXT failed\n",
			__func__);
		return (-1);
	} else {
		ha->hw.max_rcv_cntxts = cdrp.rsp_arg1;
		QL_DPRINT2((dev, "%s: max_rcv_cntxts 0x%08x\n",
			__func__, ha->hw.max_rcv_cntxts));
	}
	return 0;
}

/*
 * Name: qla_get_max_tx_cntxts
 * Function: Returns the maximum number of Transmit Contexts supported.
 */
static int
qla_get_max_tx_cntxts(qla_host_t *ha)
{
	qla_cdrp_t	cdrp;
	device_t	dev;

	dev = ha->pci_dev;

	bzero(&cdrp, sizeof(qla_cdrp_t));

	cdrp.cmd = Q8_CMD_RD_MAX_TX_CNTXT;

	if (qla_issue_cmd(ha, &cdrp)) {
		device_printf(dev, "%s: Q8_CMD_RD_MAX_TX_CNTXT failed\n",
			__func__);
		return (-1);
	} else {
		ha->hw.max_xmt_cntxts = cdrp.rsp_arg1;
		QL_DPRINT2((dev, "%s: max_xmt_cntxts 0x%08x\n",
			__func__, ha->hw.max_xmt_cntxts));
	}
	return 0;
}

/*
 * Name: qla_get_max_mtu
 * Function: Returns the MTU supported for a context.
 */
static int
qla_get_max_mtu(qla_host_t *ha)
{
	qla_cdrp_t	cdrp;
	device_t	dev;

	dev = ha->pci_dev;

	bzero(&cdrp, sizeof(qla_cdrp_t));

	cdrp.cmd = Q8_CMD_RD_MAX_MTU;

	if (qla_issue_cmd(ha, &cdrp)) {
		device_printf(dev, "%s: Q8_CMD_RD_MAX_MTU failed\n", __func__);
		return (-1);
	} else {
		ha->hw.max_mtu = cdrp.rsp_arg1;
		QL_DPRINT2((dev, "%s: max_mtu 0x%08x\n", __func__,
			ha->hw.max_mtu));
	}
	return 0;
}

/*
 * Name: qla_set_max_mtu
 * Function:
 *	Sets the maximum transfer unit size for the specified rcv context.
 */
int
qla_set_max_mtu(qla_host_t *ha, uint32_t mtu, uint16_t cntxt_id)
{
	qla_cdrp_t	cdrp;
	device_t	dev;

	dev = ha->pci_dev;

	bzero(&cdrp, sizeof(qla_cdrp_t));

	cdrp.cmd = Q8_CMD_SET_MTU;
	cdrp.cmd_arg1 = (uint32_t)cntxt_id;
	cdrp.cmd_arg2 = mtu;

	if (qla_issue_cmd(ha, &cdrp)) {
		device_printf(dev, "%s: Q8_CMD_RD_MAX_MTU failed\n", __func__);
		return (-1);
	} else {
		ha->hw.max_mtu = cdrp.rsp_arg1;
	}
	return 0;
}

/*
 * Name: qla_get_max_lro
 * Function: Returns the maximum number of TCP Connection which can be supported
 *	with LRO.
 */
static int
qla_get_max_lro(qla_host_t *ha)
{
	qla_cdrp_t	cdrp;
	device_t	dev;

	dev = ha->pci_dev;

	bzero(&cdrp, sizeof(qla_cdrp_t));

	cdrp.cmd = Q8_CMD_RD_MAX_LRO;

	if (qla_issue_cmd(ha, &cdrp)) {
		device_printf(dev, "%s: Q8_CMD_RD_MAX_LRO failed\n", __func__);
		return (-1);
	} else {
		ha->hw.max_lro = cdrp.rsp_arg1;
		QL_DPRINT2((dev, "%s: max_lro 0x%08x\n", __func__,
			ha->hw.max_lro));
	}
	return 0;
}

/*
 * Name: qla_get_flow_control
 * Function: Returns the Receive/Transmit Flow Control (PAUSE) settings for
 *	PCI function.
 */
static int
qla_get_flow_control(qla_host_t *ha)
{
	qla_cdrp_t	cdrp;
	device_t	dev;

	dev = ha->pci_dev;

	bzero(&cdrp, sizeof(qla_cdrp_t));

	cdrp.cmd = Q8_CMD_GET_FLOW_CNTRL;

	if (qla_issue_cmd(ha, &cdrp)) {
		device_printf(dev, "%s: Q8_CMD_GET_FLOW_CNTRL failed\n",
			__func__);
		return (-1);
	} else {
		QL_DPRINT2((dev, "%s: flow control 0x%08x\n", __func__,
			cdrp.rsp_arg1));
	}
	return 0;
}

/*
 * Name: qla_get_flow_control
 * Function: Retrieves hardware capabilities
 */
void
qla_get_hw_caps(qla_host_t *ha)
{
	//qla_read_mac_addr(ha);
	qla_get_max_rds(ha);
	qla_get_max_sds(ha);
	qla_get_max_rules(ha);
	qla_get_max_rcv_cntxts(ha);
	qla_get_max_tx_cntxts(ha);
	qla_get_max_mtu(ha);
	qla_get_max_lro(ha);
	qla_get_flow_control(ha);
	return;
}

/*
 * Name: qla_hw_set_multi
 * Function: Sets the Multicast Addresses provided the host O.S into the
 *	hardware (for the given interface)
 */
void
qla_hw_set_multi(qla_host_t *ha, uint8_t *mta, uint32_t mcnt,
	uint32_t add_multi)
{
	q80_rcv_cntxt_rsp_t	*rsp;
	int i;

	rsp = ha->hw.rx_cntxt_rsp;
	for (i = 0; i < mcnt; i++) {
		qla_config_mac_addr(ha, mta, rsp->rx_rsp.cntxt_id, add_multi);
		mta += Q8_MAC_ADDR_LEN;
	}
	return;
}

/*
 * Name: qla_hw_tx_done_locked
 * Function: Handle Transmit Completions
 */
static void
qla_hw_tx_done_locked(qla_host_t *ha)
{
	qla_tx_buf_t *txb;
        qla_hw_t *hw = &ha->hw;
	uint32_t comp_idx, comp_count = 0;

	/* retrieve index of last entry in tx ring completed */
	comp_idx = qla_le32_to_host(*(hw->tx_cons));

	while (comp_idx != hw->txr_comp) {

		txb = &ha->tx_buf[hw->txr_comp];

		hw->txr_comp++;
		if (hw->txr_comp == NUM_TX_DESCRIPTORS)
			hw->txr_comp = 0;

		comp_count++;

		if (txb->m_head) {
			bus_dmamap_sync(ha->tx_tag, txb->map,
				BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(ha->tx_tag, txb->map);
			bus_dmamap_destroy(ha->tx_tag, txb->map);
			m_freem(txb->m_head);

			txb->map = (bus_dmamap_t)0;
			txb->m_head = NULL;
		}
	}

	hw->txr_free += comp_count;

       	QL_DPRINT8((ha->pci_dev, "%s: return [c,f, p, pn][%d, %d, %d, %d]\n", __func__,
		hw->txr_comp, hw->txr_free, hw->txr_next, READ_REG32(ha, (ha->hw.tx_prod_reg + 0x1b2000))));

	return;
}

/*
 * Name: qla_hw_tx_done
 * Function: Handle Transmit Completions
 */
void
qla_hw_tx_done(qla_host_t *ha)
{
	if (!mtx_trylock(&ha->tx_lock)) {
       		QL_DPRINT8((ha->pci_dev,
			"%s: !mtx_trylock(&ha->tx_lock)\n", __func__));
		return;
	}
	qla_hw_tx_done_locked(ha);

	if (ha->hw.txr_free > free_pkt_thres)
		ha->ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	mtx_unlock(&ha->tx_lock);
	return;
}

void
qla_update_link_state(qla_host_t *ha)
{
	uint32_t link_state;
	uint32_t prev_link_state;

	if (!(ha->ifp->if_drv_flags & IFF_DRV_RUNNING)) {
		ha->hw.flags.link_up = 0;
		return;
	}
	link_state = READ_REG32(ha, Q8_LINK_STATE);

	prev_link_state =  ha->hw.flags.link_up;

	if (ha->pci_func == 0)
		ha->hw.flags.link_up = (((link_state & 0xF) == 1)? 1 : 0);
	else
		ha->hw.flags.link_up = ((((link_state >> 4)& 0xF) == 1)? 1 : 0);

	if (prev_link_state !=  ha->hw.flags.link_up) {
		if (ha->hw.flags.link_up) {
			if_link_state_change(ha->ifp, LINK_STATE_UP);
		} else {
			if_link_state_change(ha->ifp, LINK_STATE_DOWN);
		}
	}
}

int
qla_config_lro(qla_host_t *ha)
{
	int i;
        qla_hw_t *hw = &ha->hw;
	struct lro_ctrl *lro;

	for (i = 0; i < hw->num_sds_rings; i++) {
		lro = &hw->sds[i].lro;
		if (tcp_lro_init(lro)) {
			device_printf(ha->pci_dev, "%s: tcp_lro_init failed\n",
				__func__);
			return (-1);
		}
		lro->ifp = ha->ifp;
	}
	ha->flags.lro_init = 1;

	QL_DPRINT2((ha->pci_dev, "%s: LRO initialized\n", __func__));
	return (0);
}

void
qla_free_lro(qla_host_t *ha)
{
	int i;
        qla_hw_t *hw = &ha->hw;
	struct lro_ctrl *lro;

	if (!ha->flags.lro_init)
		return;

	for (i = 0; i < hw->num_sds_rings; i++) {
		lro = &hw->sds[i].lro;
		tcp_lro_free(lro);
	}
	ha->flags.lro_init = 0;
}

void
qla_hw_stop_rcv(qla_host_t *ha)
{
	int i, done, count = 100;

	while (count--) {
		done = 1;
		for (i = 0; i < ha->hw.num_sds_rings; i++) {
			if (ha->hw.sds[i].rcv_active)
				done = 0;
		}
		if (done)
			break;
		else 
			qla_mdelay(__func__, 10);
	}
}

