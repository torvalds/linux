/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011-2013 Qlogic Corporation
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
 * File: qla_isr.c
 * Author : David C Somayajulu, Qlogic Corporation, Aliso Viejo, CA 92656.
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

static void qla_replenish_normal_rx(qla_host_t *ha, qla_sds_t *sdsp);
static void qla_replenish_jumbo_rx(qla_host_t *ha, qla_sds_t *sdsp);

/*
 * Name: qla_rx_intr
 * Function: Handles normal ethernet frames received
 */
static void
qla_rx_intr(qla_host_t *ha, uint64_t data, uint32_t sds_idx,
	struct lro_ctrl *lro)
{
	uint32_t idx, length, status, ring;
	qla_rx_buf_t *rxb;
	struct mbuf *mp;
	struct ifnet *ifp = ha->ifp;
	qla_sds_t *sdsp;
	struct ether_vlan_header *eh;
	
	sdsp = &ha->hw.sds[sds_idx];
	
	ring = (uint32_t)Q8_STAT_DESC_TYPE(data);
	idx = (uint32_t)Q8_STAT_DESC_HANDLE(data);
	length = (uint32_t)Q8_STAT_DESC_TOTAL_LENGTH(data);
	status = (uint32_t)Q8_STAT_DESC_STATUS(data);

	if (ring == 0) {
		if ((idx >= NUM_RX_DESCRIPTORS) || (length > MCLBYTES)) {
			device_printf(ha->pci_dev, "%s: ring[%d] index[0x%08x]"
				" len[0x%08x] invalid\n",
				__func__, ring, idx, length);
			return;
		}
	} else {
		if ((idx >= NUM_RX_JUMBO_DESCRIPTORS)||(length > MJUM9BYTES)) {
			device_printf(ha->pci_dev, "%s: ring[%d] index[0x%08x]"
				" len[0x%08x] invalid\n",
				__func__, ring, idx, length);
			return;
		}
	}

	if (ring == 0)
		rxb = &ha->rx_buf[idx];
	else 
		rxb = &ha->rx_jbuf[idx];

	QL_ASSERT((rxb != NULL),\
		("%s: [r, i, sds_idx]=[%d, 0x%x, %d] rxb != NULL\n",\
		 __func__, ring, idx, sds_idx));

	mp = rxb->m_head;

	QL_ASSERT((mp != NULL),\
		("%s: [r,i,rxb, sds_idx]=[%d, 0x%x, %p, %d] mp != NULL\n",\
		 __func__, ring, idx, rxb, sds_idx));

	bus_dmamap_sync(ha->rx_tag, rxb->map, BUS_DMASYNC_POSTREAD);

	if (ring == 0) {
		rxb->m_head = NULL;
		rxb->next = sdsp->rxb_free;
		sdsp->rxb_free = rxb;
		sdsp->rx_free++;
	} else {
		rxb->m_head = NULL;
		rxb->next = sdsp->rxjb_free;
		sdsp->rxjb_free = rxb;
		sdsp->rxj_free++;
	}
	
	mp->m_len = length;
	mp->m_pkthdr.len = length;
	mp->m_pkthdr.rcvif = ifp;
	
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

	if (status == Q8_STAT_DESC_STATUS_CHKSUM_OK) {
		mp->m_pkthdr.csum_flags = (CSUM_IP_CHECKED | CSUM_IP_VALID);
	} else {
		mp->m_pkthdr.csum_flags = 0;
	}

	if (lro->lro_cnt && (tcp_lro_rx(lro, mp, 0) == 0)) {
		/* LRO packet has been successfully queued */
	} else {
		(*ifp->if_input)(ifp, mp);
	}

	if (sdsp->rx_free > std_replenish)
		qla_replenish_normal_rx(ha, sdsp);

	if (sdsp->rxj_free > jumbo_replenish)
		qla_replenish_jumbo_rx(ha, sdsp);

	return;
}

