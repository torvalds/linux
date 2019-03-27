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
 * File: ql_glbl.h
 * Author : David C Somayajulu, Qlogic Corporation, Aliso Viejo, CA 92656.
 * Content: Contains prototypes of the exported functions from each file.
 */
#ifndef _QL_GLBL_H_
#define _QL_GLBL_H_

/*
 * from ql_isr.c
 */
extern void ql_mbx_isr(void *arg);
extern void ql_isr(void *arg);
extern uint32_t ql_rcv_isr(qla_host_t *ha, uint32_t sds_idx, uint32_t count);

/*
 * from ql_os.c
 */
extern int ql_alloc_dmabuf(qla_host_t *ha, qla_dma_t *dma_buf);
extern void ql_free_dmabuf(qla_host_t *ha, qla_dma_t *dma_buf);
extern int ql_get_mbuf(qla_host_t *ha, qla_rx_buf_t *rxb, struct mbuf *nmp);
extern void qla_set_error_recovery(qla_host_t *ha);

/*
 * from ql_hw.c
 */
extern int ql_alloc_dma(qla_host_t *ha);
extern void ql_free_dma(qla_host_t *ha);
extern void ql_hw_add_sysctls(qla_host_t *ha);
extern int ql_hw_send(qla_host_t *ha, bus_dma_segment_t *segs, int nsegs,
                uint32_t tx_idx, struct mbuf *mp, uint32_t txr_idx,
		uint32_t iscsi_pdu);
extern void qla_confirm_9kb_enable(qla_host_t *ha);
extern int ql_init_hw_if(qla_host_t *ha);
extern int ql_hw_set_multi(qla_host_t *ha, uint8_t *mta, uint32_t mcnt,
		uint32_t add_multi);
extern void ql_del_hw_if(qla_host_t *ha);
extern int ql_set_promisc(qla_host_t *ha);
extern void qla_reset_promisc(qla_host_t *ha);
extern int ql_set_allmulti(qla_host_t *ha);
extern void qla_reset_allmulti(qla_host_t *ha);
extern void ql_update_link_state(qla_host_t *ha);
extern void ql_hw_tx_done_locked(qla_host_t *ha, uint32_t txr_idx);
extern int ql_set_max_mtu(qla_host_t *ha, uint32_t mtu, uint16_t cntxt_id);
extern void ql_get_stats(qla_host_t *ha);
extern void ql_hw_link_status(qla_host_t *ha);
extern int ql_hw_check_health(qla_host_t *ha);
extern void qla_hw_async_event(qla_host_t *ha);
extern int qla_get_nic_partition(qla_host_t *ha, uint32_t *supports_9kb,
		uint32_t *num_rcvq);
extern int qla_hw_del_all_mcast(qla_host_t *ha);

extern int ql_iscsi_pdu(qla_host_t *ha, struct mbuf *mp);
extern void ql_minidump(qla_host_t *ha);
extern int ql_minidump_init(qla_host_t *ha);

/*
 * from ql_misc.c
 */
extern int ql_init_hw(qla_host_t *ha);
extern int ql_rdwr_indreg32(qla_host_t *ha, uint32_t addr, uint32_t *val,
		uint32_t rd);
extern int ql_rd_flash32(qla_host_t *ha, uint32_t addr, uint32_t *data);
extern int ql_rdwr_offchip_mem(qla_host_t *ha, uint64_t addr,
		q80_offchip_mem_val_t *val, uint32_t rd);
extern void ql_read_mac_addr(qla_host_t *ha);
extern int ql_erase_flash(qla_host_t *ha, uint32_t off, uint32_t size);
extern int ql_wr_flash_buffer(qla_host_t *ha, uint32_t off, uint32_t size,
		void *buf);
extern int ql_stop_sequence(qla_host_t *ha);
extern int ql_start_sequence(qla_host_t *ha, uint16_t index);

/*
 * from ql_ioctl.c
 */
extern int ql_make_cdev(qla_host_t *ha);
extern void ql_del_cdev(qla_host_t *ha);

extern unsigned char ql83xx_firmware[];
extern unsigned int ql83xx_firmware_len;
extern unsigned char ql83xx_bootloader[];
extern unsigned int ql83xx_bootloader_len;
extern unsigned char ql83xx_resetseq[];
extern unsigned int ql83xx_resetseq_len;
extern unsigned char ql83xx_minidump[];
extern unsigned int ql83xx_minidump_len;

extern void ql_alloc_drvr_state_buffer(qla_host_t *ha);
extern void ql_free_drvr_state_buffer(qla_host_t *ha);
extern void ql_capture_drvr_state(qla_host_t *ha);
extern void ql_sp_log(qla_host_t *ha, uint16_t fmtstr_idx, uint16_t num_params,
		uint32_t param0, uint32_t param1, uint32_t param2,
		uint32_t param3, uint32_t param4);
extern void ql_alloc_sp_log_buffer(qla_host_t *ha);
extern void ql_free_sp_log_buffer(qla_host_t *ha);


#endif /* #ifndef_QL_GLBL_H_ */
