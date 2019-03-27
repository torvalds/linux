/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Damjan Marion <dmarion@Freebsd.org>
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

#ifndef _TI_EDMA3_H_
#define _TI_EDMA3_H_

/* Direct Mapped EDMA3 Events */
#define TI_EDMA3_EVENT_SDTXEVT1			2
#define TI_EDMA3_EVENT_SDRXEVT1			3
#define TI_EDMA3_EVENT_SDTXEVT0			24
#define TI_EDMA3_EVENT_SDRXEVT0			25

struct ti_edma3cc_param_set {
	struct {
		uint32_t sam:1;		/* Source address mode */
		uint32_t dam:1;		/* Destination address mode */
		uint32_t syncdim:1;	/* Transfer synchronization dimension */
		uint32_t static_set:1;	/* Static Set */
		uint32_t :4;
		uint32_t fwid:3;	/* FIFO Width */
		uint32_t tccmode:1;	/* Transfer complete code mode */
		uint32_t tcc:6;		/* Transfer complete code */
		uint32_t :2;
		uint32_t tcinten:1;	/* Transfer complete interrupt enable */
		uint32_t itcinten:1;	/* Intermediate xfer completion intr. ena */
		uint32_t tcchen:1;	/* Transfer complete chaining enable */
		uint32_t itcchen:1;	/* Intermediate xfer completion chaining ena */
		uint32_t privid:4;	/* Privilege identification */
		uint32_t :3;
		uint32_t priv:1;	/* Privilege level */
	} opt;
	uint32_t src;			/* Channel Source Address */
	uint16_t acnt;			/* Count for 1st Dimension */
	uint16_t bcnt;			/* Count for 2nd Dimension */
	uint32_t dst;			/* Channel Destination Address */
	int16_t srcbidx;		/* Source B Index */
	int16_t dstbidx;		/* Destination B Index */
	uint16_t link;			/* Link Address */
	uint16_t bcntrld;		/* BCNT Reload */
	int16_t srccidx;		/* Source C Index */
	int16_t dstcidx;		/* Destination C Index */
	uint16_t ccnt;			/* Count for 3rd Dimension */
	uint16_t reserved;		/* Reserved */
};

void ti_edma3_init(unsigned int eqn);
int ti_edma3_request_dma_ch(unsigned int ch, unsigned int tccn, unsigned int eqn);
int ti_edma3_request_qdma_ch(unsigned int ch, unsigned int tccn, unsigned int eqn);
int ti_edma3_enable_transfer_manual(unsigned int ch);
int ti_edma3_enable_transfer_qdma(unsigned int ch);
int ti_edma3_enable_transfer_event(unsigned int ch);

void ti_edma3_param_write(unsigned int ch, struct ti_edma3cc_param_set *prs);
void ti_edma3_param_read(unsigned int ch, struct ti_edma3cc_param_set *prs);

#endif /* _TI_EDMA3_H_ */