static void
qla_replenish_jumbo_rx(qla_host_t *ha, qla_sds_t *sdsp)
{
	qla_rx_buf_t *rxb;
	int count = jumbo_replenish;
	uint32_t rxj_next;

	if (!mtx_trylock(&ha->rxj_lock))
		return;

	rxj_next = ha->hw.rxj_next;

	while (count--) {
		rxb = sdsp->rxjb_free;

		if (rxb == NULL)
			break;

		sdsp->rxjb_free = rxb->next;
		sdsp->rxj_free--;


		if (qla_get_mbuf(ha, rxb, NULL, RDS_RING_INDEX_JUMBO) == 0) {
			qla_set_hw_rcv_desc(ha, RDS_RING_INDEX_JUMBO,
				ha->hw.rxj_in, rxb->handle, rxb->paddr,
				(rxb->m_head)->m_pkthdr.len);
			ha->hw.rxj_in++;
			if (ha->hw.rxj_in == NUM_RX_JUMBO_DESCRIPTORS)
				ha->hw.rxj_in = 0;
			ha->hw.rxj_next++;
			if (ha->hw.rxj_next == NUM_RX_JUMBO_DESCRIPTORS)
				ha->hw.rxj_next = 0;
		} else {
			device_printf(ha->pci_dev,
				"%s: qla_get_mbuf [1,(%d),(%d)] failed\n",
				__func__, ha->hw.rxj_in, rxb->handle);

			rxb->m_head = NULL;
			rxb->next = sdsp->rxjb_free;
			sdsp->rxjb_free = rxb;
			sdsp->rxj_free++;

			break;
		}
	}

	if (rxj_next != ha->hw.rxj_next) {
		QL_UPDATE_RDS_PRODUCER_INDEX(ha, 1, ha->hw.rxj_next);
	}
	mtx_unlock(&ha->rxj_lock);
}

static void
qla_replenish_normal_rx(qla_host_t *ha, qla_sds_t *sdsp)
{
	qla_rx_buf_t *rxb;
	int count = std_replenish;
	uint32_t rx_next;

	if (!mtx_trylock(&ha->rx_lock))
		return;

	rx_next = ha->hw.rx_next;

	while (count--) {
		rxb = sdsp->rxb_free;

		if (rxb == NULL)
			break;

		sdsp->rxb_free = rxb->next;
		sdsp->rx_free--;

		if (qla_get_mbuf(ha, rxb, NULL, RDS_RING_INDEX_NORMAL) == 0) {
			qla_set_hw_rcv_desc(ha, RDS_RING_INDEX_NORMAL,
				ha->hw.rx_in, rxb->handle, rxb->paddr,
				(rxb->m_head)->m_pkthdr.len);
			ha->hw.rx_in++;
			if (ha->hw.rx_in == NUM_RX_DESCRIPTORS)
				ha->hw.rx_in = 0;
			ha->hw.rx_next++;
			if (ha->hw.rx_next == NUM_RX_DESCRIPTORS)
				ha->hw.rx_next = 0;
		} else {
			device_printf(ha->pci_dev,
				"%s: qla_get_mbuf [0,(%d),(%d)] failed\n",
				__func__, ha->hw.rx_in, rxb->handle);

			rxb->m_head = NULL;
			rxb->next = sdsp->rxb_free;
			sdsp->rxb_free = rxb;
			sdsp->rx_free++;

			break;
		}
	}

	if (rx_next != ha->hw.rx_next) {
		QL_UPDATE_RDS_PRODUCER_INDEX(ha, 0, ha->hw.rx_next);
	}
	mtx_unlock(&ha->rx_lock);
}

/*
 * Name: qla_isr
 * Function: Main Interrupt Service Routine
 */
