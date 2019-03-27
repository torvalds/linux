/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013-2014 Qlogic Corporation
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
 * File: qls_isr.c
 * Author : David C Somayajulu, Qlogic Corporation, Aliso Viejo, CA 92656.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");



#include "qls_os.h"
#include "qls_hw.h"
#include "qls_def.h"
#include "qls_inline.h"
#include "qls_ver.h"
#include "qls_glbl.h"
#include "qls_dbg.h"


static void
qls_tx_comp(qla_host_t *ha, uint32_t txr_idx, q81_tx_mac_comp_t *tx_comp)
{
	qla_tx_buf_t *txb;
	uint32_t tx_idx = tx_comp->tid_lo;

	if (tx_idx >= NUM_TX_DESCRIPTORS) {
		ha->qla_initiate_recovery = 1;
		return;
	}

	txb = &ha->tx_ring[txr_idx].tx_buf[tx_idx];

	if (txb->m_head) {
		if_inc_counter(ha->ifp, IFCOUNTER_OPACKETS, 1);
		bus_dmamap_sync(ha->tx_tag, txb->map,
		        BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(ha->tx_tag, txb->map);
		m_freem(txb->m_head);

		txb->m_head = NULL;
	}

        ha->tx_ring[txr_idx].txr_done++;

	if (ha->tx_ring[txr_idx].txr_done == NUM_TX_DESCRIPTORS)
		ha->tx_ring[txr_idx].txr_done = 0;
}

static void
qls_replenish_rx(qla_host_t *ha, uint32_t r_idx)
{
        qla_rx_buf_t			*rxb;
	qla_rx_ring_t			*rxr;
        int				count;
	volatile q81_bq_addr_e_t	*sbq_e;

	rxr = &ha->rx_ring[r_idx];

	count = rxr->rx_free;
	sbq_e = rxr->sbq_vaddr;

        while (count--) {

		rxb = &rxr->rx_buf[rxr->sbq_next];

		if (rxb->m_head == NULL) {
                	if (qls_get_mbuf(ha, rxb, NULL) != 0) {
                        	device_printf(ha->pci_dev,
					"%s: qls_get_mbuf [0,%d,%d] failed\n",
					__func__, rxr->sbq_next, r_idx);
				rxb->m_head = NULL;
				break;
			}
		}

		if (rxb->m_head != NULL) {
			sbq_e[rxr->sbq_next].addr_lo = (uint32_t)rxb->paddr;
			sbq_e[rxr->sbq_next].addr_hi =
				(uint32_t)(rxb->paddr >> 32);

                        rxr->sbq_next++;
                        if (rxr->sbq_next == NUM_RX_DESCRIPTORS)
                                rxr->sbq_next = 0;

			rxr->sbq_free++;
                	rxr->rx_free--;
		}

                if (rxr->sbq_free == 16) {

			rxr->sbq_in += 16;
			rxr->sbq_in = rxr->sbq_in & (NUM_RX_DESCRIPTORS - 1);
			rxr->sbq_free = 0;

			Q81_WR_SBQ_PROD_IDX(r_idx, (rxr->sbq_in));
                }
        }
}

static int
qls_rx_comp(qla_host_t *ha, uint32_t rxr_idx, uint32_t cq_idx, q81_rx_t *cq_e)
{
	qla_rx_buf_t	*rxb;
	qla_rx_ring_t	*rxr;
	device_t	dev = ha->pci_dev;
	struct mbuf     *mp = NULL;
	struct ifnet	*ifp = ha->ifp;
	struct lro_ctrl	*lro;
	struct ether_vlan_header *eh;

	rxr = &ha->rx_ring[rxr_idx];

	lro = &rxr->lro;

	rxb = &rxr->rx_buf[rxr->rx_next];

	if (!(cq_e->flags1 & Q81_RX_FLAGS1_DS)) {
		device_printf(dev, "%s: DS bit not set \n", __func__);
		return -1;
	}
	if (rxb->paddr != cq_e->b_paddr) {

		device_printf(dev,
			"%s: (rxb->paddr != cq_e->b_paddr)[%p, %p] \n",
			__func__, (void *)rxb->paddr, (void *)cq_e->b_paddr);

		Q81_SET_CQ_INVALID(cq_idx);

		ha->qla_initiate_recovery = 1;

		return(-1);
	}

	rxr->rx_int++;

	if ((cq_e->flags1 & Q81_RX_FLAGS1_ERR_MASK) == 0) {

		mp = rxb->m_head;
		rxb->m_head = NULL;

		if (mp == NULL) {
			device_printf(dev, "%s: mp == NULL\n", __func__);
		} else {
			mp->m_flags |= M_PKTHDR;
			mp->m_pkthdr.len = cq_e->length;
			mp->m_pkthdr.rcvif = ifp;
			mp->m_len = cq_e->length;

			eh = mtod(mp, struct ether_vlan_header *);

			if (eh->evl_encap_proto == htons(ETHERTYPE_VLAN)) {
				uint32_t *data = (uint32_t *)eh;

				mp->m_pkthdr.ether_vtag = ntohs(eh->evl_tag);
				mp->m_flags |= M_VLANTAG;

				*(data + 3) = *(data + 2);
				*(data + 2) = *(data + 1);
				*(data + 1) = *data;

				m_adj(mp, ETHER_VLAN_ENCAP_LEN);
			}

			if ((cq_e->flags1 & Q81_RX_FLAGS1_RSS_MATCH_MASK)) {
				rxr->rss_int++;
				mp->m_pkthdr.flowid = cq_e->rss;
				M_HASHTYPE_SET(mp, M_HASHTYPE_OPAQUE_HASH);
			}
			if (cq_e->flags0 & (Q81_RX_FLAGS0_TE |
				Q81_RX_FLAGS0_NU | Q81_RX_FLAGS0_IE)) {
				mp->m_pkthdr.csum_flags = 0;
			} else {
				mp->m_pkthdr.csum_flags = CSUM_IP_CHECKED |
					CSUM_IP_VALID | CSUM_DATA_VALID |
					CSUM_PSEUDO_HDR;
				mp->m_pkthdr.csum_data = 0xFFFF;
			}
			if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);

			if (lro->lro_cnt && (tcp_lro_rx(lro, mp, 0) == 0)) {
				/* LRO packet has been successfully queued */
			} else {
				(*ifp->if_input)(ifp, mp);
			}
		}
	} else {
		device_printf(dev, "%s: err [0%08x]\n", __func__, cq_e->flags1);
	}

	rxr->rx_free++;
	rxr->rx_next++;

	if (rxr->rx_next == NUM_RX_DESCRIPTORS)
		rxr->rx_next = 0;

	if ((rxr->rx_free + rxr->sbq_free) >= 16)
                qls_replenish_rx(ha, rxr_idx);

	return 0;
}

