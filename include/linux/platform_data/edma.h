/*
 *  TI EDMA definitions
 *
 *  Copyright (C) 2006-2013 Texas Instruments.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

/*
 * This EDMA3 programming framework exposes two basic kinds of resource:
 *
 *  Channel	Triggers transfers, usually from a hardware event but
 *		also manually or by "chaining" from DMA completions.
 *		Each channel is coupled to a Parameter RAM (PaRAM) slot.
 *
 *  Slot	Each PaRAM slot holds a DMA transfer descriptor (PaRAM
 *		"set"), source and destination addresses, a link to a
 *		next PaRAM slot (if any), options for the transfer, and
 *		instructions for updating those addresses.  There are
 *		more than twice as many slots as event channels.
 *
 * Each PaRAM set describes a sequence of transfers, either for one large
 * buffer or for several discontiguous smaller buffers.  An EDMA transfer
 * is driven only from a channel, which performs the transfers specified
 * in its PaRAM slot until there are no more transfers.  When that last
 * transfer completes, the "link" field may be used to reload the channel's
 * PaRAM slot with a new transfer descriptor.
 *
 * The EDMA Channel Controller (CC) maps requests from channels into physical
 * Transfer Controller (TC) requests when the channel triggers (by hardware
 * or software events, or by chaining).  The two physical DMA channels provided
 * by the TCs are thus shared by many logical channels.
 *
 * DaVinci hardware also has a "QDMA" mechanism which is not currently
 * supported through this interface.  (DSP firmware uses it though.)
 */

#ifndef EDMA_H_
#define EDMA_H_

/* PaRAM slots are laid out like this */
struct edmacc_param {
	u32 opt;
	u32 src;
	u32 a_b_cnt;
	u32 dst;
	u32 src_dst_bidx;
	u32 link_bcntrld;
	u32 src_dst_cidx;
	u32 ccnt;
} __packed;

/* fields in edmacc_param.opt */
#define SAM		BIT(0)
#define DAM		BIT(1)
#define SYNCDIM		BIT(2)
#define STATIC		BIT(3)
#define EDMA_FWID	(0x07 << 8)
#define TCCMODE		BIT(11)
#define EDMA_TCC(t)	((t) << 12)
#define TCINTEN		BIT(20)
#define ITCINTEN	BIT(21)
#define TCCHEN		BIT(22)
#define ITCCHEN		BIT(23)

/*ch_status paramater of callback function possible values*/
#define EDMA_DMA_COMPLETE 1
#define EDMA_DMA_CC_ERROR 2
#define EDMA_DMA_TC1_ERROR 3
#define EDMA_DMA_TC2_ERROR 4

enum dma_event_q {
	EVENTQ_0 = 0,
	EVENTQ_1 = 1,
	EVENTQ_2 = 2,
	EVENTQ_3 = 3,
	EVENTQ_DEFAULT = -1
};

#define EDMA_CTLR_CHAN(ctlr, chan)	(((ctlr) << 16) | (chan))
#define EDMA_CTLR(i)			((i) >> 16)
#define EDMA_CHAN_SLOT(i)		((i) & 0xffff)

#define EDMA_CHANNEL_ANY		-1	/* for edma_alloc_channel() */
#define EDMA_SLOT_ANY			-1	/* for edma_alloc_slot() */
#define EDMA_CONT_PARAMS_ANY		 1001
#define EDMA_CONT_PARAMS_FIXED_EXACT	 1002
#define EDMA_CONT_PARAMS_FIXED_NOT_EXACT 1003

#define EDMA_MAX_CC               2

struct edma;

struct edma *edma_get_data(struct device *edma_dev);

/* alloc/free DMA channels and their dedicated parameter RAM slots */
int edma_alloc_channel(struct edma *cc, int channel,
	void (*callback)(unsigned channel, u16 ch_status, void *data),
	void *data, enum dma_event_q);
void edma_free_channel(struct edma *cc, unsigned channel);

/* alloc/free parameter RAM slots */
int edma_alloc_slot(struct edma *cc, int slot);
void edma_free_slot(struct edma *cc, unsigned slot);

/* calls that operate on part of a parameter RAM slot */
dma_addr_t edma_get_position(struct edma *cc, unsigned slot, bool dst);
void edma_link(struct edma *cc, unsigned from, unsigned to);

/* calls that operate on an entire parameter RAM slot */
void edma_write_slot(struct edma *cc, unsigned slot,
		     const struct edmacc_param *params);
void edma_read_slot(struct edma *cc, unsigned slot,
		    struct edmacc_param *params);

/* channel control operations */
int edma_start(struct edma *cc, unsigned channel);
void edma_stop(struct edma *cc, unsigned channel);
void edma_clean_channel(struct edma *cc, unsigned channel);
void edma_pause(struct edma *cc, unsigned channel);
void edma_resume(struct edma *cc, unsigned channel);
int edma_trigger_channel(struct edma *cc, unsigned channel);

void edma_assign_channel_eventq(struct edma *cc, unsigned channel,
				enum dma_event_q eventq_no);

struct edma_rsv_info {

	const s16	(*rsv_chans)[2];
	const s16	(*rsv_slots)[2];
};

/* platform_data for EDMA driver */
struct edma_soc_info {
	/*
	 * Default queue is expected to be a low-priority queue.
	 * This way, long transfers on the default queue started
	 * by the codec engine will not cause audio defects.
	 */
	enum dma_event_q	default_queue;

	/* Resource reservation for other cores */
	struct edma_rsv_info	*rsv;

	s8	(*queue_priority_mapping)[2];
	const s16	(*xbar_chans)[2];
};

#endif