static uint32_t
qla_rcv_isr(qla_host_t *ha, uint32_t sds_idx, uint32_t count)
{
	device_t dev;
	qla_hw_t *hw;
	uint32_t comp_idx, desc_count;
	q80_stat_desc_t *sdesc;
	struct lro_ctrl *lro;
	uint32_t ret = 0;

	dev = ha->pci_dev;
	hw = &ha->hw;

	hw->sds[sds_idx].rcv_active = 1;
	if (ha->flags.stop_rcv) {
		hw->sds[sds_idx].rcv_active = 0;
		return 0;
	}

	QL_DPRINT2((dev, "%s: [%d]enter\n", __func__, sds_idx));

	/*
	 * receive interrupts
	 */
	comp_idx = hw->sds[sds_idx].sdsr_next;
	lro = &hw->sds[sds_idx].lro;

	while (count--) {

		sdesc = (q80_stat_desc_t *)
				&hw->sds[sds_idx].sds_ring_base[comp_idx];

		if (Q8_STAT_DESC_OWNER((sdesc->data[0])) !=
			Q8_STAT_DESC_OWNER_HOST) {
			QL_DPRINT2((dev, "%s:  data %p sdsr_next 0x%08x\n",
				__func__, (void *)sdesc->data[0], comp_idx));
			break;
		}

		desc_count = Q8_STAT_DESC_COUNT((sdesc->data[0]));

		switch (Q8_STAT_DESC_OPCODE((sdesc->data[0]))) {

		case Q8_STAT_DESC_OPCODE_RCV_PKT:
		case Q8_STAT_DESC_OPCODE_SYN_OFFLOAD:
			qla_rx_intr(ha, (sdesc->data[0]), sds_idx, lro);
			
			break;

		default:
			device_printf(dev, "%s: default 0x%llx!\n", __func__,
					(long long unsigned int)sdesc->data[0]);
			break;
		}

		while (desc_count--) {
			sdesc->data[0] =
				Q8_STAT_DESC_SET_OWNER(Q8_STAT_DESC_OWNER_FW);
			comp_idx = (comp_idx + 1) & (NUM_STATUS_DESCRIPTORS-1);
			sdesc = (q80_stat_desc_t *)
				&hw->sds[sds_idx].sds_ring_base[comp_idx];
		}
	}

	tcp_lro_flush_all(lro);

	if (hw->sds[sds_idx].sdsr_next != comp_idx) {
		QL_UPDATE_SDS_CONSUMER_INDEX(ha, sds_idx, comp_idx);
	}
	hw->sds[sds_idx].sdsr_next = comp_idx;

	sdesc = (q80_stat_desc_t *)&hw->sds[sds_idx].sds_ring_base[comp_idx];
	if ((sds_idx == 0) && (Q8_STAT_DESC_OWNER((sdesc->data[0])) ==
					Q8_STAT_DESC_OWNER_HOST)) {
		ret = -1;
	}

	hw->sds[sds_idx].rcv_active = 0;
	return (ret);
}

void
qla_isr(void *arg)
{
	qla_ivec_t *ivec = arg;
	qla_host_t *ha;
	uint32_t sds_idx;
	uint32_t ret;

	ha = ivec->ha;
	sds_idx = ivec->irq_rid - 1;

	if (sds_idx >= ha->hw.num_sds_rings) {
		device_printf(ha->pci_dev, "%s: bogus sds_idx 0x%x\n", __func__,
			sds_idx);
		
		return;
	}

	if (sds_idx == 0)
		taskqueue_enqueue(ha->tx_tq, &ha->tx_task);

	ret = qla_rcv_isr(ha, sds_idx, rcv_pkt_thres);

	if (sds_idx == 0)
		taskqueue_enqueue(ha->tx_tq, &ha->tx_task);

	if (ret) {
		taskqueue_enqueue(ha->irq_vec[sds_idx].rcv_tq,
			&ha->irq_vec[sds_idx].rcv_task);
	} else {
		QL_ENABLE_INTERRUPTS(ha, sds_idx);
	}
}

void
qla_rcv(void *context, int pending)
{
	qla_ivec_t *ivec = context;
	qla_host_t *ha;
	device_t dev;
	qla_hw_t *hw;
	uint32_t sds_idx;
	uint32_t ret;
	struct ifnet *ifp;

	ha = ivec->ha;
	dev = ha->pci_dev;
	hw = &ha->hw;
	sds_idx = ivec->irq_rid - 1;
	ifp = ha->ifp;

	do {
		if (sds_idx == 0) {
			if (qla_le32_to_host(*(hw->tx_cons)) != hw->txr_comp) {
				taskqueue_enqueue(ha->tx_tq, &ha->tx_task);
			} else if ((ifp->if_snd.ifq_head != NULL) &&
					QL_RUNNING(ifp)) {
				taskqueue_enqueue(ha->tx_tq, &ha->tx_task);
			}
		}
		ret = qla_rcv_isr(ha, sds_idx, rcv_pkt_thres_d);
	} while (ret);

	if (sds_idx == 0)
		taskqueue_enqueue(ha->tx_tq, &ha->tx_task);

	QL_ENABLE_INTERRUPTS(ha, sds_idx);
}