static void
qls_cq_isr(qla_host_t *ha, uint32_t cq_idx)
{
	q81_cq_e_t *cq_e, *cq_b;
	uint32_t i, cq_comp_idx;
	int ret = 0, tx_comp_done = 0;
	struct lro_ctrl	*lro;

	cq_b = ha->rx_ring[cq_idx].cq_base_vaddr;
	lro = &ha->rx_ring[cq_idx].lro;

	cq_comp_idx = *(ha->rx_ring[cq_idx].cqi_vaddr);

	i = ha->rx_ring[cq_idx].cq_next;

	while (i != cq_comp_idx) {

		cq_e = &cq_b[i];

		switch (cq_e->opcode) {

                case Q81_IOCB_TX_MAC:
                case Q81_IOCB_TX_TSO:
                        qls_tx_comp(ha, cq_idx, (q81_tx_mac_comp_t *)cq_e);
                        tx_comp_done++;
                        break;

		case Q81_IOCB_RX:
			ret = qls_rx_comp(ha, cq_idx, i, (q81_rx_t *)cq_e);
	
			break;

		case Q81_IOCB_MPI:
		case Q81_IOCB_SYS:
		default:
			device_printf(ha->pci_dev, "%s[%d %d 0x%x]: illegal \n",
				__func__, i, (*(ha->rx_ring[cq_idx].cqi_vaddr)),
				cq_e->opcode);
			qls_dump_buf32(ha, __func__, cq_e,
				(sizeof (q81_cq_e_t) >> 2));
			break;
		}

		i++;
		if (i == NUM_CQ_ENTRIES)
			i = 0;

		if (ret) {
			break;
		}

		if (i == cq_comp_idx) {
			cq_comp_idx = *(ha->rx_ring[cq_idx].cqi_vaddr);
		}

                if (tx_comp_done) {
                        taskqueue_enqueue(ha->tx_tq, &ha->tx_task);
                        tx_comp_done = 0;
                }
	}

	tcp_lro_flush_all(lro);

	ha->rx_ring[cq_idx].cq_next = cq_comp_idx;

	if (!ret) {
		Q81_WR_CQ_CONS_IDX(cq_idx, (ha->rx_ring[cq_idx].cq_next));
	}
        if (tx_comp_done)
                taskqueue_enqueue(ha->tx_tq, &ha->tx_task);

	return;
}

