/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011
 *	Ben Gray <ben.r.gray@gmail.com>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/**
 * sDMA device driver interface for the TI SoC
 *
 * See the ti_sdma.c file for implementation details.
 *
 * Reference:
 *  OMAP35x Applications Processor
 *   Technical Reference Manual
 *  (omap35xx_techref.pdf)
 */
#ifndef _TI_DMA_H_
#define _TI_DMA_H_

#define TI_SDMA_ENDIAN_BIG          0x1
#define TI_SDMA_ENDIAN_LITTLE       0x0

#define TI_SDMA_BURST_NONE          0x0
#define TI_SDMA_BURST_16            0x1
#define TI_SDMA_BURST_32            0x2
#define TI_SDMA_BURST_64            0x3

#define TI_SDMA_DATA_8BITS_SCALAR   0x0
#define TI_SDMA_DATA_16BITS_SCALAR  0x1
#define TI_SDMA_DATA_32BITS_SCALAR  0x2

#define TI_SDMA_ADDR_CONSTANT       0x0
#define TI_SDMA_ADDR_POST_INCREMENT 0x1
#define TI_SDMA_ADDR_SINGLE_INDEX   0x2
#define TI_SDMA_ADDR_DOUBLE_INDEX   0x3

/**
 * Status flags for the DMA callback
 *
 */
#define TI_SDMA_STATUS_DROP                  (1UL << 1)
#define TI_SDMA_STATUS_HALF                  (1UL << 2)
#define TI_SDMA_STATUS_FRAME                 (1UL << 3)
#define TI_SDMA_STATUS_LAST                  (1UL << 4)
#define TI_SDMA_STATUS_BLOCK                 (1UL << 5)
#define TI_SDMA_STATUS_SYNC                  (1UL << 6)
#define TI_SDMA_STATUS_PKT                   (1UL << 7)
#define TI_SDMA_STATUS_TRANS_ERR             (1UL << 8)
#define TI_SDMA_STATUS_SECURE_ERR            (1UL << 9)
#define TI_SDMA_STATUS_SUPERVISOR_ERR        (1UL << 10)
#define TI_SDMA_STATUS_MISALIGNED_ADRS_ERR   (1UL << 11)
#define TI_SDMA_STATUS_DRAIN_END             (1UL << 12)

#define TI_SDMA_SYNC_FRAME                   (1UL << 0)
#define TI_SDMA_SYNC_BLOCK                   (1UL << 1)
#define TI_SDMA_SYNC_PACKET                  (TI_SDMA_SYNC_FRAME | TI_SDMA_SYNC_BLOCK)
#define TI_SDMA_SYNC_TRIG_ON_SRC             (1UL << 8)
#define TI_SDMA_SYNC_TRIG_ON_DST             (1UL << 9)

#define TI_SDMA_IRQ_FLAG_DROP                (1UL << 1)
#define TI_SDMA_IRQ_FLAG_HALF_FRAME_COMPL    (1UL << 2)
#define TI_SDMA_IRQ_FLAG_FRAME_COMPL         (1UL << 3)
#define TI_SDMA_IRQ_FLAG_START_LAST_FRAME    (1UL << 4)
#define TI_SDMA_IRQ_FLAG_BLOCK_COMPL         (1UL << 5)
#define TI_SDMA_IRQ_FLAG_ENDOF_PKT           (1UL << 7)
#define TI_SDMA_IRQ_FLAG_DRAIN               (1UL << 12)

int ti_sdma_activate_channel(unsigned int *ch,
    void (*callback)(unsigned int ch, uint32_t status, void *data), void *data);
int ti_sdma_deactivate_channel(unsigned int ch);
int ti_sdma_start_xfer(unsigned int ch, unsigned int src_paddr,
    unsigned long dst_paddr, unsigned int frmcnt, unsigned int elmcnt);
int ti_sdma_start_xfer_packet(unsigned int ch, unsigned int src_paddr,
    unsigned long dst_paddr, unsigned int frmcnt, unsigned int elmcnt, 
    unsigned int pktsize);
int ti_sdma_stop_xfer(unsigned int ch);
int ti_sdma_enable_channel_irq(unsigned int ch, uint32_t flags);
int ti_sdma_disable_channel_irq(unsigned int ch);
int ti_sdma_get_channel_status(unsigned int ch, uint32_t *status);
int ti_sdma_set_xfer_endianess(unsigned int ch, unsigned int src, unsigned int dst);
int ti_sdma_set_xfer_burst(unsigned int ch, unsigned int src, unsigned int dst);
int ti_sdma_set_xfer_data_type(unsigned int ch, unsigned int type);
int ti_sdma_set_callback(unsigned int ch,
    void (*callback)(unsigned int ch, uint32_t status, void *data), void *data);
int ti_sdma_sync_params(unsigned int ch, unsigned int trigger, unsigned int mode);
int ti_sdma_set_addr_mode(unsigned int ch, unsigned int src_mode, unsigned int dst_mode);

#endif /* _TI_SDMA_H_ */
