/*-
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
 * File: qls_hw.c
 * Author : David C Somayajulu, Qlogic Corporation, Aliso Viejo, CA 92656.
 * Content: Contains Hardware dependent functions
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

/*
 * Static Functions
 */
static int qls_wait_for_mac_proto_idx_ready(qla_host_t *ha, uint32_t op);
static int qls_config_unicast_mac_addr(qla_host_t *ha, uint32_t add_mac);
static int qls_config_mcast_mac_addr(qla_host_t *ha, uint8_t *mac_addr,
                uint32_t add_mac, uint32_t index);

static int qls_init_rss(qla_host_t *ha);
static int qls_init_comp_queue(qla_host_t *ha, int cid);
static int qls_init_work_queue(qla_host_t *ha, int wid);
static int qls_init_fw_routing_table(qla_host_t *ha);
static int qls_hw_add_all_mcast(qla_host_t *ha);
static int qls_hw_add_mcast(qla_host_t *ha, uint8_t *mta);
static int qls_hw_del_mcast(qla_host_t *ha, uint8_t *mta);
static int qls_wait_for_flash_ready(qla_host_t *ha);

static int qls_sem_lock(qla_host_t *ha, uint32_t mask, uint32_t value);
static void qls_sem_unlock(qla_host_t *ha, uint32_t mask);

static void qls_free_tx_dma(qla_host_t *ha);
static int qls_alloc_tx_dma(qla_host_t *ha);
static void qls_free_rx_dma(qla_host_t *ha);
static int qls_alloc_rx_dma(qla_host_t *ha);
static void qls_free_mpi_dma(qla_host_t *ha);
static int qls_alloc_mpi_dma(qla_host_t *ha);
static void qls_free_rss_dma(qla_host_t *ha);
static int qls_alloc_rss_dma(qla_host_t *ha);

static int qls_flash_validate(qla_host_t *ha, const char *signature);


static int qls_wait_for_proc_addr_ready(qla_host_t *ha);
static int qls_proc_addr_rd_reg(qla_host_t *ha, uint32_t addr_module,
		uint32_t reg, uint32_t *data);
static int qls_proc_addr_wr_reg(qla_host_t *ha, uint32_t addr_module,
		uint32_t reg, uint32_t data);

static int qls_hw_reset(qla_host_t *ha);

/*
 * MPI Related Functions
 */
static int qls_mbx_cmd(qla_host_t *ha, uint32_t *in_mbx, uint32_t i_count,
		uint32_t *out_mbx, uint32_t o_count);
static int qls_mbx_set_mgmt_ctrl(qla_host_t *ha, uint32_t t_ctrl);
static int qls_mbx_get_mgmt_ctrl(qla_host_t *ha, uint32_t *t_status);
static void qls_mbx_get_link_status(qla_host_t *ha);
static void qls_mbx_about_fw(qla_host_t *ha);

int
qls_get_msix_count(qla_host_t *ha)
{
	return (ha->num_rx_rings);
}

static int
qls_syctl_mpi_dump(SYSCTL_HANDLER_ARGS)
{
        int err = 0, ret;
        qla_host_t *ha;

        err = sysctl_handle_int(oidp, &ret, 0, req);

        if (err || !req->newptr)
                return (err);


        if (ret == 1) {
                ha = (qla_host_t *)arg1;
		qls_mpi_core_dump(ha);
        }
        return (err);
}

static int
qls_syctl_link_status(SYSCTL_HANDLER_ARGS)
{
        int err = 0, ret;
        qla_host_t *ha;

        err = sysctl_handle_int(oidp, &ret, 0, req);

        if (err || !req->newptr)
                return (err);


        if (ret == 1) {
                ha = (qla_host_t *)arg1;
		qls_mbx_get_link_status(ha);
		qls_mbx_about_fw(ha);
        }
        return (err);
}

void
qls_hw_add_sysctls(qla_host_t *ha)
{
        device_t	dev;

        dev = ha->pci_dev;

	ha->num_rx_rings = MAX_RX_RINGS; ha->num_tx_rings = MAX_TX_RINGS;

	SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
		SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
		OID_AUTO, "num_rx_rings", CTLFLAG_RD, &ha->num_rx_rings,
		ha->num_rx_rings, "Number of Completion Queues");

        SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "num_tx_rings", CTLFLAG_RD, &ha->num_tx_rings,
		ha->num_tx_rings, "Number of Transmit Rings");

        SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "mpi_dump", CTLTYPE_INT | CTLFLAG_RW,
                (void *)ha, 0,
                qls_syctl_mpi_dump, "I", "MPI Dump");

        SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "link_status", CTLTYPE_INT | CTLFLAG_RW,
                (void *)ha, 0,
                qls_syctl_link_status, "I", "Link Status");
}

/*
 * Name: qls_free_dma
 * Function: Frees the DMA'able memory allocated in qls_alloc_dma()
 */
void
qls_free_dma(qla_host_t *ha)
{
	qls_free_rss_dma(ha);
	qls_free_mpi_dma(ha);
	qls_free_tx_dma(ha);
	qls_free_rx_dma(ha);
	return;
}

/*
 * Name: qls_alloc_dma
 * Function: Allocates DMA'able memory for Tx/Rx Rings, Tx/Rx Contexts.
 */
int
qls_alloc_dma(qla_host_t *ha)
{
	if (qls_alloc_rx_dma(ha))
		return (-1);

	if (qls_alloc_tx_dma(ha)) {
		qls_free_rx_dma(ha);
		return (-1);
	}

	if (qls_alloc_mpi_dma(ha)) {
		qls_free_tx_dma(ha);
		qls_free_rx_dma(ha);
		return (-1);
	}

	if (qls_alloc_rss_dma(ha)) {
		qls_free_mpi_dma(ha);
		qls_free_tx_dma(ha);
		qls_free_rx_dma(ha);
		return (-1);
	}

	return (0);
}


static int
qls_wait_for_mac_proto_idx_ready(qla_host_t *ha, uint32_t op)
{
	uint32_t data32;
	uint32_t count = 3;

	while (count--) {
		data32 = READ_REG32(ha, Q81_CTL_MAC_PROTO_ADDR_INDEX);

		if (data32 & op)
			return (0);

		QLA_USEC_DELAY(100);
	}
	ha->qla_initiate_recovery = 1;
	return (-1);
}

/*
 * Name: qls_config_unicast_mac_addr
 * Function: binds/unbinds a unicast MAC address to the interface.
 */
static int
qls_config_unicast_mac_addr(qla_host_t *ha, uint32_t add_mac)
{
	int ret = 0;
	uint32_t mac_upper = 0;
	uint32_t mac_lower = 0;
	uint32_t value = 0, index;

	if (qls_sem_lock(ha, Q81_CTL_SEM_MASK_MAC_SERDES,
		Q81_CTL_SEM_SET_MAC_SERDES)) {
		QL_DPRINT1((ha->pci_dev, "%s: semlock failed\n", __func__));
		return(-1);
	}

	if (add_mac) {
		mac_upper = (ha->mac_addr[0] << 8) | ha->mac_addr[1];
		mac_lower = (ha->mac_addr[2] << 24) | (ha->mac_addr[3] << 16) |
				(ha->mac_addr[4] << 8) | ha->mac_addr[5];
	}
	ret = qls_wait_for_mac_proto_idx_ready(ha, Q81_CTL_MAC_PROTO_AI_MW);
	if (ret)
		goto qls_config_unicast_mac_addr_exit;
	
	index = 128 * (ha->pci_func & 0x1); /* index */

	value = (index << Q81_CTL_MAC_PROTO_AI_IDX_SHIFT) |
		Q81_CTL_MAC_PROTO_AI_TYPE_CAM_MAC;

	WRITE_REG32(ha, Q81_CTL_MAC_PROTO_ADDR_INDEX, value);
	WRITE_REG32(ha, Q81_CTL_MAC_PROTO_ADDR_DATA, mac_lower);

	ret = qls_wait_for_mac_proto_idx_ready(ha, Q81_CTL_MAC_PROTO_AI_MW);
	if (ret)
		goto qls_config_unicast_mac_addr_exit;

	value = (index << Q81_CTL_MAC_PROTO_AI_IDX_SHIFT) |
		Q81_CTL_MAC_PROTO_AI_TYPE_CAM_MAC | 0x1;

	WRITE_REG32(ha, Q81_CTL_MAC_PROTO_ADDR_INDEX, value);
	WRITE_REG32(ha, Q81_CTL_MAC_PROTO_ADDR_DATA, mac_upper);

	ret = qls_wait_for_mac_proto_idx_ready(ha, Q81_CTL_MAC_PROTO_AI_MW);
	if (ret)
		goto qls_config_unicast_mac_addr_exit;

	value = (index << Q81_CTL_MAC_PROTO_AI_IDX_SHIFT) |
		Q81_CTL_MAC_PROTO_AI_TYPE_CAM_MAC | 0x2;

	WRITE_REG32(ha, Q81_CTL_MAC_PROTO_ADDR_INDEX, value);

	value = Q81_CAM_MAC_OFF2_ROUTE_NIC |
			((ha->pci_func & 0x1) << Q81_CAM_MAC_OFF2_FUNC_SHIFT) |
			(0 << Q81_CAM_MAC_OFF2_CQID_SHIFT);

	WRITE_REG32(ha, Q81_CTL_MAC_PROTO_ADDR_DATA, value);

qls_config_unicast_mac_addr_exit:
	qls_sem_unlock(ha, Q81_CTL_SEM_MASK_MAC_SERDES);
	return (ret);
}

/*
 * Name: qls_config_mcast_mac_addr
 * Function: binds/unbinds a multicast MAC address to the interface.
 */
