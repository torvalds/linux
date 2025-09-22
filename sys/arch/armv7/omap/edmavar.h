/*	$OpenBSD: edmavar.h,v 1.4 2015/01/22 14:33:01 krw Exp $	*/
/*
 * Copyright (c) 2013 Sylvestre Gallon <ccna.syl@gmail.com>
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

#ifndef __EDMAVAR_H__
#define __EDMAVAR_H__

typedef	void (*edma_intr_cb_t)(void *);

/*
 *	EDMA PaRAM dma descriptors
 */
struct edma_param{
	uint32_t		opt;		/* Option */
	uint32_t		src;		/* Ch source */
	uint16_t		acnt;		/* 1st dim count */
	uint16_t		bcnt;		/* 2nd dim count */
	uint32_t		dst;		/* Chan dst addr */
	int16_t			srcbidx;	/* Src b index */
	int16_t			dstbidx;	/* Dst b index */
	uint16_t		link;		/* Link addr */
	uint16_t		bcntrld;	/* BCNT reload */
	int16_t			srccidx;	/* Source C index */
	int16_t			dstcidx;	/* Dest C index */
	uint16_t		ccnt;		/* 3rd dim count */
	uint16_t		res;		/* Reserved */
} __attribute__((__packed__));

int	edma_intr_dma_en(uint32_t, edma_intr_cb_t, void *); /* en it for chan */
int	edma_intr_dma_dis(uint32_t);		    /* disable intr for chan */
int	edma_trig_xfer_man(uint32_t);		    /* trig a dma xfer */
int	edma_trig_xfer_by_dev(uint32_t);	    /* dma xfer trig by dev */
void	edma_param_write(uint32_t, struct edma_param *);
void	edma_param_read(uint32_t, struct edma_param *);

#endif /* __EDMAVAR_H__ */
