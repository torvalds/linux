#ifndef _ACKVEC_H
#define _ACKVEC_H
/*
 *  net/dccp/ackvec.h
 *
 *  An implementation of Ack Vectors for the DCCP protocol
 *  Copyright (c) 2007 University of Aberdeen, Scotland, UK
 *  Copyright (c) 2005 Arnaldo Carvalho de Melo <acme@mandriva.com>
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License version 2 as
 *	published by the Free Software Foundation.
 */

#include <linux/dccp.h>
#include <linux/compiler.h>
#include <linux/list.h>
#include <linux/types.h>

/*
 * Ack Vector buffer space is static, in multiples of %DCCP_SINGLE_OPT_MAXLEN,
 * the maximum size of a single Ack Vector. Setting %DCCPAV_NUM_ACKVECS to 1
 * will be sufficient for most cases of low Ack Ratios, using a value of 2 gives
 * more headroom if Ack Ratio is higher or when the sender acknowledges slowly.
 * The maximum value is bounded by the u16 types for indices and functions.
 */
#define DCCPAV_NUM_ACKVECS	2
#define DCCPAV_MAX_ACKVEC_LEN	(DCCP_SINGLE_OPT_MAXLEN * DCCPAV_NUM_ACKVECS)

/* Estimated minimum average Ack Vector length - used for updating MPS */
#define DCCPAV_MIN_OPTLEN	16

/* Threshold for coping with large bursts of losses */
#define DCCPAV_BURST_THRESH	(DCCPAV_MAX_ACKVEC_LEN / 8)

enum dccp_ackvec_states {
	DCCPAV_RECEIVED =	0x00,
	DCCPAV_ECN_MARKED =	0x40,
	DCCPAV_RESERVED =	0x80,
	DCCPAV_NOT_RECEIVED =	0xC0
};
#define DCCPAV_MAX_RUNLEN	0x3F

static inline u8 dccp_ackvec_runlen(const u8 *cell)
{
	return *cell & DCCPAV_MAX_RUNLEN;
}

static inline u8 dccp_ackvec_state(const u8 *cell)
{
	return *cell & ~DCCPAV_MAX_RUNLEN;
}

/**
 * struct dccp_ackvec - Ack Vector main data structure
 *
 * This implements a fixed-size circular buffer within an array and is largely
 * based on Appendix A of RFC 4340.
 *
 * @av_buf:	   circular buffer storage area
 * @av_buf_head:   head index; begin of live portion in @av_buf
 * @av_buf_tail:   tail index; first index _after_ the live portion in @av_buf
 * @av_buf_ackno:  highest seqno of acknowledgeable packet recorded in @av_buf
 * @av_tail_ackno: lowest  seqno of acknowledgeable packet recorded in @av_buf
 * @av_buf_nonce:  ECN nonce sums, each covering subsequent segments of up to
 *		   %DCCP_SINGLE_OPT_MAXLEN cells in the live portion of @av_buf
 * @av_overflow:   if 1 then buf_head == buf_tail indicates buffer wraparound
 * @av_records:	   list of %dccp_ackvec_record (Ack Vectors sent previously)
 */
struct dccp_ackvec {
	u8			av_buf[DCCPAV_MAX_ACKVEC_LEN];
	u16			av_buf_head;
	u16			av_buf_tail;
	u64			av_buf_ackno:48;
	u64			av_tail_ackno:48;
	bool			av_buf_nonce[DCCPAV_NUM_ACKVECS];
	u8			av_overflow:1;
	struct list_head	av_records;
};

/**
 * struct dccp_ackvec_record - Records information about sent Ack Vectors
 *
 * These list entries define the additional information which the HC-Receiver
 * keeps about recently-sent Ack Vectors; again refer to RFC 4340, Appendix A.
 *
 * @avr_node:	    the list node in @av_records
 * @avr_ack_seqno:  sequence number of the packet the Ack Vector was sent on
 * @avr_ack_ackno:  the Ack number that this record/Ack Vector refers to
 * @avr_ack_ptr:    pointer into @av_buf where this record starts
 * @avr_ack_runlen: run length of @avr_ack_ptr at the time of sending
 * @avr_ack_nonce:  the sum of @av_buf_nonce's at the time this record was sent
 *
 * The list as a whole is sorted in descending order by @avr_ack_seqno.
 */
struct dccp_ackvec_record {
	struct list_head avr_node;
	u64		 avr_ack_seqno:48;
	u64		 avr_ack_ackno:48;
	u16		 avr_ack_ptr;
	u8		 avr_ack_runlen;
	u8		 avr_ack_nonce:1;
};

extern int dccp_ackvec_init(void);
extern void dccp_ackvec_exit(void);

extern struct dccp_ackvec *dccp_ackvec_alloc(const gfp_t priority);
extern void dccp_ackvec_free(struct dccp_ackvec *av);

extern void dccp_ackvec_input(struct dccp_ackvec *av, struct sk_buff *skb);
extern int  dccp_ackvec_update_records(struct dccp_ackvec *av, u64 seq, u8 sum);
extern void dccp_ackvec_clear_state(struct dccp_ackvec *av, const u64 ackno);
extern u16  dccp_ackvec_buflen(const struct dccp_ackvec *av);

static inline bool dccp_ackvec_is_empty(const struct dccp_ackvec *av)
{
	return av->av_overflow == 0 && av->av_buf_head == av->av_buf_tail;
}

/**
 * struct dccp_ackvec_parsed  -  Record offsets of Ack Vectors in skb
 * @vec:	start of vector (offset into skb)
 * @len:	length of @vec
 * @nonce:	whether @vec had an ECN nonce of 0 or 1
 * @node:	FIFO - arranged in descending order of ack_ackno
 *
 * This structure is used by CCIDs to access Ack Vectors in a received skb.
 */
struct dccp_ackvec_parsed {
	u8		 *vec,
			 len,
			 nonce:1;
	struct list_head node;
};

extern int dccp_ackvec_parsed_add(struct list_head *head,
				  u8 *vec, u8 len, u8 nonce);
extern void dccp_ackvec_parsed_cleanup(struct list_head *parsed_chunks);
#endif /* _ACKVEC_H */
