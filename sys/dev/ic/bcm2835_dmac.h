/*     $OpenBSD: bcm2835_dmac.h,v 1.1 2020/04/21 18:56:54 tobhe Exp $ */

/*
 * Copyright (c) 2020 Tobias Heider <tobhe@openbsd.org>
 * Copyright (c) 2019 Neil Ashford <ashfordneil0@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*-
 * Copyright (c) 2014 Jared D. McNeill <jmcneill@invisible.ca>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef BCM2835_DMAC_H
#define BCM2835_DMAC_H

#define DMAC_CS(n)		(0x00 + (0x100 * (n)))
#define  DMAC_CS_RESET		(1<<31)
#define  DMAC_CS_ABORT		(1<<30)
#define  DMAC_CS_DISDEBUG	(1<<29)
#define  DMAC_CS_WAIT_FOR_OUTSTANDING_WRITES (1<<28)
#define  DMAC_CS_PANIC_PRIORITY	(((1<<24) - 1) ^ (1<<20))
#define  DMAC_CS_PRIORITY	(((1<<20) - 1) ^ (1<<16))
#define  DMAC_CS_ERROR		(1<<8)
#define  DMAC_CS_WAITING_FOR_OUTSTANDING_WRITES (1<<6)
#define  DMAC_CS_DREQ_STOPS_DMA	(1<<5)
#define  DMAC_CS_PAUSED		(1<<4)
#define  DMAC_CS_DREQ		(1<<3)
#define  DMAC_CS_INT		(1<<2)
#define  DMAC_CS_END		(1<<1)
#define  DMAC_CS_ACTIVE		(1<<0)
#define  DMAC_CS_INTMASK	(DMAC_CS_INT|DMAC_CS_END)
#define DMAC_CONBLK_AD(n)	(0x04 + (0x100 * (n)))
#define DMAC_TI(n)		(0x08 + (0x100 * (n)))
#define DMAC_SOURCE_AD(n)	(0x0c + (0x100 * (n)))
#define DMAC_DEST_AD(n)		(0x10 + (0x100 * (n)))
#define DMAC_TXFR_LEN(n)	(0x14 + (0x100 * (n)))
#define DMAC_STRIDE(n)		(0x18 + (0x100 * (n)))
#define DMAC_NEXTCONBK(n)	(0x1c + (0x100 * (n)))
#define DMAC_DEBUG(n)		(0x20 + (0x100 * (n)))
#define  DMAC_DEBUG_LITE	(1<<28)
#define  DMAC_DEBUG_VERSION	(((1<<28) - 1) ^ (1<<25))
#define  DMAC_DEBUG_DMA_STATE	(((1<<25) - 1) ^ (1<<16))
#define  DMAC_DEBUG_DMA_ID	(((1<<16) - 1) ^ (1<<8))
#define  DMAC_DEBUG_OUTSTANDING_WRITES (((1<<8) - 1) ^ (1<<4))
#define  DMAC_DEBUG_READ_ERROR	(1<<2)
#define  DMAC_DEBUG_FIFO_ERROR	(1<<1)
#define  DMAC_DEBUG_READ_LAST_NOT_SET_ERROR (1<<0)

struct bcmdmac_conblk {
	uint32_t cb_ti;
#define DMAC_TI_NO_WIDE_BURSTS (1 << 26)
#define DMAC_TI_WAITS (((1 << 26) - 1) ^ (1 << 21))
#define DMAC_TI_PERMAP (((1 << 21) - 1) ^ (1 << 16))
#define  DMAC_TI_PERMAP_BASE (1 << 16)
#define DMAC_TI_BURST_LENGTH (((1 << 16) - 1) ^ (1 << 12))
#define DMAC_TI_SRC_IGNORE (1 << 11)
#define DMAC_TI_SRC_DREQ (1 << 10)
#define DMAC_TI_SRC_WIDTH (1 << 9)
#define DMAC_TI_SRC_INC (1 << 8)
#define DMAC_TI_DEST_IGNORE (1 << 7)
#define DMAC_TI_DEST_DREQ (1 << 6)
#define DMAC_TI_DEST_WIDTH (1 << 5)
#define DMAC_TI_DEST_INC (1 << 4)
#define DMAC_TI_WAIT_RESP (1 << 3)
#define DMAC_TI_TDMODE (1 << 1)
#define DMAC_TI_INTEN (1 << 0)
	uint32_t cb_source_ad;
	uint32_t cb_dest_ad;
	uint32_t cb_txfr_len;
#define DMAC_TXFR_LEN_YLENGTH (((1 << 30) - 1) ^ (1 << 16))
#define DMAC_TXFR_LEN_XLENGTH (((1 << 16) - 1) ^ (1 << 0))
	uint32_t cb_stride;
#define DMAC_STRIDE_D_STRIDE (((1 << 32) - 1) ^ (1 << 16))
#define DMAC_STRIDE_S_STRIDE (((1 << 16) - 1) ^ (1 << 0))
	uint32_t cb_nextconbk;
	uint32_t cb_padding[2];
} __packed;

#define DMAC_INT_STATUS 0xfe0
#define DMAC_ENABLE 0xff0

enum bcmdmac_type { BCMDMAC_TYPE_NORMAL, BCMDMAC_TYPE_LITE };

struct bcmdmac_channel;

struct bcmdmac_channel *bcmdmac_alloc(enum bcmdmac_type, int,
				  void (*)(uint32_t, uint32_t, void *), void *);
void bcmdmac_free(struct bcmdmac_channel *);
void bcmdmac_set_conblk_addr(struct bcmdmac_channel *, bus_addr_t);
int bcmdmac_transfer(struct bcmdmac_channel *);
void bcmdmac_halt(struct bcmdmac_channel *);

#endif /* BCM2835_DMAC_H */
