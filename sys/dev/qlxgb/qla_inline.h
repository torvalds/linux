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
 *
 * $FreeBSD$
 */
/*
 * File: qla_inline.h
 * Author : David C Somayajulu, Qlogic Corporation, Aliso Viejo, CA 92656.
 */
#ifndef _QLA_INLINE_H_
#define _QLA_INLINE_H_

/*
 * Function: qla_hw_reset
 */
static __inline void qla_hw_reset(qla_host_t *ha)
{
        WRITE_OFFSET32(ha, Q8_ASIC_RESET, 0xFFFFFFFF);
}

#define QL8_SEMLOCK_TIMEOUT	1000/* QLA8020 Semaphore Lock Timeout 10ms */


/*
 * Inline functions for hardware semaphores
 */

/*
 * Name:	qla_sem_lock
 * Function:	Locks one of the semaphore registers (semaphore 2,3,5 & 7)
 *		If the id_reg is valid, then id_val is written into it.
 *		This is for debugging purpose
 * Returns:	0 on success; otherwise its failed.
 */
static __inline int
qla_sem_lock(qla_host_t *ha, uint32_t sem_reg, uint32_t id_reg, uint32_t id_val)
{
	int count = QL8_SEMLOCK_TIMEOUT;

	while (count) {
		if ((READ_REG32(ha, sem_reg) & SEM_LOCK_BIT))
			break;
		count--;

		if (!count)
			return(-1);
		qla_mdelay(__func__, 10);
	}
	if (id_reg)
		WRITE_OFFSET32(ha, id_reg, id_val);

	return(0);
}

/*
 * Name:	qla_sem_unlock
 * Function:	Unlocks the semaphore registers (semaphore 2,3,5 & 7)
 *		previously locked by qla_sem_lock()
 */
static __inline void
qla_sem_unlock(qla_host_t *ha, uint32_t sem_reg)
{
	READ_REG32(ha, sem_reg);
}

static __inline int
qla_get_ifq_snd_maxlen(qla_host_t *ha)
{
	return((NUM_TX_DESCRIPTORS - 1));
}

static __inline uint32_t
qla_get_optics(qla_host_t *ha)
{
	uint32_t link_speed;

	link_speed = READ_REG32(ha, Q8_LINK_SPEED_0);
	if (ha->pci_func == 0)
		link_speed = link_speed & 0xFF;
	else
		link_speed = (link_speed >> 8) & 0xFF;

	switch (link_speed) {
	case 0x1:
		link_speed = IFM_100_FX;
		break;

	case 0x10:
		link_speed = IFM_1000_SX;
		break;

	default:
		link_speed = (IFM_10G_LR | IFM_10G_SR);
		break;
	}

	return(link_speed);
}

static __inline uint8_t *
qla_get_mac_addr(qla_host_t *ha)
{
	return (ha->hw.mac_addr);
}

static __inline void
qla_read_mac_addr(qla_host_t *ha)
{
	uint32_t mac_crb_addr;
	uint32_t mac_lo;
	uint32_t mac_hi;
	uint8_t	*macp;

	mac_crb_addr = Q8_CRB_MAC_BLOCK_START +
		(((ha->pci_func >> 1) * 3) << 2) + ((ha->pci_func & 0x01) << 2);

	mac_lo = READ_REG32(ha, mac_crb_addr);
	mac_hi = READ_REG32(ha, (mac_crb_addr + 0x4));

	if (ha->pci_func & 0x01) {
		mac_lo = mac_lo >> 16;

		macp = (uint8_t *)&mac_lo;

		ha->hw.mac_addr[5] = macp[0];
		ha->hw.mac_addr[4] = macp[1];

		macp = (uint8_t *)&mac_hi;

		ha->hw.mac_addr[3] = macp[0];
		ha->hw.mac_addr[2] = macp[1];
		ha->hw.mac_addr[1] = macp[2];
		ha->hw.mac_addr[0] = macp[3];
	} else {
		macp = (uint8_t *)&mac_lo;

		ha->hw.mac_addr[5] = macp[0];
		ha->hw.mac_addr[4] = macp[1];
		ha->hw.mac_addr[3] = macp[2];
		ha->hw.mac_addr[2] = macp[3];

		macp = (uint8_t *)&mac_hi;

		ha->hw.mac_addr[1] = macp[0];
		ha->hw.mac_addr[0] = macp[1];
	}
	return;
}

static __inline void
qla_set_hw_rcv_desc(qla_host_t *ha, uint32_t ridx, uint32_t index,
	uint32_t handle, bus_addr_t paddr, uint32_t buf_size)
{
	q80_recv_desc_t *rcv_desc;

	rcv_desc = (q80_recv_desc_t *)ha->hw.dma_buf.rds_ring[ridx].dma_b;

	rcv_desc += index;

	rcv_desc->handle = (uint16_t)handle;
	rcv_desc->buf_size = buf_size;
	rcv_desc->buf_addr = paddr;

	return;
}

static __inline void
qla_init_hw_rcv_descriptors(qla_host_t *ha, uint32_t ridx)
{
	if (ridx == RDS_RING_INDEX_NORMAL)
		bzero((void *)ha->hw.dma_buf.rds_ring[ridx].dma_b,
			(sizeof(q80_recv_desc_t) * NUM_RX_DESCRIPTORS));
	else if (ridx == RDS_RING_INDEX_JUMBO)
		bzero((void *)ha->hw.dma_buf.rds_ring[ridx].dma_b,
			(sizeof(q80_recv_desc_t) * NUM_RX_JUMBO_DESCRIPTORS));
	else
		QL_ASSERT(0, ("%s: invalid rds index [%d]\n", __func__, ridx));
}

static __inline void
qla_lock(qla_host_t *ha, const char *str)
{
	while (1) {
		mtx_lock(&ha->hw_lock);
		if (!ha->hw_lock_held) {
			ha->hw_lock_held = 1;
			ha->qla_lock = str;
			mtx_unlock(&ha->hw_lock);
			break;
		}
		mtx_unlock(&ha->hw_lock);
		qla_mdelay(__func__, 1);
	}
	return;
}

static __inline void
qla_unlock(qla_host_t *ha, const char *str)
{
	mtx_lock(&ha->hw_lock);
	ha->hw_lock_held = 0;
	ha->qla_unlock = str;
	mtx_unlock(&ha->hw_lock);
}

#endif /* #ifndef _QLA_INLINE_H_ */
