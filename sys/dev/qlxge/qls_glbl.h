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
 * File: qls_glbl.h
 * Author : David C Somayajulu, Qlogic Corporation, Aliso Viejo, CA 92656.
 * Content: Contains prototypes of the exported functions from each file.
 */
#ifndef _QLS_GLBL_H_
#define _QLS_GLBL_H_

/*
 * from qls_isr.c
 */

extern void qls_isr(void *arg);

/*
 * from qls_os.c
 */

extern int qls_alloc_dmabuf(qla_host_t *ha, qla_dma_t *dma_buf);
extern void qls_free_dmabuf(qla_host_t *ha, qla_dma_t *dma_buf);
extern int qls_get_mbuf(qla_host_t *ha, qla_rx_buf_t *rxb, struct mbuf *nmp);

/*
 * from qls_hw.c
 */

extern int qls_init_host_fw(qla_host_t *ha);
extern int qls_get_msix_count(qla_host_t *ha);

extern void qls_hw_add_sysctls(qla_host_t *ha);

extern void qls_free_dma(qla_host_t *ha);
extern int qls_alloc_dma(qla_host_t *ha);

extern int qls_set_promisc(qla_host_t *ha);
extern void qls_reset_promisc(qla_host_t *ha);
extern int qls_set_allmulti(qla_host_t *ha);
extern void qls_reset_allmulti(qla_host_t *ha);

extern int qls_hw_tx_done(qla_host_t *ha, uint32_t txr_idx);

extern int qls_hw_send(qla_host_t *ha, bus_dma_segment_t *segs, int nsegs,
		uint32_t tx_idx, struct mbuf *mp, uint32_t txr_idx);

extern void qls_del_hw_if(qla_host_t *ha);
extern int qls_init_hw_if(qla_host_t *ha);

extern void qls_hw_set_multi(qla_host_t *ha, uint8_t *mta, uint32_t mcnt,
	uint32_t add_multi);

extern void qls_update_link_state(qla_host_t *ha);

extern int qls_init_hw(qla_host_t *ha);

extern int qls_rd_flash32(qla_host_t *ha, uint32_t addr, uint32_t *data);
extern int qls_rd_nic_params(qla_host_t *ha);

extern int qls_mbx_rd_reg(qla_host_t *ha, uint32_t reg, uint32_t *data);
extern int qls_mbx_wr_reg(qla_host_t *ha, uint32_t reg, uint32_t data);
extern int qls_mpi_risc_rd_reg(qla_host_t *ha, uint32_t reg, uint32_t *data);
extern int qls_mpi_risc_wr_reg(qla_host_t *ha, uint32_t reg, uint32_t data);

extern int qls_mbx_dump_risc_ram(qla_host_t *ha, void *buf, uint32_t r_addr,
		uint32_t r_size);

extern int qls_mpi_reset(qla_host_t *ha);

/*
 * from qls_ioctl.c
 */

extern int qls_make_cdev(qla_host_t *ha);
extern void qls_del_cdev(qla_host_t *ha);

extern int qls_mpi_core_dump(qla_host_t *ha);

#endif /* #ifndef_QLS_GLBL_H_ */