static int
qls_config_mcast_mac_addr(qla_host_t *ha, uint8_t *mac_addr, uint32_t add_mac,
	uint32_t index)
{
	int ret = 0;
	uint32_t mac_upper = 0;
	uint32_t mac_lower = 0;
	uint32_t value = 0;

	if (qls_sem_lock(ha, Q81_CTL_SEM_MASK_MAC_SERDES,
		Q81_CTL_SEM_SET_MAC_SERDES)) {
		QL_DPRINT1((ha->pci_dev, "%s: semlock failed\n", __func__));
		return(-1);
	}

	if (add_mac) {
		mac_upper = (mac_addr[0] << 8) | mac_addr[1];
		mac_lower = (mac_addr[2] << 24) | (mac_addr[3] << 16) |
				(mac_addr[4] << 8) | mac_addr[5];
	}
	ret = qls_wait_for_mac_proto_idx_ready(ha, Q81_CTL_MAC_PROTO_AI_MW);
	if (ret)
		goto qls_config_mcast_mac_addr_exit;
	
	value = Q81_CTL_MAC_PROTO_AI_E |
			(index << Q81_CTL_MAC_PROTO_AI_IDX_SHIFT) |
			Q81_CTL_MAC_PROTO_AI_TYPE_MCAST ;

	WRITE_REG32(ha, Q81_CTL_MAC_PROTO_ADDR_INDEX, value);
	WRITE_REG32(ha, Q81_CTL_MAC_PROTO_ADDR_DATA, mac_lower);

	ret = qls_wait_for_mac_proto_idx_ready(ha, Q81_CTL_MAC_PROTO_AI_MW);
	if (ret)
		goto qls_config_mcast_mac_addr_exit;

	value = Q81_CTL_MAC_PROTO_AI_E |
			(index << Q81_CTL_MAC_PROTO_AI_IDX_SHIFT) |
			Q81_CTL_MAC_PROTO_AI_TYPE_MCAST | 0x1;

	WRITE_REG32(ha, Q81_CTL_MAC_PROTO_ADDR_INDEX, value);
	WRITE_REG32(ha, Q81_CTL_MAC_PROTO_ADDR_DATA, mac_upper);

qls_config_mcast_mac_addr_exit:
	qls_sem_unlock(ha, Q81_CTL_SEM_MASK_MAC_SERDES);

	return (ret);
}

/*
 * Name: qls_set_mac_rcv_mode
 * Function: Enable/Disable AllMulticast and Promiscuous Modes.
 */
static int
qls_wait_for_route_idx_ready(qla_host_t *ha, uint32_t op)
{
	uint32_t data32;
	uint32_t count = 3;

	while (count--) {
		data32 = READ_REG32(ha, Q81_CTL_ROUTING_INDEX);

		if (data32 & op)
			return (0);

		QLA_USEC_DELAY(100);
	}
	ha->qla_initiate_recovery = 1;
	return (-1);
}

static int
qls_load_route_idx_reg(qla_host_t *ha, uint32_t index, uint32_t data)
{
	int ret = 0;

	ret = qls_wait_for_route_idx_ready(ha, Q81_CTL_RI_MW);

	if (ret) {
		device_printf(ha->pci_dev, "%s: [0x%08x, 0x%08x] failed\n",
			__func__, index, data);
		goto qls_load_route_idx_reg_exit;
	}

	
	WRITE_REG32(ha, Q81_CTL_ROUTING_INDEX, index);
	WRITE_REG32(ha, Q81_CTL_ROUTING_DATA, data);

qls_load_route_idx_reg_exit:
	return (ret);
}

static int
qls_load_route_idx_reg_locked(qla_host_t *ha, uint32_t index, uint32_t data)
{
	int ret = 0;

	if (qls_sem_lock(ha, Q81_CTL_SEM_MASK_RIDX_DATAREG,
		Q81_CTL_SEM_SET_RIDX_DATAREG)) {
		QL_DPRINT1((ha->pci_dev, "%s: semlock failed\n", __func__));
		return(-1);
	}

	ret = qls_load_route_idx_reg(ha, index, data);

	qls_sem_unlock(ha, Q81_CTL_SEM_MASK_RIDX_DATAREG);

	return (ret);
}

static int
qls_clear_routing_table(qla_host_t *ha)
{
	int i, ret = 0;

	if (qls_sem_lock(ha, Q81_CTL_SEM_MASK_RIDX_DATAREG,
		Q81_CTL_SEM_SET_RIDX_DATAREG)) {
		QL_DPRINT1((ha->pci_dev, "%s: semlock failed\n", __func__));
		return(-1);
	}

	for (i = 0; i < 16; i++) {
		ret = qls_load_route_idx_reg(ha, (Q81_CTL_RI_TYPE_NICQMASK|
			(i << 8) | Q81_CTL_RI_DST_DFLTQ), 0);
		if (ret)
			break;
	}

	qls_sem_unlock(ha, Q81_CTL_SEM_MASK_RIDX_DATAREG);

	return (ret);
}

int
qls_set_promisc(qla_host_t *ha)
{
	int ret;

	ret = qls_load_route_idx_reg_locked(ha,
			(Q81_CTL_RI_E | Q81_CTL_RI_TYPE_NICQMASK |
			Q81_CTL_RI_IDX_PROMISCUOUS | Q81_CTL_RI_DST_DFLTQ),
			Q81_CTL_RD_VALID_PKT);
	return (ret);
}

void
qls_reset_promisc(qla_host_t *ha)
{
	int ret;

	ret = qls_load_route_idx_reg_locked(ha, (Q81_CTL_RI_TYPE_NICQMASK |
			Q81_CTL_RI_IDX_PROMISCUOUS | Q81_CTL_RI_DST_DFLTQ), 0);
	return;
}

int
qls_set_allmulti(qla_host_t *ha)
{
	int ret;

	ret = qls_load_route_idx_reg_locked(ha,
			(Q81_CTL_RI_E | Q81_CTL_RI_TYPE_NICQMASK |
			Q81_CTL_RI_IDX_ALLMULTI | Q81_CTL_RI_DST_DFLTQ),
			Q81_CTL_RD_MCAST);
	return (ret);
}

void
qls_reset_allmulti(qla_host_t *ha)
{
	int ret;

	ret = qls_load_route_idx_reg_locked(ha, (Q81_CTL_RI_TYPE_NICQMASK |
			Q81_CTL_RI_IDX_ALLMULTI | Q81_CTL_RI_DST_DFLTQ), 0);
	return;
}


static int
qls_init_fw_routing_table(qla_host_t *ha)
{
	int ret = 0;

	ret = qls_clear_routing_table(ha);
	if (ret)
		return (-1);

	if (qls_sem_lock(ha, Q81_CTL_SEM_MASK_RIDX_DATAREG,
		Q81_CTL_SEM_SET_RIDX_DATAREG)) {
		QL_DPRINT1((ha->pci_dev, "%s: semlock failed\n", __func__));
		return(-1);
	}

	ret = qls_load_route_idx_reg(ha, (Q81_CTL_RI_E | Q81_CTL_RI_DST_DROP |
			Q81_CTL_RI_TYPE_NICQMASK | Q81_CTL_RI_IDX_ALL_ERROR),
			Q81_CTL_RD_ERROR_PKT);
	if (ret)
		goto qls_init_fw_routing_table_exit;

	ret = qls_load_route_idx_reg(ha, (Q81_CTL_RI_E | Q81_CTL_RI_DST_DFLTQ |
			Q81_CTL_RI_TYPE_NICQMASK | Q81_CTL_RI_IDX_BCAST),
			Q81_CTL_RD_BCAST);
	if (ret)
		goto qls_init_fw_routing_table_exit;

	if (ha->num_rx_rings > 1 ) {
		ret = qls_load_route_idx_reg(ha,
				(Q81_CTL_RI_E | Q81_CTL_RI_DST_RSS |
				Q81_CTL_RI_TYPE_NICQMASK |
				Q81_CTL_RI_IDX_RSS_MATCH),
				Q81_CTL_RD_RSS_MATCH);
		if (ret)
			goto qls_init_fw_routing_table_exit;
	}

	ret = qls_load_route_idx_reg(ha, (Q81_CTL_RI_E | Q81_CTL_RI_DST_DFLTQ |
			Q81_CTL_RI_TYPE_NICQMASK | Q81_CTL_RI_IDX_MCAST_MATCH),
			Q81_CTL_RD_MCAST_REG_MATCH);
	if (ret)
		goto qls_init_fw_routing_table_exit;

	ret = qls_load_route_idx_reg(ha, (Q81_CTL_RI_E | Q81_CTL_RI_DST_DFLTQ |
			Q81_CTL_RI_TYPE_NICQMASK | Q81_CTL_RI_IDX_CAM_HIT),
			Q81_CTL_RD_CAM_HIT);
	if (ret)
		goto qls_init_fw_routing_table_exit;

qls_init_fw_routing_table_exit:
	qls_sem_unlock(ha, Q81_CTL_SEM_MASK_RIDX_DATAREG);
	return (ret);
}

static int
qls_tx_tso_chksum(qla_host_t *ha, struct mbuf *mp, q81_tx_tso_t *tx_mac)
{
        struct ether_vlan_header *eh;
        struct ip *ip;
        struct ip6_hdr *ip6;
	struct tcphdr *th;
        uint32_t ehdrlen, ip_hlen;
	int ret = 0;
        uint16_t etype;
        device_t dev;
        uint8_t buf[sizeof(struct ip6_hdr)];

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
                        ip = (struct ip *)(mp->m_data + ehdrlen);

                        ip_hlen = sizeof (struct ip);

                        if (mp->m_len < (ehdrlen + ip_hlen)) {
                                m_copydata(mp, ehdrlen, sizeof(struct ip), buf);
                                ip = (struct ip *)buf;
                        }
			tx_mac->opcode = Q81_IOCB_TX_TSO;
			tx_mac->flags |= Q81_TX_TSO_FLAGS_IPV4 ;

			tx_mac->phdr_offsets = ehdrlen;

			tx_mac->phdr_offsets |= ((ehdrlen + ip_hlen) <<
							Q81_TX_TSO_PHDR_SHIFT);

			ip->ip_sum = 0;

			if (mp->m_pkthdr.csum_flags & CSUM_TSO) {
				tx_mac->flags |= Q81_TX_TSO_FLAGS_LSO;
				
				th = (struct tcphdr *)(ip + 1);

				th->th_sum = in_pseudo(ip->ip_src.s_addr,
						ip->ip_dst.s_addr,
						htons(IPPROTO_TCP));
				tx_mac->mss = mp->m_pkthdr.tso_segsz;
				tx_mac->phdr_length = ip_hlen + ehdrlen +
							(th->th_off << 2);
				break;
			}
			tx_mac->vlan_off |= Q81_TX_TSO_VLAN_OFF_IC ;


                        if (ip->ip_p == IPPROTO_TCP) {
				tx_mac->flags |= Q81_TX_TSO_FLAGS_TC;
                        } else if (ip->ip_p == IPPROTO_UDP) {
				tx_mac->flags |= Q81_TX_TSO_FLAGS_UC;
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

			tx_mac->opcode = Q81_IOCB_TX_TSO;
			tx_mac->flags |= Q81_TX_TSO_FLAGS_IPV6 ;
			tx_mac->vlan_off |= Q81_TX_TSO_VLAN_OFF_IC ;

			tx_mac->phdr_offsets = ehdrlen;
			tx_mac->phdr_offsets |= ((ehdrlen + ip_hlen) <<
							Q81_TX_TSO_PHDR_SHIFT);

                        if (ip6->ip6_nxt == IPPROTO_TCP) {
				tx_mac->flags |= Q81_TX_TSO_FLAGS_TC;
                        } else if (ip6->ip6_nxt == IPPROTO_UDP) {
				tx_mac->flags |= Q81_TX_TSO_FLAGS_UC;
                        }
                break;

                default:
                        ret = -1;
                break;
        }

        return (ret);
}

