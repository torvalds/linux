/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * SGI UV Broadcast Assist Unit definitions
 *
 * Copyright (C) 2008 Silicon Graphics, Inc. All rights reserved.
 */

#ifndef ASM_X86__UV__UV_BAU_H
#define ASM_X86__UV__UV_BAU_H

#include <linux/bitmap.h>
#define BITSPERBYTE 8

/*
 * Broadcast Assist Unit messaging structures
 *
 * Selective Broadcast activations are induced by software action
 * specifying a particular 8-descriptor "set" via a 6-bit index written
 * to an MMR.
 * Thus there are 64 unique 512-byte sets of SB descriptors - one set for
 * each 6-bit index value. These descriptor sets are mapped in sequence
 * starting with set 0 located at the address specified in the
 * BAU_SB_DESCRIPTOR_BASE register, set 1 is located at BASE + 512,
 * set 2 is at BASE + 2*512, set 3 at BASE + 3*512, and so on.
 *
 * We will use 31 sets, one for sending BAU messages from each of the 32
 * cpu's on the node.
 *
 * TLB shootdown will use the first of the 8 descriptors of each set.
 * Each of the descriptors is 64 bytes in size (8*64 = 512 bytes in a set).
 */

#define UV_ITEMS_PER_DESCRIPTOR		8
#define UV_CPUS_PER_ACT_STATUS		32
#define UV_ACT_STATUS_MASK		0x3
#define UV_ACT_STATUS_SIZE		2
#define UV_ACTIVATION_DESCRIPTOR_SIZE	32
#define UV_DISTRIBUTION_SIZE		256
#define UV_SW_ACK_NPENDING		8
#define UV_NET_ENDPOINT_INTD		0x38
#define UV_DESC_BASE_PNODE_SHIFT	49
#define UV_PAYLOADQ_PNODE_SHIFT		49
#define UV_PTC_BASENAME			"sgi_uv/ptc_statistics"
#define uv_physnodeaddr(x)		((__pa((unsigned long)(x)) & uv_mmask))

/*
 * bits in UVH_LB_BAU_SB_ACTIVATION_STATUS_0/1
 */
#define DESC_STATUS_IDLE		0
#define DESC_STATUS_ACTIVE		1
#define DESC_STATUS_DESTINATION_TIMEOUT	2
#define DESC_STATUS_SOURCE_TIMEOUT	3

/*
 * source side threshholds at which message retries print a warning
 */
#define SOURCE_TIMEOUT_LIMIT		20
#define DESTINATION_TIMEOUT_LIMIT	20

/*
 * number of entries in the destination side payload queue
 */
#define DEST_Q_SIZE			17
/*
 * number of destination side software ack resources
 */
#define DEST_NUM_RESOURCES		8
#define MAX_CPUS_PER_NODE		32
/*
 * completion statuses for sending a TLB flush message
 */
#define	FLUSH_RETRY			1
#define	FLUSH_GIVEUP			2
#define	FLUSH_COMPLETE			3

/*
 * Distribution: 32 bytes (256 bits) (bytes 0-0x1f of descriptor)
 * If the 'multilevel' flag in the header portion of the descriptor
 * has been set to 0, then endpoint multi-unicast mode is selected.
 * The distribution specification (32 bytes) is interpreted as a 256-bit
 * distribution vector. Adjacent bits correspond to consecutive even numbered
 * nodeIDs. The result of adding the index of a given bit to the 15-bit
 * 'base_dest_nodeid' field of the header corresponds to the
 * destination nodeID associated with that specified bit.
 */
struct bau_target_nodemask {
	unsigned long bits[BITS_TO_LONGS(256)];
};

/*
 * mask of cpu's on a node
 * (during initialization we need to check that unsigned long has
 *  enough bits for max. cpu's per node)
 */
struct bau_local_cpumask {
	unsigned long bits;
};

/*
 * Payload: 16 bytes (128 bits) (bytes 0x20-0x2f of descriptor)
 * only 12 bytes (96 bits) of the payload area are usable.
 * An additional 3 bytes (bits 27:4) of the header address are carried
 * to the next bytes of the destination payload queue.
 * And an additional 2 bytes of the header Suppl_A field are also
 * carried to the destination payload queue.
 * But the first byte of the Suppl_A becomes bits 127:120 (the 16th byte)
 * of the destination payload queue, which is written by the hardware
 * with the s/w ack resource bit vector.
 * [ effective message contents (16 bytes (128 bits) maximum), not counting
 *   the s/w ack bit vector  ]
 */

