/*	$OpenBSD: ldcvar.h,v 1.6 2014/09/29 17:43:29 kettenis Exp $	*/
/*
 * Copyright (c) 2009 Mark Kettenis
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

/*
 * LDC queues.
 */

#include <sys/mutex.h>

struct ldc_queue {
	struct mutex	lq_mtx;
	bus_dmamap_t	lq_map;
	bus_dma_segment_t lq_seg;
	caddr_t		lq_va;
	int		lq_nentries;
};

struct ldc_queue *ldc_queue_alloc(bus_dma_tag_t, int);
void	ldc_queue_free(bus_dma_tag_t, struct ldc_queue *);

/*
 * LDC virtual link layer protocol.
 */

#define LDC_VERSION_MAJOR	1
#define LDC_VERSION_MINOR	0

#define LDC_PKT_PAYLOAD		56

struct ldc_pkt {
	uint8_t		type;
	uint8_t		stype;
	uint8_t		ctrl;
	uint8_t		env;
	uint32_t	seqid;

	uint16_t	major;
	uint16_t	minor;
	uint32_t	_reserved[13];
};

/* Packet types. */
#define LDC_CTRL	0x01
#define LDC_DATA	0x02
#define LDC_ERR		0x10

/* Packet subtypes. */
#define LDC_INFO	0x01
#define LDC_ACK		0x02
#define LDC_NACK	0x04

/* Control info values. */
#define LDC_VERS	0x01
#define LDC_RTS		0x02
#define LDC_RTR		0x03
#define LDC_RDX		0x04

/* Packet envelope. */
#define LDC_MODE_RAW		0x00
#define LDC_MODE_UNRELIABLE	0x01
#define LDC_MODE_RELIABLE	0x03

#define LDC_LEN_MASK	0x3f
#define LDC_FRAG_MASK	0xc0
#define LDC_FRAG_START	0x40
#define LDC_FRAG_STOP	0x80

/*
 * XXX Get rid of the +8 once we no longer need to store the header of
 * the first packet.
 */
#define LDC_MSG_MAX	(128 + 8)

struct ldc_conn {
	uint64_t	lc_id;

	struct ldc_queue *lc_txq;
	struct ldc_queue *lc_rxq;
	uint64_t	lc_tx_state;
	uint64_t	lc_rx_state;

	uint32_t	lc_tx_seqid;
	uint8_t		lc_state;
#define LDC_SND_VERS	1
#define LDC_RCV_VERS	2
#define LDC_SND_RTS	3
#define LDC_SND_RTR	4
#define LDC_SND_RDX	5

	uint64_t	lc_msg[LDC_MSG_MAX / 8];
	size_t		lc_len;

	void		*lc_sc;
	void		(*lc_reset)(struct ldc_conn *);
	void		(*lc_start)(struct ldc_conn *);
	void		(*lc_rx_data)(struct ldc_conn *, struct ldc_pkt *);
};

void	ldc_rx_ctrl(struct ldc_conn *, struct ldc_pkt *);
void	ldc_rx_data(struct ldc_conn *, struct ldc_pkt *);

void	ldc_send_vers(struct ldc_conn *);
int	ldc_send_unreliable(struct ldc_conn *, void *, size_t);

void	ldc_reset(struct ldc_conn *);

/*
 * LDC map tables.
 */

struct ldc_map_slot {
	uint64_t	entry;
	uint64_t	cookie;
};

#define LDC_MTE_R	0x0000000000000010ULL
#define LDC_MTE_W	0x0000000000000020ULL
#define LDC_MTE_X	0x0000000000000040ULL
#define LDC_MTE_IOR	0x0000000000000080ULL
#define LDC_MTE_IOW	0x0000000000000100ULL
#define LDC_MTE_CPR	0x0000000000000200ULL
#define LDC_MTE_CPW	0x0000000000000400ULL
#define LDC_MTE_RA_MASK	0x007fffffffffe000ULL

struct ldc_map {
	bus_dmamap_t		lm_map;
	bus_dma_segment_t	lm_seg;
	struct ldc_map_slot	*lm_slot;
	int			lm_nentries;
	int			lm_next;
	int			lm_count;
};

struct ldc_map *ldc_map_alloc(bus_dma_tag_t, int);
void	ldc_map_free(bus_dma_tag_t, struct ldc_map *);

struct ldc_cookie {
	uint64_t	addr;
	uint64_t	size;
};