#define QLA_TX_MIN_FREE 2
int
qls_hw_tx_done(qla_host_t *ha, uint32_t txr_idx)
{
	uint32_t txr_done, txr_next;

	txr_done = ha->tx_ring[txr_idx].txr_done;
	txr_next = ha->tx_ring[txr_idx].txr_next;

	if (txr_done == txr_next) {
		ha->tx_ring[txr_idx].txr_free = NUM_TX_DESCRIPTORS;
	} else if (txr_done > txr_next) {
		ha->tx_ring[txr_idx].txr_free = txr_done - txr_next;
	} else {
		ha->tx_ring[txr_idx].txr_free = NUM_TX_DESCRIPTORS +
			txr_done - txr_next;
	}

	if (ha->tx_ring[txr_idx].txr_free <= QLA_TX_MIN_FREE)
		return (-1);

	return (0);
}

/*
 * Name: qls_hw_send
 * Function: Transmits a packet. It first checks if the packet is a
 *	candidate for Large TCP Segment Offload and then for UDP/TCP checksum
 *	offload. If either of these creteria are not met, it is transmitted
 *	as a regular ethernet frame.
 */
int
qls_hw_send(qla_host_t *ha, bus_dma_segment_t *segs, int nsegs,
	uint32_t txr_next,  struct mbuf *mp, uint32_t txr_idx)
{
        q81_tx_mac_t *tx_mac;
	q81_txb_desc_t *tx_desc;
        uint32_t total_length = 0;
        uint32_t i;
        device_t dev;
	int ret = 0;

	dev = ha->pci_dev;

        total_length = mp->m_pkthdr.len;

        if (total_length > QLA_MAX_TSO_FRAME_SIZE) {
                device_printf(dev, "%s: total length exceeds maxlen(%d)\n",
                        __func__, total_length);
                return (-1);
        }

	if (ha->tx_ring[txr_idx].txr_free <= (NUM_TX_DESCRIPTORS >> 2)) {
		if (qls_hw_tx_done(ha, txr_idx)) {
			device_printf(dev, "%s: tx_free[%d] = %d\n",
				__func__, txr_idx,
				ha->tx_ring[txr_idx].txr_free);
			return (-1);
		}
	}

	tx_mac = (q81_tx_mac_t *)&ha->tx_ring[txr_idx].wq_vaddr[txr_next];

	bzero(tx_mac, sizeof(q81_tx_mac_t));
	
	if ((mp->m_pkthdr.csum_flags &
			(CSUM_TCP | CSUM_UDP | CSUM_IP | CSUM_TSO)) != 0) {

		ret = qls_tx_tso_chksum(ha, mp, (q81_tx_tso_t *)tx_mac);
		if (ret) 
			return (EINVAL);

		if (mp->m_pkthdr.csum_flags & CSUM_TSO)
			ha->tx_ring[txr_idx].tx_tso_frames++;
		else
			ha->tx_ring[txr_idx].tx_frames++;
			
	} else { 
		tx_mac->opcode = Q81_IOCB_TX_MAC;
	}

	if (mp->m_flags & M_VLANTAG) {

		tx_mac->vlan_tci = mp->m_pkthdr.ether_vtag;
		tx_mac->vlan_off |= Q81_TX_MAC_VLAN_OFF_V;

		ha->tx_ring[txr_idx].tx_vlan_frames++;
	}

	tx_mac->frame_length = total_length;

	tx_mac->tid_lo = txr_next;

	if (nsegs <= MAX_TX_MAC_DESC) {

		QL_DPRINT2((dev, "%s: 1 [%d, %d]\n", __func__, total_length,
			tx_mac->tid_lo));

		for (i = 0; i < nsegs; i++) {
			tx_mac->txd[i].baddr = segs->ds_addr;
			tx_mac->txd[i].length = segs->ds_len;
			segs++;
		}
		tx_mac->txd[(nsegs - 1)].flags = Q81_RXB_DESC_FLAGS_E;

	} else {
		QL_DPRINT2((dev, "%s: 2 [%d, %d]\n", __func__, total_length,
			tx_mac->tid_lo));

		tx_mac->txd[0].baddr =
			ha->tx_ring[txr_idx].tx_buf[txr_next].oal_paddr;
		tx_mac->txd[0].length =
			nsegs * (sizeof(q81_txb_desc_t));
		tx_mac->txd[0].flags = Q81_RXB_DESC_FLAGS_C;

		tx_desc = ha->tx_ring[txr_idx].tx_buf[txr_next].oal_vaddr;

		for (i = 0; i < nsegs; i++) {
			tx_desc->baddr = segs->ds_addr;
			tx_desc->length = segs->ds_len;

			if (i == (nsegs -1))
				tx_desc->flags = Q81_RXB_DESC_FLAGS_E;
			else
				tx_desc->flags = 0;

			segs++;
			tx_desc++;
		}
	}
	txr_next = (txr_next + 1) & (NUM_TX_DESCRIPTORS - 1);
	ha->tx_ring[txr_idx].txr_next = txr_next;

	ha->tx_ring[txr_idx].txr_free--;

	Q81_WR_WQ_PROD_IDX(txr_idx, txr_next);

	return (0);
}

/*
 * Name: qls_del_hw_if
 * Function: Destroys the hardware specific entities corresponding to an
 *	Ethernet Interface
 */
void
qls_del_hw_if(qla_host_t *ha)
{
	uint32_t value;
	int i;
	//int  count;

	if (ha->hw_init == 0) {
		qls_hw_reset(ha);
		return;
	}

	for (i = 0;  i < ha->num_tx_rings; i++) {
		Q81_SET_WQ_INVALID(i); 
	}
	for (i = 0;  i < ha->num_rx_rings; i++) {
		Q81_SET_CQ_INVALID(i);
	}

	for (i = 0; i < ha->num_rx_rings; i++) {
		Q81_DISABLE_INTR(ha, i); /* MSI-x i */
	}

	value = (Q81_CTL_INTRE_IHD << Q81_CTL_INTRE_MASK_SHIFT);
	WRITE_REG32(ha, Q81_CTL_INTR_ENABLE, value);

	value = (Q81_CTL_INTRE_EI << Q81_CTL_INTRE_MASK_SHIFT);
	WRITE_REG32(ha, Q81_CTL_INTR_ENABLE, value);
	ha->flags.intr_enable = 0;

	qls_hw_reset(ha);

	return;
}

/*
 * Name: qls_init_hw_if
 * Function: Creates the hardware specific entities corresponding to an
 *	Ethernet Interface - Transmit and Receive Contexts. Sets the MAC Address
 *	corresponding to the interface. Enables LRO if allowed.
 */
int
qls_init_hw_if(qla_host_t *ha)
{
	device_t	dev;
	uint32_t	value;
	int		ret = 0;
	int		i;


	QL_DPRINT2((ha->pci_dev, "%s:enter\n", __func__));

	dev = ha->pci_dev;

	ret = qls_hw_reset(ha);
	if (ret)
		goto qls_init_hw_if_exit;

	ha->vm_pgsize = 4096;

	/* Enable FAE and EFE bits in System Register */
	value = Q81_CTL_SYSTEM_ENABLE_FAE | Q81_CTL_SYSTEM_ENABLE_EFE;
	value = (value << Q81_CTL_SYSTEM_MASK_SHIFT) | value;

	WRITE_REG32(ha, Q81_CTL_SYSTEM, value);

	/* Set Default Completion Queue_ID in NIC Rcv Configuration Register */
	value = (Q81_CTL_NIC_RCVC_DCQ_MASK << Q81_CTL_NIC_RCVC_MASK_SHIFT);
	WRITE_REG32(ha, Q81_CTL_NIC_RCV_CONFIG, value);

	/* Function Specific Control Register - Set Page Size and Enable NIC */
	value = Q81_CTL_FUNC_SPECIFIC_FE |
		Q81_CTL_FUNC_SPECIFIC_VM_PGSIZE_MASK |
		Q81_CTL_FUNC_SPECIFIC_EPC_O |
		Q81_CTL_FUNC_SPECIFIC_EPC_I |
		Q81_CTL_FUNC_SPECIFIC_EC;
	value = (value << Q81_CTL_FUNC_SPECIFIC_MASK_SHIFT) | 
                        Q81_CTL_FUNC_SPECIFIC_FE |
			Q81_CTL_FUNC_SPECIFIC_VM_PGSIZE_4K |
			Q81_CTL_FUNC_SPECIFIC_EPC_O |
			Q81_CTL_FUNC_SPECIFIC_EPC_I |
			Q81_CTL_FUNC_SPECIFIC_EC;

	WRITE_REG32(ha, Q81_CTL_FUNC_SPECIFIC, value);

	/* Interrupt Mask Register */
	value = Q81_CTL_INTRM_PI;
	value = (value << Q81_CTL_INTRM_MASK_SHIFT) | value;
	
	WRITE_REG32(ha, Q81_CTL_INTR_MASK, value);

	/* Initialiatize Completion Queue */
	for (i = 0; i < ha->num_rx_rings; i++) {
		ret = qls_init_comp_queue(ha, i);
		if (ret)
			goto qls_init_hw_if_exit;
	}

	if (ha->num_rx_rings > 1 ) {
		ret = qls_init_rss(ha);
		if (ret)
			goto qls_init_hw_if_exit;
	}

	/* Initialize Work Queue */

	for (i = 0; i < ha->num_tx_rings; i++) {
		ret = qls_init_work_queue(ha, i);
		if (ret)
			goto qls_init_hw_if_exit;
	}

	if (ret)
		goto qls_init_hw_if_exit;

	/* Set up CAM RAM with MAC Address */
	ret = qls_config_unicast_mac_addr(ha, 1);
	if (ret)
		goto qls_init_hw_if_exit;

	ret = qls_hw_add_all_mcast(ha);
	if (ret)
		goto qls_init_hw_if_exit;

	/* Initialize Firmware Routing Table */
	ret = qls_init_fw_routing_table(ha);
	if (ret)
		goto qls_init_hw_if_exit;

	/* Get Chip Revision ID */
	ha->rev_id = READ_REG32(ha, Q81_CTL_REV_ID);

	/* Enable Global Interrupt */
	value = Q81_CTL_INTRE_EI;
	value = (value << Q81_CTL_INTRE_MASK_SHIFT) | value;

	WRITE_REG32(ha, Q81_CTL_INTR_ENABLE, value);

	/* Enable Interrupt Handshake Disable */
	value = Q81_CTL_INTRE_IHD;
	value = (value << Q81_CTL_INTRE_MASK_SHIFT) | value;

	WRITE_REG32(ha, Q81_CTL_INTR_ENABLE, value);

	/* Enable Completion Interrupt */

	ha->flags.intr_enable = 1;

	for (i = 0; i < ha->num_rx_rings; i++) {
		Q81_ENABLE_INTR(ha, i); /* MSI-x i */
	}

	ha->hw_init = 1;

	qls_mbx_get_link_status(ha);

	QL_DPRINT2((ha->pci_dev, "%s:rxr [0x%08x]\n", __func__,
		ha->rx_ring[0].cq_db_offset));
	QL_DPRINT2((ha->pci_dev, "%s:txr [0x%08x]\n", __func__,
		ha->tx_ring[0].wq_db_offset));

	for (i = 0; i < ha->num_rx_rings; i++) {

		Q81_WR_CQ_CONS_IDX(i, 0);
		Q81_WR_LBQ_PROD_IDX(i, ha->rx_ring[i].lbq_in);
		Q81_WR_SBQ_PROD_IDX(i, ha->rx_ring[i].sbq_in);

		QL_DPRINT2((dev, "%s: [wq_idx, cq_idx, lbq_idx, sbq_idx]"
			"[0x%08x, 0x%08x, 0x%08x, 0x%08x]\n", __func__,
			Q81_RD_WQ_IDX(i), Q81_RD_CQ_IDX(i), Q81_RD_LBQ_IDX(i),
			Q81_RD_SBQ_IDX(i)));
	}

	for (i = 0; i < ha->num_rx_rings; i++) {
		Q81_SET_CQ_VALID(i);
	}

qls_init_hw_if_exit:
	QL_DPRINT2((ha->pci_dev, "%s:exit\n", __func__));
	return (ret);
}

