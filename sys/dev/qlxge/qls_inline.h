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
 *
 * $FreeBSD$
 */
/*
 * File: qls_inline.h
 * Author : David C Somayajulu, Qlogic Corporation, Aliso Viejo, CA 92656.
 */
#ifndef _QLS_INLINE_H_
#define _QLS_INLINE_H_

static __inline int
qls_get_ifq_snd_maxlen(qla_host_t *ha)
{
	return((NUM_TX_DESCRIPTORS - 1));
}

static __inline uint32_t
qls_get_optics(qla_host_t *ha)
{
	uint32_t link_speed = 0;

	if (ha->link_up) {
		switch ((ha->link_hw_info & 0xF0)) {
		case (0x01 << 4):
		case (0x02 << 4):
		case (0x03 << 4):
			link_speed = (IFM_10G_LR | IFM_10G_SR);
			break;

		case (0x04 << 4):
		case (0x05 << 4):
		case (0x06 << 4):
			link_speed = IFM_10G_TWINAX;
			break;

		case (0x07 << 4):
		case (0x08 << 4):
		case (0x09 << 4):
		case (0x0A << 4):
		case (0x0B << 4):
			link_speed = IFM_1000_SX;
			break;
		}
	}

	return(link_speed);
}

static __inline uint8_t *
qls_get_mac_addr(qla_host_t *ha)
{
	return (ha->mac_addr);
}

static __inline int
qls_lock(qla_host_t *ha, const char *str, uint32_t no_delay)
{
	int ret = -1;

	while (1) {
		mtx_lock(&ha->hw_lock);
		if (!ha->hw_lock_held) {
			ha->hw_lock_held = 1;
			ha->qla_lock = str;
			ret = 0;
			mtx_unlock(&ha->hw_lock);
			break;
		}
		mtx_unlock(&ha->hw_lock);

		if (no_delay)
			break;
		else
			qls_mdelay(__func__, 1);
	}
	return (ret);
}

static __inline void
qls_unlock(qla_host_t *ha, const char *str)
{
	mtx_lock(&ha->hw_lock);
	ha->hw_lock_held = 0;
	ha->qla_unlock = str;
	mtx_unlock(&ha->hw_lock);
}

#endif /* #ifndef _QLS_INLINE_H_ */