/*
 * The payload is software-defined for INTD transactions
 */
struct bau_msg_payload {
	unsigned long address;		/* signifies a page or all TLB's
						of the cpu */
	/* 64 bits */
	unsigned short sending_cpu;	/* filled in by sender */
	/* 16 bits */
	unsigned short acknowledge_count;/* filled in by destination */
	/* 16 bits */
	unsigned int reserved1:32;	/* not usable */
};


/*
 * Message header:  16 bytes (128 bits) (bytes 0x30-0x3f of descriptor)
 * see table 4.2.3.0.1 in broacast_assist spec.
 */
struct bau_msg_header {
	int dest_subnodeid:6;	/* must be zero */
	/* bits 5:0 */
	int base_dest_nodeid:15; /* nasid>>1 (pnode) of first bit in node_map */
	/* bits 20:6 */
	int command:8;		/* message type */
	/* bits 28:21 */
				/* 0x38: SN3net EndPoint Message */
	int rsvd_1:3;		/* must be zero */
	/* bits 31:29 */
				/* int will align on 32 bits */
	int rsvd_2:9;		/* must be zero */
	/* bits 40:32 */
				/* Suppl_A is 56-41 */
	int payload_2a:8;	/* becomes byte 16 of msg */
	/* bits 48:41 */	/* not currently using */
	int payload_2b:8;	/* becomes byte 17 of msg */
	/* bits 56:49 */	/* not currently using */
				/* Address field (96:57) is never used as an
				   address (these are address bits 42:3) */
	int rsvd_3:1;		/* must be zero */
	/* bit 57 */
				/* address bits 27:4 are payload */
				/* these 24 bits become bytes 12-14 of msg */
	int replied_to:1;	/* sent as 0 by the source to byte 12 */
	/* bit 58 */

	int payload_1a:5;	/* not currently used */
	/* bits 63:59 */
	int payload_1b:8;	/* not currently used */
	/* bits 71:64 */
	int payload_1c:8;	/* not currently used */
	/* bits 79:72 */
	int payload_1d:2;	/* not currently used */
	/* bits 81:80 */

	int rsvd_4:7;		/* must be zero */
	/* bits 88:82 */
	int sw_ack_flag:1;	/* software acknowledge flag */
	/* bit 89 */
				/* INTD trasactions at destination are to
				   wait for software acknowledge */
	int rsvd_5:6;		/* must be zero */
	/* bits 95:90 */
	int rsvd_6:5;		/* must be zero */
	/* bits 100:96 */
	int int_both:1;		/* if 1, interrupt both sockets on the blade */
	/* bit 101*/
	int fairness:3;		/* usually zero */
	/* bits 104:102 */
	int multilevel:1;	/* multi-level multicast format */
	/* bit 105 */
				/* 0 for TLB: endpoint multi-unicast messages */
	int chaining:1;		/* next descriptor is part of this activation*/
	/* bit 106 */
	int rsvd_7:21;		/* must be zero */
	/* bits 127:107 */
};

/*
 * The activation descriptor:
 * The format of the message to send, plus all accompanying control
 * Should be 64 bytes
 */
struct bau_desc {
	struct bau_target_nodemask distribution;
	/*
	 * message template, consisting of header and payload:
	 */
	struct bau_msg_header header;
	struct bau_msg_payload payload;
};
/*
 *   -payload--    ---------header------
 *   bytes 0-11    bits 41-56  bits 58-81
 *       A           B  (2)      C (3)
 *
 *            A/B/C are moved to:
 *       A            C          B
 *   bytes 0-11  bytes 12-14  bytes 16-17  (byte 15 filled in by hw as vector)
 *   ------------payload queue-----------
 */