static int
qls_wait_for_config_reg_bits(qla_host_t *ha, uint32_t bits, uint32_t value)
{
	uint32_t data32;
	uint32_t count = 3;

	while (count--) {

		data32 = READ_REG32(ha, Q81_CTL_CONFIG);

		if ((data32 & bits) == value)
			return (0);
		
		QLA_USEC_DELAY(100);
	}
	ha->qla_initiate_recovery = 1;
	device_printf(ha->pci_dev, "%s: failed\n", __func__);
	return (-1);
}

static uint8_t q81_hash_key[] = {
			0xda, 0x56, 0x5a, 0x6d,
			0xc2, 0x0e, 0x5b, 0x25,
			0x3d, 0x25, 0x67, 0x41,
			0xb0, 0x8f, 0xa3, 0x43,
			0xcb, 0x2b, 0xca, 0xd0,
			0xb4, 0x30, 0x7b, 0xae,
			0xa3, 0x2d, 0xcb, 0x77,
			0x0c, 0xf2, 0x30, 0x80,
			0x3b, 0xb7, 0x42, 0x6a,
			0xfa, 0x01, 0xac, 0xbe };

static int
qls_init_rss(qla_host_t *ha)
{
	q81_rss_icb_t	*rss_icb;
	int		ret = 0;
	int		i;
	uint32_t	value;

	rss_icb = ha->rss_dma.dma_b;

	bzero(rss_icb, sizeof (q81_rss_icb_t));

	rss_icb->flags_base_cq_num = Q81_RSS_ICB_FLAGS_L4K |
				Q81_RSS_ICB_FLAGS_L6K | Q81_RSS_ICB_FLAGS_LI |
				Q81_RSS_ICB_FLAGS_LB | Q81_RSS_ICB_FLAGS_LM |
				Q81_RSS_ICB_FLAGS_RT4 | Q81_RSS_ICB_FLAGS_RT6; 

	rss_icb->mask = 0x3FF;

	for (i = 0; i < Q81_RSS_ICB_NUM_INDTBL_ENTRIES; i++) {
		rss_icb->cq_id[i] = (i & (ha->num_rx_rings - 1));
	}

	memcpy(rss_icb->ipv6_rss_hash_key, q81_hash_key, 40);
	memcpy(rss_icb->ipv4_rss_hash_key, q81_hash_key, 16);

	ret = qls_wait_for_config_reg_bits(ha, Q81_CTL_CONFIG_LR, 0);

	if (ret)
		goto qls_init_rss_exit;

	ret = qls_sem_lock(ha, Q81_CTL_SEM_MASK_ICB, Q81_CTL_SEM_SET_ICB);

	if (ret) {
		QL_DPRINT1((ha->pci_dev, "%s: semlock failed\n", __func__));
		goto qls_init_rss_exit;
	}

	value = (uint32_t)ha->rss_dma.dma_addr;
	WRITE_REG32(ha, Q81_CTL_ICB_ACCESS_ADDR_LO, value);

	value = (uint32_t)(ha->rss_dma.dma_addr >> 32);
	WRITE_REG32(ha, Q81_CTL_ICB_ACCESS_ADDR_HI, value);

	qls_sem_unlock(ha, Q81_CTL_SEM_MASK_ICB);

	value = (Q81_CTL_CONFIG_LR << Q81_CTL_CONFIG_MASK_SHIFT) |
			Q81_CTL_CONFIG_LR;

	WRITE_REG32(ha, Q81_CTL_CONFIG, value);

	ret = qls_wait_for_config_reg_bits(ha, Q81_CTL_CONFIG_LR, 0);

qls_init_rss_exit:
	return (ret);
}

static int
qls_init_comp_queue(qla_host_t *ha, int cid)
{
	q81_cq_icb_t	*cq_icb;
	qla_rx_ring_t	*rxr;
	int		ret = 0;
	uint32_t	value;

	rxr = &ha->rx_ring[cid];

	rxr->cq_db_offset = ha->vm_pgsize * (128 + cid);

	cq_icb = rxr->cq_icb_vaddr;

	bzero(cq_icb, sizeof (q81_cq_icb_t));

	cq_icb->msix_vector = cid;
	cq_icb->flags = Q81_CQ_ICB_FLAGS_LC |
			Q81_CQ_ICB_FLAGS_LI |
			Q81_CQ_ICB_FLAGS_LL |
			Q81_CQ_ICB_FLAGS_LS |
			Q81_CQ_ICB_FLAGS_LV;
	
	cq_icb->length_v = NUM_CQ_ENTRIES;

	cq_icb->cq_baddr_lo = (rxr->cq_base_paddr & 0xFFFFFFFF);
	cq_icb->cq_baddr_hi = (rxr->cq_base_paddr >> 32) & 0xFFFFFFFF;

	cq_icb->cqi_addr_lo = (rxr->cqi_paddr & 0xFFFFFFFF);
	cq_icb->cqi_addr_hi = (rxr->cqi_paddr >> 32) & 0xFFFFFFFF;

	cq_icb->pkt_idelay = 10;
	cq_icb->idelay = 100;

	cq_icb->lbq_baddr_lo = (rxr->lbq_addr_tbl_paddr & 0xFFFFFFFF);
	cq_icb->lbq_baddr_hi = (rxr->lbq_addr_tbl_paddr >> 32) & 0xFFFFFFFF;

	cq_icb->lbq_bsize = QLA_LGB_SIZE;
	cq_icb->lbq_length = QLA_NUM_LGB_ENTRIES;

	cq_icb->sbq_baddr_lo = (rxr->sbq_addr_tbl_paddr & 0xFFFFFFFF);
	cq_icb->sbq_baddr_hi = (rxr->sbq_addr_tbl_paddr >> 32) & 0xFFFFFFFF;

	cq_icb->sbq_bsize = (uint16_t)ha->msize;
	cq_icb->sbq_length = QLA_NUM_SMB_ENTRIES;

	QL_DUMP_CQ(ha);

	ret = qls_wait_for_config_reg_bits(ha, Q81_CTL_CONFIG_LCQ, 0);

	if (ret)
		goto qls_init_comp_queue_exit;

	ret = qls_sem_lock(ha, Q81_CTL_SEM_MASK_ICB, Q81_CTL_SEM_SET_ICB);

	if (ret) {
		QL_DPRINT1((ha->pci_dev, "%s: semlock failed\n", __func__));
		goto qls_init_comp_queue_exit;
	}

	value = (uint32_t)rxr->cq_icb_paddr;
	WRITE_REG32(ha, Q81_CTL_ICB_ACCESS_ADDR_LO, value);

	value = (uint32_t)(rxr->cq_icb_paddr >> 32);
	WRITE_REG32(ha, Q81_CTL_ICB_ACCESS_ADDR_HI, value);

	qls_sem_unlock(ha, Q81_CTL_SEM_MASK_ICB);

	value = Q81_CTL_CONFIG_LCQ | Q81_CTL_CONFIG_Q_NUM_MASK;
	value = (value << Q81_CTL_CONFIG_MASK_SHIFT) | Q81_CTL_CONFIG_LCQ;
	value |= (cid << Q81_CTL_CONFIG_Q_NUM_SHIFT);
	WRITE_REG32(ha, Q81_CTL_CONFIG, value);

	ret = qls_wait_for_config_reg_bits(ha, Q81_CTL_CONFIG_LCQ, 0);

	rxr->cq_next = 0;
	rxr->lbq_next = rxr->lbq_free = 0;
	rxr->sbq_next = rxr->sbq_free = 0;
	rxr->rx_free = rxr->rx_next = 0;
	rxr->lbq_in = (QLA_NUM_LGB_ENTRIES - 1) & ~0xF;
	rxr->sbq_in = (QLA_NUM_SMB_ENTRIES - 1) & ~0xF;

qls_init_comp_queue_exit:
	return (ret);
}

