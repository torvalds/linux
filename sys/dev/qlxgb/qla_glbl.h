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
 * File: qla_glbl.h
 * Author : David C Somayajulu, Qlogic Corporation, Aliso Viejo, CA 92656.
 * Content: Contains prototypes of the exported functions from each file.
 */
#ifndef _QLA_GLBL_H_
#define _QLA_GLBL_H_

/*
 * from qla_isr.c
 */
extern void qla_isr(void *arg);
extern void qla_rcv(void *context, int pending);

/*
 * from qla_os.c
 */
extern uint32_t std_replenish;
extern uint32_t jumbo_replenish;
extern uint32_t rcv_pkt_thres;
extern uint32_t rcv_pkt_thres_d;
extern uint32_t snd_pkt_thres;
extern uint32_t free_pkt_thres;

extern int qla_alloc_dmabuf(qla_host_t *ha, qla_dma_t *dma_buf);
extern void qla_free_dmabuf(qla_host_t *ha, qla_dma_t *dma_buf);
extern void qla_start(struct ifnet *ifp);
extern int qla_get_mbuf(qla_host_t *ha, qla_rx_buf_t *rxb, struct mbuf *nmp,
	        uint32_t jumbo);

/*
 * from qla_hw.c
 */
extern int qla_get_msix_count(qla_host_t *ha);
extern int qla_alloc_dma(qla_host_t *ha);
extern void qla_free_dma(qla_host_t *ha);
extern void qla_hw_add_sysctls(qla_host_t *ha);
extern int qla_hw_send(qla_host_t *ha, bus_dma_segment_t *segs, int nsegs,
		uint32_t *tx_idx, struct mbuf *mp);
extern int qla_init_hw_if(qla_host_t *ha);
extern void qla_get_hw_caps(qla_host_t *ha);
extern void qla_hw_set_multi(qla_host_t *ha, uint8_t *mta, uint32_t mcnt,
	uint32_t add_multi);
extern void qla_del_hw_if(qla_host_t *ha);
extern void qla_set_promisc(qla_host_t *ha);
extern void qla_set_allmulti(qla_host_t *ha);
extern void qla_reset_promisc_allmulti(qla_host_t *ha);
extern void qla_config_ipv4_addr(qla_host_t *ha, uint32_t ipv4_addr);
extern int qla_hw_tx_compl(qla_host_t *ha);
extern void qla_update_link_state(qla_host_t *ha);
extern void qla_hw_tx_done(qla_host_t *ha);
extern int qla_config_lro(qla_host_t *ha);
extern void qla_free_lro(qla_host_t *ha);
extern int qla_set_max_mtu(qla_host_t *ha, uint32_t mtu, uint16_t cntxt_id);
extern void qla_hw_stop_rcv(qla_host_t *ha);

/*
 * from qla_misc.c
 */
extern int qla_init_hw(qla_host_t *ha);
extern int qla_rdwr_indreg32(qla_host_t *ha, uint32_t addr, uint32_t *val,
		uint32_t rd);
extern int qla_rd_flash32(qla_host_t *ha, uint32_t addr, uint32_t *data);
extern int qla_flash_rd32_words(qla_host_t *ha, uint32_t addr,
		uint32_t *val, uint32_t num);
extern int qla_flash_rd32(qla_host_t *ha, uint32_t addr, uint32_t *val);
extern int qla_fw_update(qla_host_t *ha, void *fdata, uint32_t off,
		uint32_t size);
extern int qla_erase_flash(qla_host_t *ha, uint32_t off, uint32_t size);
extern int qla_wr_flash_buffer(qla_host_t *ha, uint32_t off, uint32_t size,
		void *buf, uint32_t pattern);

/*
 * from qla_ioctl.c
 */
extern int qla_make_cdev(qla_host_t *ha);
extern void qla_del_cdev(qla_host_t *ha);
extern int qla_eioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
		struct thread *td);

#endif /* #ifndef_QLA_GLBL_H_ */
