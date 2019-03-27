/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Daisuke Aoyama <aoyama@peach.ne.jp>
 * Copyright (c) 2013 Oleksandr Tymoshenko <gonzo@bluezbox.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef	_BCM2835_DMA_H_
#define	_BCM2835_DMA_H_

#define	BCM_DMA_BLOCK_SIZE	512

/* DMA0-DMA15 but DMA15 is special */
#define	BCM_DMA_CH_MAX		12

/* request CH for any nubmer */
#define	BCM_DMA_CH_INVALID	(-1)
#define	BCM_DMA_CH_ANY		(-1)

/* Peripheral DREQ Signals (4.2.1.3) */
#define	BCM_DMA_DREQ_NONE	0
#define	BCM_DMA_DREQ_EMMC	11
#define	BCM_DMA_DREQ_SDHOST	13

#define	BCM_DMA_SAME_ADDR	0
#define	BCM_DMA_INC_ADDR	1

#define	BCM_DMA_32BIT		0
#define	BCM_DMA_128BIT		1

int bcm_dma_allocate(int req_ch);
int bcm_dma_free(int ch);
int bcm_dma_setup_intr(int ch, void (*func)(int, void *), void *arg);
int bcm_dma_setup_src(int ch, int dreq, int inc_addr, int width);
int bcm_dma_setup_dst(int ch, int dreq, int inc_addr, int width);
int bcm_dma_start(int ch, vm_paddr_t src, vm_paddr_t dst, int len);
uint32_t bcm_dma_length(int ch);

#endif	/* _BCM2835_DMA_H_ */