static int
qls_init_work_queue(qla_host_t *ha, int wid)
{
	q81_wq_icb_t	*wq_icb;
	qla_tx_ring_t	*txr;
	int		ret = 0;
	uint32_t	value;

	txr = &ha->tx_ring[wid];

	txr->wq_db_addr = (struct resource *)((uint8_t *)ha->pci_reg1
						+ (ha->vm_pgsize * wid));

	txr->wq_db_offset = (ha->vm_pgsize * wid);

	wq_icb = txr->wq_icb_vaddr;
	bzero(wq_icb, sizeof (q81_wq_icb_t));

	wq_icb->length_v = NUM_TX_DESCRIPTORS  |
				Q81_WQ_ICB_VALID;

	wq_icb->flags = Q81_WQ_ICB_FLAGS_LO | Q81_WQ_ICB_FLAGS_LI |
			Q81_WQ_ICB_FLAGS_LB | Q81_WQ_ICB_FLAGS_LC;

	wq_icb->wqcqid_rss = wid;

	wq_icb->baddr_lo = txr->wq_paddr & 0xFFFFFFFF;
	wq_icb->baddr_hi = (txr->wq_paddr >> 32)& 0xFFFFFFFF;

	wq_icb->ci_addr_lo = txr->txr_cons_paddr & 0xFFFFFFFF;
	wq_icb->ci_addr_hi = (txr->txr_cons_paddr >> 32)& 0xFFFFFFFF;

	ret = qls_wait_for_config_reg_bits(ha, Q81_CTL_CONFIG_LRQ, 0);

	if (ret)
		goto qls_init_wq_exit;

	ret = qls_sem_lock(ha, Q81_CTL_SEM_MASK_ICB, Q81_CTL_SEM_SET_ICB);

	if (ret) {
		QL_DPRINT1((ha->pci_dev, "%s: semlock failed\n", __func__));
		goto qls_init_wq_exit;
	}

	value = (uint32_t)txr->wq_icb_paddr;
	WRITE_REG32(ha, Q81_CTL_ICB_ACCESS_ADDR_LO, value);

	value = (uint32_t)(txr->wq_icb_paddr >> 32);
	WRITE_REG32(ha, Q81_CTL_ICB_ACCESS_ADDR_HI, value);

	qls_sem_unlock(ha, Q81_CTL_SEM_MASK_ICB);

	value = Q81_CTL_CONFIG_LRQ | Q81_CTL_CONFIG_Q_NUM_MASK;
	value = (value << Q81_CTL_CONFIG_MASK_SHIFT) | Q81_CTL_CONFIG_LRQ;
	value |= (wid << Q81_CTL_CONFIG_Q_NUM_SHIFT);
	WRITE_REG32(ha, Q81_CTL_CONFIG, value);

	ret = qls_wait_for_config_reg_bits(ha, Q81_CTL_CONFIG_LRQ, 0);

	txr->txr_free = NUM_TX_DESCRIPTORS;
	txr->txr_next = 0;
	txr->txr_done = 0;

qls_init_wq_exit:
	return (ret);
}

static int
qls_hw_add_all_mcast(qla_host_t *ha)
{
	int i, nmcast;

	nmcast = ha->nmcast;

	for (i = 0 ; ((i < Q8_MAX_NUM_MULTICAST_ADDRS) && nmcast); i++) {
		if ((ha->mcast[i].addr[0] != 0) || 
			(ha->mcast[i].addr[1] != 0) ||
			(ha->mcast[i].addr[2] != 0) ||
			(ha->mcast[i].addr[3] != 0) ||
			(ha->mcast[i].addr[4] != 0) ||
			(ha->mcast[i].addr[5] != 0)) {

			if (qls_config_mcast_mac_addr(ha, ha->mcast[i].addr,
				1, i)) {
                		device_printf(ha->pci_dev, "%s: failed\n",
					__func__);
				return (-1);
			}

			nmcast--;
		}
	}
	return 0;
}

static int
qls_hw_add_mcast(qla_host_t *ha, uint8_t *mta)
{
	int i;

	for (i = 0; i < Q8_MAX_NUM_MULTICAST_ADDRS; i++) {

		if (QL_MAC_CMP(ha->mcast[i].addr, mta) == 0)
			return 0; /* its been already added */
	}

	for (i = 0; i < Q8_MAX_NUM_MULTICAST_ADDRS; i++) {

		if ((ha->mcast[i].addr[0] == 0) && 
			(ha->mcast[i].addr[1] == 0) &&
			(ha->mcast[i].addr[2] == 0) &&
			(ha->mcast[i].addr[3] == 0) &&
			(ha->mcast[i].addr[4] == 0) &&
			(ha->mcast[i].addr[5] == 0)) {

			if (qls_config_mcast_mac_addr(ha, mta, 1, i))
				return (-1);

			bcopy(mta, ha->mcast[i].addr, Q8_MAC_ADDR_LEN);
			ha->nmcast++;	

			return 0;
		}
	}
	return 0;
}

static int
qls_hw_del_mcast(qla_host_t *ha, uint8_t *mta)
{
	int i;

	for (i = 0; i < Q8_MAX_NUM_MULTICAST_ADDRS; i++) {
		if (QL_MAC_CMP(ha->mcast[i].addr, mta) == 0) {

			if (qls_config_mcast_mac_addr(ha, mta, 0, i))
				return (-1);

			ha->mcast[i].addr[0] = 0;
			ha->mcast[i].addr[1] = 0;
			ha->mcast[i].addr[2] = 0;
			ha->mcast[i].addr[3] = 0;
			ha->mcast[i].addr[4] = 0;
			ha->mcast[i].addr[5] = 0;

			ha->nmcast--;	

			return 0;
		}
	}
	return 0;
}

/*
 * Name: qls_hw_set_multi
 * Function: Sets the Multicast Addresses provided the host O.S into the
 *	hardware (for the given interface)
 */
void
qls_hw_set_multi(qla_host_t *ha, uint8_t *mta, uint32_t mcnt,
	uint32_t add_mac)
{
	int i;

	for (i = 0; i < mcnt; i++) {
		if (add_mac) {
			if (qls_hw_add_mcast(ha, mta))
				break;
		} else {
			if (qls_hw_del_mcast(ha, mta))
				break;
		}
			
		mta += Q8_MAC_ADDR_LEN;
	}
	return;
}

void
qls_update_link_state(qla_host_t *ha)
{
	uint32_t link_state;
	uint32_t prev_link_state;

	if (!(ha->ifp->if_drv_flags & IFF_DRV_RUNNING)) {
		ha->link_up = 0;
		return;
	}
	link_state = READ_REG32(ha, Q81_CTL_STATUS);

	prev_link_state =  ha->link_up;

	if ((ha->pci_func & 0x1) == 0)
		ha->link_up = ((link_state & Q81_CTL_STATUS_PL0)? 1 : 0);
	else
		ha->link_up = ((link_state & Q81_CTL_STATUS_PL1)? 1 : 0);

	if (prev_link_state !=  ha->link_up) {


		if (ha->link_up) {
			if_link_state_change(ha->ifp, LINK_STATE_UP);
		} else {
			if_link_state_change(ha->ifp, LINK_STATE_DOWN);
		}
	}
	return;
}

static void
qls_free_tx_ring_dma(qla_host_t *ha, int r_idx)
{
	if (ha->tx_ring[r_idx].flags.wq_dma) {
		qls_free_dmabuf(ha, &ha->tx_ring[r_idx].wq_dma);
		ha->tx_ring[r_idx].flags.wq_dma = 0;
	}

	if (ha->tx_ring[r_idx].flags.privb_dma) {
		qls_free_dmabuf(ha, &ha->tx_ring[r_idx].privb_dma);
		ha->tx_ring[r_idx].flags.privb_dma = 0;
	}
	return;
}

static void
qls_free_tx_dma(qla_host_t *ha)
{
	int i, j;
	qla_tx_buf_t *txb;

	for (i = 0; i < ha->num_tx_rings; i++) {

		qls_free_tx_ring_dma(ha, i);

		for (j = 0; j < NUM_TX_DESCRIPTORS; j++) {

			txb = &ha->tx_ring[i].tx_buf[j];

			if (txb->map) {
				bus_dmamap_destroy(ha->tx_tag, txb->map);
			}
		}
	}

        if (ha->tx_tag != NULL) {
                bus_dma_tag_destroy(ha->tx_tag);
                ha->tx_tag = NULL;
        }

	return;
}

static int
qls_alloc_tx_ring_dma(qla_host_t *ha, int ridx)
{
	int		ret = 0, i;
	uint8_t		*v_addr;
	bus_addr_t	p_addr;
	qla_tx_buf_t	*txb;
	device_t	dev = ha->pci_dev;

	ha->tx_ring[ridx].wq_dma.alignment = 8;
	ha->tx_ring[ridx].wq_dma.size =
		NUM_TX_DESCRIPTORS * (sizeof (q81_tx_cmd_t));

	ret = qls_alloc_dmabuf(ha, &ha->tx_ring[ridx].wq_dma);

	if (ret) {
		device_printf(dev, "%s: [%d] txr failed\n", __func__, ridx);
		goto qls_alloc_tx_ring_dma_exit;
	}
	ha->tx_ring[ridx].flags.wq_dma = 1;

	ha->tx_ring[ridx].privb_dma.alignment = 8;
	ha->tx_ring[ridx].privb_dma.size = QLA_TX_PRIVATE_BSIZE;

	ret = qls_alloc_dmabuf(ha, &ha->tx_ring[ridx].privb_dma);

	if (ret) {
		device_printf(dev, "%s: [%d] oalb failed\n", __func__, ridx);
		goto qls_alloc_tx_ring_dma_exit;
	}

	ha->tx_ring[ridx].flags.privb_dma = 1;

	ha->tx_ring[ridx].wq_vaddr = ha->tx_ring[ridx].wq_dma.dma_b;
	ha->tx_ring[ridx].wq_paddr = ha->tx_ring[ridx].wq_dma.dma_addr;

	v_addr = ha->tx_ring[ridx].privb_dma.dma_b;
	p_addr = ha->tx_ring[ridx].privb_dma.dma_addr;

	ha->tx_ring[ridx].wq_icb_vaddr = v_addr;
	ha->tx_ring[ridx].wq_icb_paddr = p_addr;

	ha->tx_ring[ridx].txr_cons_vaddr =
		(uint32_t *)(v_addr + (PAGE_SIZE >> 1));
	ha->tx_ring[ridx].txr_cons_paddr = p_addr + (PAGE_SIZE >> 1);

	v_addr = v_addr + (PAGE_SIZE >> 1);
	p_addr = p_addr + (PAGE_SIZE >> 1);

	txb = ha->tx_ring[ridx].tx_buf;

	for (i = 0; i < NUM_TX_DESCRIPTORS; i++) {

		txb[i].oal_vaddr = v_addr;
		txb[i].oal_paddr = p_addr;

		v_addr = v_addr + QLA_OAL_BLK_SIZE;
		p_addr = p_addr + QLA_OAL_BLK_SIZE;
	}

qls_alloc_tx_ring_dma_exit:
	return (ret);
}

static int
qls_alloc_tx_dma(qla_host_t *ha)
{
	int	i, j;
	int	ret = 0;
	qla_tx_buf_t *txb;

        if (bus_dma_tag_create(NULL,    /* parent */
                1, 0,    /* alignment, bounds */
                BUS_SPACE_MAXADDR,       /* lowaddr */
                BUS_SPACE_MAXADDR,       /* highaddr */
                NULL, NULL,      /* filter, filterarg */
                QLA_MAX_TSO_FRAME_SIZE,     /* maxsize */
                QLA_MAX_SEGMENTS,        /* nsegments */
                PAGE_SIZE,        /* maxsegsize */
                BUS_DMA_ALLOCNOW,        /* flags */
                NULL,    /* lockfunc */
                NULL,    /* lockfuncarg */
                &ha->tx_tag)) {
                device_printf(ha->pci_dev, "%s: tx_tag alloc failed\n",
                        __func__);
                return (ENOMEM);
        }

	for (i = 0; i < ha->num_tx_rings; i++) {

		ret = qls_alloc_tx_ring_dma(ha, i);

		if (ret) {
			qls_free_tx_dma(ha);
			break;
		}

		for (j = 0; j < NUM_TX_DESCRIPTORS; j++) {

			txb = &ha->tx_ring[i].tx_buf[j];

			ret = bus_dmamap_create(ha->tx_tag,
				BUS_DMA_NOWAIT, &txb->map);
			if (ret) {
				ha->err_tx_dmamap_create++;
				device_printf(ha->pci_dev,
				"%s: bus_dmamap_create failed[%d, %d, %d]\n",
				__func__, ret, i, j);

				qls_free_tx_dma(ha);

                		return (ret);
       			}
		}
	}

	return (ret);
}

