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
 *
 * $FreeBSD$
 */
/*
 * File: ql_inline.h
 * Author : David C Somayajulu, Qlogic Corporation, Aliso Viejo, CA 92656.
 */
#ifndef _QL_INLINE_H_
#define _QL_INLINE_H_


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
		if ((READ_REG32(ha, sem_reg) & BIT_0))
			break;
		count--;

		if (!count)
			return(-1);
		qla_mdelay(__func__, 10);
	}
	if (id_reg)
		WRITE_REG32(ha, id_reg, id_val);

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
	return(((NUM_TX_DESCRIPTORS * 4) - 1));
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
		if ((ha->hw.module_type == 0x4) ||
			(ha->hw.module_type == 0x5) ||
			(ha->hw.module_type == 0x6))
			link_speed = (IFM_10G_TWINAX);
		else
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
qla_set_hw_rcv_desc(qla_host_t *ha, uint32_t r_idx, uint32_t index,
	uint32_t handle, bus_addr_t paddr, uint32_t buf_size)
{
	volatile q80_recv_desc_t *rcv_desc;

	rcv_desc = (q80_recv_desc_t *)ha->hw.dma_buf.rds_ring[r_idx].dma_b;

	rcv_desc += index;

	rcv_desc->handle = (uint16_t)handle;
	rcv_desc->buf_size = buf_size;
	rcv_desc->buf_addr = paddr;

	return;
}

static __inline void
qla_init_hw_rcv_descriptors(qla_host_t *ha)
{
	int i;

	for (i = 0; i < ha->hw.num_rds_rings; i++) 
		bzero((void *)ha->hw.dma_buf.rds_ring[i].dma_b,
			(sizeof(q80_recv_desc_t) * NUM_RX_DESCRIPTORS));
}

#define QLA_LOCK_DEFAULT_MS_TIMEOUT	3000

#ifndef QLA_LOCK_NO_SLEEP
#define QLA_LOCK_NO_SLEEP		0
#endif 

static __inline int
qla_lock(qla_host_t *ha, const char *str, uint32_t timeout_ms,
	uint32_t no_sleep)
{
	int ret = -1;

	while (1) {
		mtx_lock(&ha->hw_lock);

		if (ha->qla_detach_active || ha->offline) {
			mtx_unlock(&ha->hw_lock);
			break;
		}

		if (!ha->hw_lock_held) {
			ha->hw_lock_held = 1;
			ha->qla_lock = str;
			ret = 0;
			mtx_unlock(&ha->hw_lock);
			break;
		}
		mtx_unlock(&ha->hw_lock);

		if (--timeout_ms == 0) {
			ha->hw_lock_failed++;
			break;
		} else {
			if (no_sleep)
				DELAY(1000);
			else
				qla_mdelay(__func__, 1);
		}
	}

//	if (!ha->enable_error_recovery)
//		device_printf(ha->pci_dev, "%s: %s ret = %d\n", __func__,
//			str,ret);

	return (ret);
}

static __inline void
qla_unlock(qla_host_t *ha, const char *str)
{
	mtx_lock(&ha->hw_lock);
	ha->hw_lock_held = 0;
	ha->qla_unlock = str;
	mtx_unlock(&ha->hw_lock);

//	if (!ha->enable_error_recovery)
//		device_printf(ha->pci_dev, "%s: %s\n", __func__, str);

	return;
}

#endif /* #ifndef _QL_INLINE_H_ */
