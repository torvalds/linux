/*-
 * Copyright (c) 2017-2018 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-10-C-0237
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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

/* Altera mSGDMA registers. */
#define	DMA_STATUS		0x00
#define	 STATUS_RESETTING	(1 << 6)
#define	DMA_CONTROL		0x04
#define	 CONTROL_GIEM		(1 << 4) /* Global Interrupt Enable Mask */
#define	 CONTROL_RESET		(1 << 1) /* Reset Dispatcher */

/* Descriptor fields. */
#define	CONTROL_GO		(1 << 31)	/* Commit all the descriptor info */
#define	CONTROL_OWN		(1 << 30)	/* Owned by hardware (prefetcher-enabled only) */
#define	CONTROL_EDE		(1 << 24)	/* Early done enable */
#define	CONTROL_ERR_S		16		/* Transmit Error, Error IRQ Enable */
#define	CONTROL_ERR_M		(0xff << CONTROL_ERR_S)
#define	CONTROL_ET_IRQ_EN	(1 << 15)	/* Early Termination IRQ Enable */
#define	CONTROL_TC_IRQ_EN	(1 << 14)	/* Transfer Complete IRQ Enable */
#define	CONTROL_END_ON_EOP	(1 << 12)	/* End on EOP */
#define	CONTROL_PARK_WR		(1 << 11)	/* Park Writes */
#define	CONTROL_PARK_RD		(1 << 10)	/* Park Reads */
#define	CONTROL_GEN_EOP		(1 << 9)	/* Generate EOP */
#define	CONTROL_GEN_SOP		(1 << 8)	/* Generate SOP */
#define	CONTROL_TX_CHANNEL_S	0		/* Transmit Channel */
#define	CONTROL_TX_CHANNEL_M	(0xff << CONTROL_TRANSMIT_CH_S)

/* Prefetcher */
#define	PF_CONTROL			0x00
#define	 PF_CONTROL_GIEM		(1 << 3)
#define	 PF_CONTROL_RESET		(1 << 2)
#define	 PF_CONTROL_DESC_POLL_EN	(1 << 1)
#define	 PF_CONTROL_RUN			(1 << 0)
#define	PF_NEXT_LO			0x04
#define	PF_NEXT_HI			0x08
#define	PF_POLL_FREQ			0x0C
#define	PF_STATUS			0x10
#define	 PF_STATUS_IRQ			(1 << 0)

#define	READ4(_sc, _reg)	\
	le32toh(bus_space_read_4(_sc->bst, _sc->bsh, _reg))
#define	WRITE4(_sc, _reg, _val)	\
	bus_space_write_4(_sc->bst, _sc->bsh, _reg, htole32(_val))

#define	READ4_DESC(_sc, _reg)	\
	le32toh(bus_space_read_4(_sc->bst_d, _sc->bsh_d, _reg))
#define	WRITE4_DESC(_sc, _reg, _val)	\
	bus_space_write_4(_sc->bst_d, _sc->bsh_d, _reg, htole32(_val))

/* Prefetcher-disabled descriptor format. */
struct msgdma_desc_nonpf {
	uint32_t src_addr;
	uint32_t dst_addr;
	uint32_t length;
	uint32_t control;
};

/* Prefetcher-enabled descriptor format. */
struct msgdma_desc {
	uint32_t read_lo;
	uint32_t write_lo;
	uint32_t length;
	uint32_t next;
	uint32_t transferred;
	uint32_t status;
	uint32_t reserved;
	uint32_t control;
};