static void
qls_free_rss_dma(qla_host_t *ha)
{
	qls_free_dmabuf(ha, &ha->rss_dma);
	ha->flags.rss_dma = 0;
}

static int
qls_alloc_rss_dma(qla_host_t *ha)
{
	int ret = 0;

	ha->rss_dma.alignment = 4;
	ha->rss_dma.size = PAGE_SIZE;

	ret = qls_alloc_dmabuf(ha, &ha->rss_dma);

	if (ret)
		device_printf(ha->pci_dev, "%s: failed\n", __func__);
	else
		ha->flags.rss_dma = 1;

	return (ret);
}

static void
qls_free_mpi_dma(qla_host_t *ha)
{
	qls_free_dmabuf(ha, &ha->mpi_dma);
	ha->flags.mpi_dma = 0;
}

static int
qls_alloc_mpi_dma(qla_host_t *ha)
{
	int ret = 0;

	ha->mpi_dma.alignment = 4;
	ha->mpi_dma.size = (0x4000 * 4);

	ret = qls_alloc_dmabuf(ha, &ha->mpi_dma);
	if (ret)
		device_printf(ha->pci_dev, "%s: failed\n", __func__);
	else
		ha->flags.mpi_dma = 1;

	return (ret);
}

static void
qls_free_rx_ring_dma(qla_host_t *ha, int ridx)
{
	if (ha->rx_ring[ridx].flags.cq_dma) {
		qls_free_dmabuf(ha, &ha->rx_ring[ridx].cq_dma);
		ha->rx_ring[ridx].flags.cq_dma = 0;
	}

	if (ha->rx_ring[ridx].flags.lbq_dma) {
		qls_free_dmabuf(ha, &ha->rx_ring[ridx].lbq_dma);
		ha->rx_ring[ridx].flags.lbq_dma = 0;
	}

	if (ha->rx_ring[ridx].flags.sbq_dma) {
		qls_free_dmabuf(ha, &ha->rx_ring[ridx].sbq_dma);
		ha->rx_ring[ridx].flags.sbq_dma = 0;
	}

	if (ha->rx_ring[ridx].flags.lb_dma) {
		qls_free_dmabuf(ha, &ha->rx_ring[ridx].lb_dma);
		ha->rx_ring[ridx].flags.lb_dma = 0;
	}
	return;
}

static void
qls_free_rx_dma(qla_host_t *ha)
{
	int i;

	for (i = 0; i < ha->num_rx_rings; i++) {
		qls_free_rx_ring_dma(ha, i);
	}

        if (ha->rx_tag != NULL) {
                bus_dma_tag_destroy(ha->rx_tag);
                ha->rx_tag = NULL;
        }

	return;
}

static int
qls_alloc_rx_ring_dma(qla_host_t *ha, int ridx)
{
	int				i, ret = 0;
	uint8_t				*v_addr;
	bus_addr_t			p_addr;
	volatile q81_bq_addr_e_t	*bq_e;
	device_t			dev = ha->pci_dev;

	ha->rx_ring[ridx].cq_dma.alignment = 128;
	ha->rx_ring[ridx].cq_dma.size =
		(NUM_CQ_ENTRIES * (sizeof (q81_cq_e_t))) + PAGE_SIZE;

	ret = qls_alloc_dmabuf(ha, &ha->rx_ring[ridx].cq_dma);

	if (ret) {
		device_printf(dev, "%s: [%d] cq failed\n", __func__, ridx);
		goto qls_alloc_rx_ring_dma_exit;
	}
	ha->rx_ring[ridx].flags.cq_dma = 1;

	ha->rx_ring[ridx].lbq_dma.alignment = 8;
	ha->rx_ring[ridx].lbq_dma.size = QLA_LGBQ_AND_TABLE_SIZE;

	ret = qls_alloc_dmabuf(ha, &ha->rx_ring[ridx].lbq_dma);

	if (ret) {
		device_printf(dev, "%s: [%d] lbq failed\n", __func__, ridx);
		goto qls_alloc_rx_ring_dma_exit;
	}
	ha->rx_ring[ridx].flags.lbq_dma = 1;

	ha->rx_ring[ridx].sbq_dma.alignment = 8;
	ha->rx_ring[ridx].sbq_dma.size = QLA_SMBQ_AND_TABLE_SIZE;

	ret = qls_alloc_dmabuf(ha, &ha->rx_ring[ridx].sbq_dma);

	if (ret) {
		device_printf(dev, "%s: [%d] sbq failed\n", __func__, ridx);
		goto qls_alloc_rx_ring_dma_exit;
	}
	ha->rx_ring[ridx].flags.sbq_dma = 1;

	ha->rx_ring[ridx].lb_dma.alignment = 8;
	ha->rx_ring[ridx].lb_dma.size = (QLA_LGB_SIZE * QLA_NUM_LGB_ENTRIES);

	ret = qls_alloc_dmabuf(ha, &ha->rx_ring[ridx].lb_dma);
	if (ret) {
		device_printf(dev, "%s: [%d] lb failed\n", __func__, ridx);
		goto qls_alloc_rx_ring_dma_exit;
	}
	ha->rx_ring[ridx].flags.lb_dma = 1;

	bzero(ha->rx_ring[ridx].cq_dma.dma_b, ha->rx_ring[ridx].cq_dma.size);
	bzero(ha->rx_ring[ridx].lbq_dma.dma_b, ha->rx_ring[ridx].lbq_dma.size);
	bzero(ha->rx_ring[ridx].sbq_dma.dma_b, ha->rx_ring[ridx].sbq_dma.size);
	bzero(ha->rx_ring[ridx].lb_dma.dma_b, ha->rx_ring[ridx].lb_dma.size);

	/* completion queue */
	ha->rx_ring[ridx].cq_base_vaddr = ha->rx_ring[ridx].cq_dma.dma_b;
	ha->rx_ring[ridx].cq_base_paddr = ha->rx_ring[ridx].cq_dma.dma_addr;

	v_addr = ha->rx_ring[ridx].cq_dma.dma_b;
	p_addr = ha->rx_ring[ridx].cq_dma.dma_addr;

	v_addr = v_addr + (NUM_CQ_ENTRIES * (sizeof (q81_cq_e_t)));
	p_addr = p_addr + (NUM_CQ_ENTRIES * (sizeof (q81_cq_e_t)));

	/* completion queue icb */
	ha->rx_ring[ridx].cq_icb_vaddr = v_addr;
	ha->rx_ring[ridx].cq_icb_paddr = p_addr;

	v_addr = v_addr + (PAGE_SIZE >> 2);
	p_addr = p_addr + (PAGE_SIZE >> 2);

	/* completion queue index register */
	ha->rx_ring[ridx].cqi_vaddr = (uint32_t *)v_addr;
	ha->rx_ring[ridx].cqi_paddr = p_addr;

	v_addr = ha->rx_ring[ridx].lbq_dma.dma_b;
	p_addr = ha->rx_ring[ridx].lbq_dma.dma_addr;

	/* large buffer queue address table */
	ha->rx_ring[ridx].lbq_addr_tbl_vaddr = v_addr;
	ha->rx_ring[ridx].lbq_addr_tbl_paddr = p_addr;

	/* large buffer queue */
	ha->rx_ring[ridx].lbq_vaddr = v_addr + PAGE_SIZE;
	ha->rx_ring[ridx].lbq_paddr = p_addr + PAGE_SIZE;
	
	v_addr = ha->rx_ring[ridx].sbq_dma.dma_b;
	p_addr = ha->rx_ring[ridx].sbq_dma.dma_addr;

	/* small buffer queue address table */
	ha->rx_ring[ridx].sbq_addr_tbl_vaddr = v_addr;
	ha->rx_ring[ridx].sbq_addr_tbl_paddr = p_addr;

	/* small buffer queue */
	ha->rx_ring[ridx].sbq_vaddr = v_addr + PAGE_SIZE;
	ha->rx_ring[ridx].sbq_paddr = p_addr + PAGE_SIZE;

	ha->rx_ring[ridx].lb_vaddr = ha->rx_ring[ridx].lb_dma.dma_b;
	ha->rx_ring[ridx].lb_paddr = ha->rx_ring[ridx].lb_dma.dma_addr;

	/* Initialize Large Buffer Queue Table */

	p_addr = ha->rx_ring[ridx].lbq_paddr;
	bq_e = ha->rx_ring[ridx].lbq_addr_tbl_vaddr;

	bq_e->addr_lo = p_addr & 0xFFFFFFFF;
	bq_e->addr_hi = (p_addr >> 32) & 0xFFFFFFFF;

	p_addr = ha->rx_ring[ridx].lb_paddr;
	bq_e = ha->rx_ring[ridx].lbq_vaddr;

	for (i = 0; i < QLA_NUM_LGB_ENTRIES; i++) {
		bq_e->addr_lo = p_addr & 0xFFFFFFFF;
		bq_e->addr_hi = (p_addr >> 32) & 0xFFFFFFFF;

		p_addr = p_addr + QLA_LGB_SIZE;
		bq_e++;
	}

	/* Initialize Small Buffer Queue Table */

	p_addr = ha->rx_ring[ridx].sbq_paddr;
	bq_e = ha->rx_ring[ridx].sbq_addr_tbl_vaddr;

	for (i =0; i < (QLA_SBQ_SIZE/QLA_PAGE_SIZE); i++) {
		bq_e->addr_lo = p_addr & 0xFFFFFFFF;
		bq_e->addr_hi = (p_addr >> 32) & 0xFFFFFFFF;

		p_addr = p_addr + QLA_PAGE_SIZE;
		bq_e++;
	}

qls_alloc_rx_ring_dma_exit:
	return (ret);
}