/*
 * The payload queue on the destination side is an array of these.
 * With BAU_MISC_CONTROL set for software acknowledge mode, the messages
 * are 32 bytes (2 micropackets) (256 bits) in length, but contain only 17
 * bytes of usable data, including the sw ack vector in byte 15 (bits 127:120)
 * (12 bytes come from bau_msg_payload, 3 from payload_1, 2 from
 *  sw_ack_vector and payload_2)
 * "Enabling Software Acknowledgment mode (see Section 4.3.3 Software
 *  Acknowledge Processing) also selects 32 byte (17 bytes usable) payload
 *  operation."
 */
struct bau_payload_queue_entry {
	unsigned long address;		/* signifies a page or all TLB's
						of the cpu */
	/* 64 bits, bytes 0-7 */

	unsigned short sending_cpu;	/* cpu that sent the message */
	/* 16 bits, bytes 8-9 */

	unsigned short acknowledge_count; /* filled in by destination */
	/* 16 bits, bytes 10-11 */

	unsigned short replied_to:1;	/* sent as 0 by the source */
	/* 1 bit */
	unsigned short unused1:7;       /* not currently using */
	/* 7 bits: byte 12) */

	unsigned char unused2[2];	/* not currently using */
	/* bytes 13-14 */

	unsigned char sw_ack_vector;	/* filled in by the hardware */
	/* byte 15 (bits 127:120) */

	unsigned char unused4[3];	/* not currently using bytes 17-19 */
	/* bytes 17-19 */

	int number_of_cpus;		/* filled in at destination */
	/* 32 bits, bytes 20-23 (aligned) */

	unsigned char unused5[8];       /* not using */
	/* bytes 24-31 */
};

/*
 * one for every slot in the destination payload queue
 */
struct bau_msg_status {
	struct bau_local_cpumask seen_by;	/* map of cpu's */
};

/*
 * one for every slot in the destination software ack resources
 */
struct bau_sw_ack_status {
	struct bau_payload_queue_entry *msg;	/* associated message */
	int watcher;				/* cpu monitoring, or -1 */
};

/*
 * one on every node and per-cpu; to locate the software tables
 */
struct bau_control {
	struct bau_desc *descriptor_base;
	struct bau_payload_queue_entry *bau_msg_head;
	struct bau_payload_queue_entry *va_queue_first;
	struct bau_payload_queue_entry *va_queue_last;
	struct bau_msg_status *msg_statuses;
	int *watching; /* pointer to array */
};

/*
 * This structure is allocated per_cpu for UV TLB shootdown statistics.
 */
struct ptc_stats {
	unsigned long ptc_i;	/* number of IPI-style flushes */
	unsigned long requestor;	/* number of nodes this cpu sent to */
	unsigned long requestee;	/* times cpu was remotely requested */
	unsigned long alltlb;	/* times all tlb's on this cpu were flushed */
	unsigned long onetlb;	/* times just one tlb on this cpu was flushed */
	unsigned long s_retry;	/* retries on source side timeouts */
	unsigned long d_retry;	/* retries on destination side timeouts */
	unsigned long sflush;	/* cycles spent in uv_flush_tlb_others */
	unsigned long dflush;	/* cycles spent on destination side */
	unsigned long retriesok; /* successes on retries */
	unsigned long nomsg;	/* interrupts with no message */
	unsigned long multmsg;	/* interrupts with multiple messages */
	unsigned long ntargeted;/* nodes targeted */
};

static inline int bau_node_isset(int node, struct bau_target_nodemask *dstp)
{
	return constant_test_bit(node, &dstp->bits[0]);
}
static inline void bau_node_set(int node, struct bau_target_nodemask *dstp)
{
	__set_bit(node, &dstp->bits[0]);
}
static inline void bau_nodes_clear(struct bau_target_nodemask *dstp, int nbits)
{
	bitmap_zero(&dstp->bits[0], nbits);
}

static inline void bau_cpubits_clear(struct bau_local_cpumask *dstp, int nbits)
{
	bitmap_zero(&dstp->bits, nbits);
}

#define cpubit_isset(cpu, bau_local_cpumask) \
	test_bit((cpu), (bau_local_cpumask).bits)

extern int uv_flush_tlb_others(cpumask_t *, struct mm_struct *, unsigned long);
extern void uv_bau_message_intr1(void);
extern void uv_bau_timeout_intr1(void);

#endif /* ASM_X86__UV__UV_BAU_H */