static void
qls_mbx_isr(qla_host_t *ha)
{
	uint32_t data;
	int i;
	device_t dev = ha->pci_dev;

	if (qls_mbx_rd_reg(ha, 0, &data) == 0) {

		if ((data & 0xF000) == 0x4000) {
			ha->mbox[0] = data;
			for (i = 1; i < Q81_NUM_MBX_REGISTERS; i++) {
				if (qls_mbx_rd_reg(ha, i, &data))
					break; 
				ha->mbox[i] = data;
			}
			ha->mbx_done = 1;
		} else if ((data & 0xF000) == 0x8000) {

			/* we have an AEN */
	
			ha->aen[0] = data;
			for (i = 1; i < Q81_NUM_AEN_REGISTERS; i++) {
				if (qls_mbx_rd_reg(ha, i, &data))
					break; 
				ha->aen[i] = data;
			}
			device_printf(dev,"%s: AEN "
				"[0x%08x 0x%08x 0x%08x 0x%08x 0x%08x"
				" 0x%08x 0x%08x 0x%08x 0x%08x]\n",
				__func__,
				ha->aen[0], ha->aen[1], ha->aen[2],
				ha->aen[3], ha->aen[4], ha->aen[5],
				ha->aen[6], ha->aen[7], ha->aen[8]);

			switch ((ha->aen[0] & 0xFFFF)) {

			case 0x8011:
				ha->link_up = 1;
				break;

			case 0x8012:
				ha->link_up = 0;
				break;

			case 0x8130:
				ha->link_hw_info = ha->aen[1];
				break;

			case 0x8131:
				ha->link_hw_info = 0;
				break;

			}
		} 
	}
	WRITE_REG32(ha, Q81_CTL_HOST_CMD_STATUS, Q81_CTL_HCS_CMD_CLR_RTH_INTR);

	return;
}

void
qls_isr(void *arg)
{
	qla_ivec_t *ivec = arg;
	qla_host_t *ha;
	uint32_t status;
	uint32_t cq_idx;
	device_t dev;

	ha = ivec->ha;
	cq_idx = ivec->cq_idx;
	dev = ha->pci_dev;

	status = READ_REG32(ha, Q81_CTL_STATUS);

	if (status & Q81_CTL_STATUS_FE) {
		device_printf(dev, "%s fatal error\n", __func__);
		return;
	}

	if ((cq_idx == 0) && (status & Q81_CTL_STATUS_PI)) {
		qls_mbx_isr(ha);
	}

	status = READ_REG32(ha, Q81_CTL_INTR_STATUS1);

	if (status & ( 0x1 << cq_idx))
		qls_cq_isr(ha, cq_idx);

	Q81_ENABLE_INTR(ha, cq_idx);

	return;
}