static int
qls_alloc_rx_dma(qla_host_t *ha)
{
	int	i;
	int	ret = 0;

        if (bus_dma_tag_create(NULL,    /* parent */
                        1, 0,    /* alignment, bounds */
                        BUS_SPACE_MAXADDR,       /* lowaddr */
                        BUS_SPACE_MAXADDR,       /* highaddr */
                        NULL, NULL,      /* filter, filterarg */
                        MJUM9BYTES,     /* maxsize */
                        1,        /* nsegments */
                        MJUM9BYTES,        /* maxsegsize */
                        BUS_DMA_ALLOCNOW,        /* flags */
                        NULL,    /* lockfunc */
                        NULL,    /* lockfuncarg */
                        &ha->rx_tag)) {

                device_printf(ha->pci_dev, "%s: rx_tag alloc failed\n",
                        __func__);

                return (ENOMEM);
        }

	for (i = 0; i < ha->num_rx_rings; i++) {
		ret = qls_alloc_rx_ring_dma(ha, i);

		if (ret) {
			qls_free_rx_dma(ha);
			break;
		}
	}

	return (ret);
}

static int
qls_wait_for_flash_ready(qla_host_t *ha)
{
	uint32_t data32;
	uint32_t count = 3;

	while (count--) {

		data32 = READ_REG32(ha, Q81_CTL_FLASH_ADDR);

		if (data32 & Q81_CTL_FLASH_ADDR_ERR)
			goto qls_wait_for_flash_ready_exit;
		
		if (data32 & Q81_CTL_FLASH_ADDR_RDY)
			return (0);

		QLA_USEC_DELAY(100);
	}

qls_wait_for_flash_ready_exit:
	QL_DPRINT1((ha->pci_dev, "%s: failed\n", __func__));

	return (-1);
}

/*
 * Name: qls_rd_flash32
 * Function: Read Flash Memory
 */
int
qls_rd_flash32(qla_host_t *ha, uint32_t addr, uint32_t *data)
{
	int ret;

	ret = qls_wait_for_flash_ready(ha);

	if (ret)
		return (ret);

	WRITE_REG32(ha, Q81_CTL_FLASH_ADDR, (addr | Q81_CTL_FLASH_ADDR_R));

	ret = qls_wait_for_flash_ready(ha);

	if (ret)
		return (ret);

	*data = READ_REG32(ha, Q81_CTL_FLASH_DATA);

	return 0;
}

static int
qls_flash_validate(qla_host_t *ha, const char *signature)
{
	uint16_t csum16 = 0;
	uint16_t *data16;
	int i;

	if (bcmp(ha->flash.id, signature, 4)) {
		QL_DPRINT1((ha->pci_dev, "%s: invalid signature "
			"%x:%x:%x:%x %s\n", __func__, ha->flash.id[0],
			ha->flash.id[1], ha->flash.id[2], ha->flash.id[3],
			signature));
		return(-1);
	}

	data16 = (uint16_t *)&ha->flash;

	for (i = 0; i < (sizeof (q81_flash_t) >> 1); i++) {
		csum16 += *data16++;
	}

	if (csum16) {
		QL_DPRINT1((ha->pci_dev, "%s: invalid checksum\n", __func__));
		return(-1);
	}
	return(0);
}

int
qls_rd_nic_params(qla_host_t *ha)
{
	int		i, ret = 0;
	uint32_t	faddr;
	uint32_t	*qflash;

	if (qls_sem_lock(ha, Q81_CTL_SEM_MASK_FLASH, Q81_CTL_SEM_SET_FLASH)) {
		QL_DPRINT1((ha->pci_dev, "%s: semlock failed\n", __func__));
		return(-1);
	}

	if ((ha->pci_func & 0x1) == 0)
		faddr = Q81_F0_FLASH_OFFSET >> 2;
	else
		faddr = Q81_F1_FLASH_OFFSET >> 2;

	qflash = (uint32_t *)&ha->flash;

	for (i = 0; i < (sizeof(q81_flash_t) >> 2) ; i++) {

		ret = qls_rd_flash32(ha, faddr, qflash);

		if (ret)
			goto qls_rd_flash_data_exit;

		faddr++;
		qflash++;
	}

	QL_DUMP_BUFFER8(ha, __func__, (&ha->flash), (sizeof (q81_flash_t)));

	ret = qls_flash_validate(ha, Q81_FLASH_ID);

	if (ret)
		goto qls_rd_flash_data_exit;

	bcopy(ha->flash.mac_addr0, ha->mac_addr, ETHER_ADDR_LEN);

	QL_DPRINT1((ha->pci_dev, "%s: mac %02x:%02x:%02x:%02x:%02x:%02x\n",
		__func__, ha->mac_addr[0],  ha->mac_addr[1], ha->mac_addr[2],
		ha->mac_addr[3], ha->mac_addr[4],  ha->mac_addr[5]));

qls_rd_flash_data_exit:

	qls_sem_unlock(ha, Q81_CTL_SEM_MASK_FLASH);

	return(ret);
}

static int
qls_sem_lock(qla_host_t *ha, uint32_t mask, uint32_t value)
{
	uint32_t count = 30;
	uint32_t data;

	while (count--) {
		WRITE_REG32(ha, Q81_CTL_SEMAPHORE, (mask|value));
	
		data = READ_REG32(ha, Q81_CTL_SEMAPHORE);

		if (data & value) {
			return (0);
		} else {
			QLA_USEC_DELAY(100);
		}
	}
	ha->qla_initiate_recovery = 1;
	return (-1);
}

static void
qls_sem_unlock(qla_host_t *ha, uint32_t mask)
{
	WRITE_REG32(ha, Q81_CTL_SEMAPHORE, mask);
}

static int
qls_wait_for_proc_addr_ready(qla_host_t *ha)
{
	uint32_t data32;
	uint32_t count = 3;

	while (count--) {

		data32 = READ_REG32(ha, Q81_CTL_PROC_ADDR);

		if (data32 & Q81_CTL_PROC_ADDR_ERR)
			goto qls_wait_for_proc_addr_ready_exit;
		
		if (data32 & Q81_CTL_PROC_ADDR_RDY)
			return (0);

		QLA_USEC_DELAY(100);
	}

qls_wait_for_proc_addr_ready_exit:
	QL_DPRINT1((ha->pci_dev, "%s: failed\n", __func__));

	ha->qla_initiate_recovery = 1;
	return (-1);
}

static int
qls_proc_addr_rd_reg(qla_host_t *ha, uint32_t addr_module, uint32_t reg,
	uint32_t *data)
{
	int ret;
	uint32_t value;

	ret = qls_wait_for_proc_addr_ready(ha);

	if (ret)
		goto qls_proc_addr_rd_reg_exit;

	value = addr_module | reg | Q81_CTL_PROC_ADDR_READ;

	WRITE_REG32(ha, Q81_CTL_PROC_ADDR, value);

	ret = qls_wait_for_proc_addr_ready(ha);

	if (ret)
		goto qls_proc_addr_rd_reg_exit;
	
	*data = READ_REG32(ha, Q81_CTL_PROC_DATA); 

qls_proc_addr_rd_reg_exit:
	return (ret);
}

static int
qls_proc_addr_wr_reg(qla_host_t *ha, uint32_t addr_module, uint32_t reg,
	uint32_t data)
{
	int ret;
	uint32_t value;

	ret = qls_wait_for_proc_addr_ready(ha);

	if (ret)
		goto qls_proc_addr_wr_reg_exit;

	WRITE_REG32(ha, Q81_CTL_PROC_DATA, data);

	value = addr_module | reg;

	WRITE_REG32(ha, Q81_CTL_PROC_ADDR, value);

	ret = qls_wait_for_proc_addr_ready(ha);

qls_proc_addr_wr_reg_exit:
	return (ret);
}

static int
qls_hw_nic_reset(qla_host_t *ha)
{
	int		count;
	uint32_t	data;
	device_t	dev = ha->pci_dev;
	
	ha->hw_init = 0;

	data = (Q81_CTL_RESET_FUNC << Q81_CTL_RESET_MASK_SHIFT) |
			Q81_CTL_RESET_FUNC;
	WRITE_REG32(ha, Q81_CTL_RESET, data);

	count = 10;
	while (count--) {
		data = READ_REG32(ha, Q81_CTL_RESET);
		if ((data & Q81_CTL_RESET_FUNC) == 0)
			break;
		QLA_USEC_DELAY(10);
	}
	if (count == 0) {
		device_printf(dev, "%s: Bit 15 not cleared after Reset\n",
			__func__);
		return (-1);
	}
	return (0);
}
	
static int
qls_hw_reset(qla_host_t *ha)
{
	device_t	dev = ha->pci_dev;
	int		ret;
	int		count;
	uint32_t	data;

	QL_DPRINT2((ha->pci_dev, "%s:enter[%d]\n", __func__, ha->hw_init));

	if (ha->hw_init == 0) {
		ret = qls_hw_nic_reset(ha);
		goto qls_hw_reset_exit;
	}

	ret = qls_clear_routing_table(ha);
	if (ret) 
		goto qls_hw_reset_exit;

	ret = qls_mbx_set_mgmt_ctrl(ha, Q81_MBX_SET_MGMT_CTL_STOP);
	if (ret) 
		goto qls_hw_reset_exit;

	/*
	 * Wait for FIFO to empty
	 */
	count = 5;
	while (count--) {
		data = READ_REG32(ha, Q81_CTL_STATUS);
		if (data & Q81_CTL_STATUS_NFE)
			break;
		qls_mdelay(__func__, 100);
	}
	if (count == 0) {
		device_printf(dev, "%s: NFE bit not set\n", __func__);
		goto qls_hw_reset_exit;
	}

	count = 5;
	while (count--) {
		(void)qls_mbx_get_mgmt_ctrl(ha, &data);

		if ((data & Q81_MBX_GET_MGMT_CTL_FIFO_EMPTY) &&
			(data & Q81_MBX_GET_MGMT_CTL_SET_MGMT))
			break;
		qls_mdelay(__func__, 100);
	}
	if (count == 0)
		goto qls_hw_reset_exit;

	/*
	 * Reset the NIC function
	 */
	ret = qls_hw_nic_reset(ha);
	if (ret) 
		goto qls_hw_reset_exit;
	
	ret = qls_mbx_set_mgmt_ctrl(ha, Q81_MBX_SET_MGMT_CTL_RESUME);

qls_hw_reset_exit:
	if (ret)
		device_printf(dev, "%s: failed\n", __func__);
		
	return (ret);
}

/*
 * MPI Related Functions
 */
int
qls_mpi_risc_rd_reg(qla_host_t *ha, uint32_t reg, uint32_t *data)
{
	int ret;

	ret = qls_proc_addr_rd_reg(ha, Q81_CTL_PROC_ADDR_MPI_RISC,
			reg, data);
	return (ret);
}

int
qls_mpi_risc_wr_reg(qla_host_t *ha, uint32_t reg, uint32_t data)
{
	int ret;

	ret = qls_proc_addr_wr_reg(ha, Q81_CTL_PROC_ADDR_MPI_RISC,
			reg, data);
	return (ret);
}

int
qls_mbx_rd_reg(qla_host_t *ha, uint32_t reg, uint32_t *data)
{
	int ret;

	if ((ha->pci_func & 0x1) == 0)
		reg += Q81_FUNC0_MBX_OUT_REG0;
	else
		reg += Q81_FUNC1_MBX_OUT_REG0;

	ret = qls_mpi_risc_rd_reg(ha, reg, data);

	return (ret);
}

int
qls_mbx_wr_reg(qla_host_t *ha, uint32_t reg, uint32_t data)
{
	int ret;

	if ((ha->pci_func & 0x1) == 0)
		reg += Q81_FUNC0_MBX_IN_REG0;
	else
		reg += Q81_FUNC1_MBX_IN_REG0;

	ret = qls_mpi_risc_wr_reg(ha, reg, data);

	return (ret);
}


static int
qls_mbx_cmd(qla_host_t *ha, uint32_t *in_mbx, uint32_t i_count,
	uint32_t *out_mbx, uint32_t o_count)
{
	int i, ret = -1;
	uint32_t data32, mbx_cmd = 0;
	uint32_t count = 50;

	QL_DPRINT2((ha->pci_dev, "%s: enter[0x%08x 0x%08x 0x%08x]\n",
		__func__, *in_mbx, *(in_mbx + 1), *(in_mbx + 2)));

	data32 = READ_REG32(ha, Q81_CTL_HOST_CMD_STATUS);

	if (data32 & Q81_CTL_HCS_HTR_INTR) {
		device_printf(ha->pci_dev, "%s: cmd_status[0x%08x]\n",
			__func__, data32);
		goto qls_mbx_cmd_exit;
	}

	if (qls_sem_lock(ha, Q81_CTL_SEM_MASK_PROC_ADDR_NIC_RCV,
		Q81_CTL_SEM_SET_PROC_ADDR_NIC_RCV)) {
		device_printf(ha->pci_dev, "%s: semlock failed\n", __func__);
		goto qls_mbx_cmd_exit;
	}

	ha->mbx_done = 0;

	mbx_cmd = *in_mbx;

	for (i = 0; i < i_count; i++) {

		ret = qls_mbx_wr_reg(ha, i, *in_mbx);

		if (ret) {
			device_printf(ha->pci_dev,
				"%s: mbx_wr[%d, 0x%08x] failed\n", __func__,
				i, *in_mbx);
			qls_sem_unlock(ha, Q81_CTL_SEM_MASK_PROC_ADDR_NIC_RCV);
			goto qls_mbx_cmd_exit;
		}

		in_mbx++;
	}
	WRITE_REG32(ha, Q81_CTL_HOST_CMD_STATUS, Q81_CTL_HCS_CMD_SET_HTR_INTR);

	qls_sem_unlock(ha, Q81_CTL_SEM_MASK_PROC_ADDR_NIC_RCV);

	ret = -1;
	ha->mbx_done = 0;

	while (count--) {

		if (ha->flags.intr_enable == 0) {
			data32 = READ_REG32(ha, Q81_CTL_STATUS);

			if (!(data32 & Q81_CTL_STATUS_PI)) {
				qls_mdelay(__func__, 100);
				continue;
			}

			ret = qls_mbx_rd_reg(ha, 0, &data32);

			if (ret == 0 ) {
				if ((data32 & 0xF000) == 0x4000) {

					out_mbx[0] = data32;

					for (i = 1; i < o_count; i++) {
						ret = qls_mbx_rd_reg(ha, i,
								&data32);
						if (ret) {
							device_printf(
								ha->pci_dev,
								"%s: mbx_rd[%d]"
								" failed\n",
								__func__, i);
							break;
						}
						out_mbx[i] = data32;
					}
					break;
				} else if ((data32 & 0xF000) == 0x8000) {
					count = 50;
					WRITE_REG32(ha,\
						Q81_CTL_HOST_CMD_STATUS,\
						Q81_CTL_HCS_CMD_CLR_RTH_INTR);
				}
			}
		} else {
			if (ha->mbx_done) {
				for (i = 1; i < o_count; i++) {
					out_mbx[i] = ha->mbox[i];
				}
				ret = 0;
				break;
			}
		}
		qls_mdelay(__func__, 1000);
	}

qls_mbx_cmd_exit:

	if (ha->flags.intr_enable == 0) {
		WRITE_REG32(ha, Q81_CTL_HOST_CMD_STATUS,\
			Q81_CTL_HCS_CMD_CLR_RTH_INTR);
	}

	if (ret) {
		ha->qla_initiate_recovery = 1;
	}

	QL_DPRINT2((ha->pci_dev, "%s: exit[%d]\n", __func__, ret));
	return (ret);
}

static int
qls_mbx_set_mgmt_ctrl(qla_host_t *ha, uint32_t t_ctrl)
{
	uint32_t *mbox;
	device_t dev = ha->pci_dev;

	mbox = ha->mbox;
	bzero(mbox, (sizeof (uint32_t) * Q81_NUM_MBX_REGISTERS));

	mbox[0] = Q81_MBX_SET_MGMT_CTL;
	mbox[1] = t_ctrl;

	if (qls_mbx_cmd(ha, mbox, 2, mbox, 1)) {
		device_printf(dev, "%s failed\n", __func__);
		return (-1);
	}

	if ((mbox[0] == Q81_MBX_CMD_COMPLETE) ||
		((t_ctrl == Q81_MBX_SET_MGMT_CTL_STOP) &&
			(mbox[0] == Q81_MBX_CMD_ERROR))){
		return (0);
	}
	device_printf(dev, "%s failed [0x%08x]\n", __func__, mbox[0]);
	return (-1);

}

static int
qls_mbx_get_mgmt_ctrl(qla_host_t *ha, uint32_t *t_status)
{
	uint32_t *mbox;
	device_t dev = ha->pci_dev;

	*t_status = 0;

	mbox = ha->mbox;
	bzero(mbox, (sizeof (uint32_t) * Q81_NUM_MBX_REGISTERS));

	mbox[0] = Q81_MBX_GET_MGMT_CTL;

	if (qls_mbx_cmd(ha, mbox, 1, mbox, 2)) {
		device_printf(dev, "%s failed\n", __func__);
		return (-1);
	}

	*t_status = mbox[1];

	return (0);
}

static void
qls_mbx_get_link_status(qla_host_t *ha)
{
	uint32_t *mbox;
	device_t dev = ha->pci_dev;

	mbox = ha->mbox;
	bzero(mbox, (sizeof (uint32_t) * Q81_NUM_MBX_REGISTERS));

	mbox[0] = Q81_MBX_GET_LNK_STATUS;

	if (qls_mbx_cmd(ha, mbox, 1, mbox, 6)) {
		device_printf(dev, "%s failed\n", __func__);
		return;
	}

	ha->link_status			= mbox[1];
	ha->link_down_info		= mbox[2];
	ha->link_hw_info		= mbox[3];
	ha->link_dcbx_counters		= mbox[4];
	ha->link_change_counters	= mbox[5];

	device_printf(dev, "%s 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
		__func__, mbox[0],mbox[1],mbox[2],mbox[3],mbox[4],mbox[5]);

	return;
}

static void
qls_mbx_about_fw(qla_host_t *ha)
{
	uint32_t *mbox;
	device_t dev = ha->pci_dev;

	mbox = ha->mbox;
	bzero(mbox, (sizeof (uint32_t) * Q81_NUM_MBX_REGISTERS));

	mbox[0] = Q81_MBX_ABOUT_FW;

	if (qls_mbx_cmd(ha, mbox, 1, mbox, 6)) {
		device_printf(dev, "%s failed\n", __func__);
		return;
	}

	device_printf(dev, "%s 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
		__func__, mbox[0],mbox[1],mbox[2],mbox[3],mbox[4],mbox[5]);
}

int
qls_mbx_dump_risc_ram(qla_host_t *ha, void *buf, uint32_t r_addr,
	uint32_t r_size)
{
	bus_addr_t b_paddr;
	uint32_t *mbox;
	device_t dev = ha->pci_dev;

	mbox = ha->mbox;
	bzero(mbox, (sizeof (uint32_t) * Q81_NUM_MBX_REGISTERS));

	bzero(ha->mpi_dma.dma_b,(r_size << 2));
	b_paddr = ha->mpi_dma.dma_addr;

	mbox[0] = Q81_MBX_DUMP_RISC_RAM;
	mbox[1] = r_addr & 0xFFFF;
	mbox[2] = ((uint32_t)(b_paddr >> 16)) & 0xFFFF;
	mbox[3] = ((uint32_t)b_paddr) & 0xFFFF;
	mbox[4] = (r_size >> 16) & 0xFFFF;
	mbox[5] = r_size & 0xFFFF;
	mbox[6] = ((uint32_t)(b_paddr >> 48)) & 0xFFFF;
	mbox[7] = ((uint32_t)(b_paddr >> 32)) & 0xFFFF;
	mbox[8] = (r_addr >> 16) & 0xFFFF;

	bus_dmamap_sync(ha->mpi_dma.dma_tag, ha->mpi_dma.dma_map,
		BUS_DMASYNC_PREREAD);

	if (qls_mbx_cmd(ha, mbox, 9, mbox, 1)) {
		device_printf(dev, "%s failed\n", __func__);
		return (-1);
	}
        if (mbox[0] != 0x4000) {
                device_printf(ha->pci_dev, "%s: failed!\n", __func__);
		return (-1);
        } else {
                bus_dmamap_sync(ha->mpi_dma.dma_tag, ha->mpi_dma.dma_map,
                        BUS_DMASYNC_POSTREAD);
                bcopy(ha->mpi_dma.dma_b, buf, (r_size << 2));
        }

	return (0);
}

int 
qls_mpi_reset(qla_host_t *ha)
{
	int		count;
	uint32_t	data;
	device_t	dev = ha->pci_dev;
	
	WRITE_REG32(ha, Q81_CTL_HOST_CMD_STATUS,\
		Q81_CTL_HCS_CMD_SET_RISC_RESET);

	count = 10;
	while (count--) {
		data = READ_REG32(ha, Q81_CTL_HOST_CMD_STATUS);
		if (data & Q81_CTL_HCS_RISC_RESET) {
			WRITE_REG32(ha, Q81_CTL_HOST_CMD_STATUS,\
				Q81_CTL_HCS_CMD_CLR_RISC_RESET);
			break;
		}
		qls_mdelay(__func__, 10);
	}
	if (count == 0) {
		device_printf(dev, "%s: failed\n", __func__);
		return (-1);
	}
	return (0);
}
	
