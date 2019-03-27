/*
 * Copyright (C) 2015 Cavium Inc.
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
 *
 */

#ifndef NICVF_QUEUES_H
#define	NICVF_QUEUES_H

#include "q_struct.h"

#define	MAX_QUEUE_SET			128
#define	MAX_RCV_QUEUES_PER_QS		8
#define	MAX_RCV_BUF_DESC_RINGS_PER_QS	2
#define	MAX_SND_QUEUES_PER_QS		8
#define	MAX_CMP_QUEUES_PER_QS		8

/* VF's queue interrupt ranges */
#define	NICVF_INTR_ID_CQ		0
#define	NICVF_INTR_ID_SQ		8
#define	NICVF_INTR_ID_RBDR		16
#define	NICVF_INTR_ID_MISC		18
#define	NICVF_INTR_ID_QS_ERR		19

#define	for_each_cq_irq(irq)	\
	for ((irq) = NICVF_INTR_ID_CQ; (irq) < NICVF_INTR_ID_SQ; (irq)++)
#define	for_each_sq_irq(irq)	\
	for ((irq) = NICVF_INTR_ID_SQ; (irq) < NICVF_INTR_ID_RBDR; (irq)++)
#define	for_each_rbdr_irq(irq)	\
	for ((irq) = NICVF_INTR_ID_RBDR; (irq) < NICVF_INTR_ID_MISC; (irq)++)

#define	RBDR_SIZE0		0UL /* 8K entries */
#define	RBDR_SIZE1		1UL /* 16K entries */
#define	RBDR_SIZE2		2UL /* 32K entries */
#define	RBDR_SIZE3		3UL /* 64K entries */
#define	RBDR_SIZE4		4UL /* 126K entries */
#define	RBDR_SIZE5		5UL /* 256K entries */
#define	RBDR_SIZE6		6UL /* 512K entries */

#define	SND_QUEUE_SIZE0		0UL /* 1K entries */
#define	SND_QUEUE_SIZE1		1UL /* 2K entries */
#define	SND_QUEUE_SIZE2		2UL /* 4K entries */
#define	SND_QUEUE_SIZE3		3UL /* 8K entries */
#define	SND_QUEUE_SIZE4		4UL /* 16K entries */
#define	SND_QUEUE_SIZE5		5UL /* 32K entries */
#define	SND_QUEUE_SIZE6		6UL /* 64K entries */

#define	CMP_QUEUE_SIZE0		0UL /* 1K entries */
#define	CMP_QUEUE_SIZE1		1UL /* 2K entries */
#define	CMP_QUEUE_SIZE2		2UL /* 4K entries */
#define	CMP_QUEUE_SIZE3		3UL /* 8K entries */
#define	CMP_QUEUE_SIZE4		4UL /* 16K entries */
#define	CMP_QUEUE_SIZE5		5UL /* 32K entries */
#define	CMP_QUEUE_SIZE6		6UL /* 64K entries */

/* Default queue count per QS, its lengths and threshold values */
#define	RBDR_CNT		1
#define	RCV_QUEUE_CNT		8
#define	SND_QUEUE_CNT		8
#define	CMP_QUEUE_CNT		8 /* Max of RCV and SND qcount */

#define	SND_QSIZE		SND_QUEUE_SIZE2
#define	SND_QUEUE_LEN		(1UL << (SND_QSIZE + 10))
#define	MAX_SND_QUEUE_LEN	(1UL << (SND_QUEUE_SIZE6 + 10))
#define	SND_QUEUE_THRESH	2UL
#define	MIN_SQ_DESC_PER_PKT_XMIT	2
/* Since timestamp not enabled, otherwise 2 */
#define	MAX_CQE_PER_PKT_XMIT		1

/*
 * Keep CQ and SQ sizes same, if timestamping
 * is enabled this equation will change.
 */
#define	CMP_QSIZE		CMP_QUEUE_SIZE2
#define	CMP_QUEUE_LEN		(1UL << (CMP_QSIZE + 10))
#define	CMP_QUEUE_CQE_THRESH	32
#define	CMP_QUEUE_TIMER_THRESH	220 /* 10usec */

#define	RBDR_SIZE		RBDR_SIZE0
#define	RCV_BUF_COUNT		(1UL << (RBDR_SIZE + 13))
#define	MAX_RCV_BUF_COUNT	(1UL << (RBDR_SIZE6 + 13))
#define	RBDR_THRESH		(RCV_BUF_COUNT / 2)
#define	DMA_BUFFER_LEN		2048 /* In multiples of 128bytes */

#define	MAX_CQES_FOR_TX		\
    ((SND_QUEUE_LEN / MIN_SQ_DESC_PER_PKT_XMIT) * MAX_CQE_PER_PKT_XMIT)
/* Calculate number of CQEs to reserve for all SQEs.
 * Its 1/256th level of CQ size.
 * '+ 1' to account for pipelining
 */
#define	RQ_CQ_DROP		\
    ((256 / (CMP_QUEUE_LEN / (CMP_QUEUE_LEN - MAX_CQES_FOR_TX))) + 1)

/* Descriptor size in bytes */
#define	SND_QUEUE_DESC_SIZE	16
#define	CMP_QUEUE_DESC_SIZE	512

/* Buffer / descriptor alignments */
#define	NICVF_RCV_BUF_ALIGN		7
#define	NICVF_RCV_BUF_ALIGN_BYTES	(1UL << NICVF_RCV_BUF_ALIGN)
#define	NICVF_CQ_BASE_ALIGN_BYTES	512  /* 9 bits */
#define	NICVF_SQ_BASE_ALIGN_BYTES	128  /* 7 bits */

#define	NICVF_ALIGNED_ADDR(addr, align_bytes)	\
    roundup2((addr), (align_bytes))
#define	NICVF_ADDR_ALIGN_LEN(addr, bytes)	\
    (NICVF_ALIGNED_ADDR((addr), (bytes)) - (bytes))
#define	NICVF_RCV_BUF_ALIGN_LEN(addr)		\
    (NICVF_ALIGNED_ADDR((addr), NICVF_RCV_BUF_ALIGN_BYTES) - (addr))

#define	NICVF_TXBUF_MAXSIZE	NIC_HW_MAX_FRS	/* Total max payload without TSO */
#define	NICVF_TXBUF_NSEGS	256	/* Single command is at most 256 buffers
					   (hdr + 255 subcmds) */
/* TSO-related definitions */
#define	NICVF_TSO_MAXSIZE	IP_MAXPACKET
#define	NICVF_TSO_NSEGS		NICVF_TXBUF_NSEGS
#define	NICVF_TSO_HEADER_SIZE	128

/* Queue enable/disable */
#define	NICVF_SQ_EN		(1UL << 19)

/* Queue reset */
#define	NICVF_CQ_RESET		(1UL << 41)
#define	NICVF_SQ_RESET		(1UL << 17)
#define	NICVF_RBDR_RESET	(1UL << 43)

enum CQ_RX_ERRLVL_E {
	CQ_ERRLVL_MAC,
	CQ_ERRLVL_L2,
	CQ_ERRLVL_L3,
	CQ_ERRLVL_L4,
};

enum CQ_RX_ERROP_E {
	CQ_RX_ERROP_RE_NONE = 0x0,
	CQ_RX_ERROP_RE_PARTIAL = 0x1,
	CQ_RX_ERROP_RE_JABBER = 0x2,
	CQ_RX_ERROP_RE_FCS = 0x7,
	CQ_RX_ERROP_RE_TERMINATE = 0x9,
	CQ_RX_ERROP_RE_RX_CTL = 0xb,
	CQ_RX_ERROP_PREL2_ERR = 0x1f,
	CQ_RX_ERROP_L2_FRAGMENT = 0x20,
	CQ_RX_ERROP_L2_OVERRUN = 0x21,
	CQ_RX_ERROP_L2_PFCS = 0x22,
	CQ_RX_ERROP_L2_PUNY = 0x23,
	CQ_RX_ERROP_L2_MAL = 0x24,
	CQ_RX_ERROP_L2_OVERSIZE = 0x25,
	CQ_RX_ERROP_L2_UNDERSIZE = 0x26,
	CQ_RX_ERROP_L2_LENMISM = 0x27,
	CQ_RX_ERROP_L2_PCLP = 0x28,
	CQ_RX_ERROP_IP_NOT = 0x41,
	CQ_RX_ERROP_IP_CSUM_ERR = 0x42,
	CQ_RX_ERROP_IP_MAL = 0x43,
	CQ_RX_ERROP_IP_MALD = 0x44,
	CQ_RX_ERROP_IP_HOP = 0x45,
	CQ_RX_ERROP_L3_ICRC = 0x46,
	CQ_RX_ERROP_L3_PCLP = 0x47,
	CQ_RX_ERROP_L4_MAL = 0x61,
	CQ_RX_ERROP_L4_CHK = 0x62,
	CQ_RX_ERROP_UDP_LEN = 0x63,
	CQ_RX_ERROP_L4_PORT = 0x64,
	CQ_RX_ERROP_TCP_FLAG = 0x65,
	CQ_RX_ERROP_TCP_OFFSET = 0x66,
	CQ_RX_ERROP_L4_PCLP = 0x67,
	CQ_RX_ERROP_RBDR_TRUNC = 0x70,
};

enum CQ_TX_ERROP_E {
	CQ_TX_ERROP_GOOD = 0x0,
	CQ_TX_ERROP_DESC_FAULT = 0x10,
	CQ_TX_ERROP_HDR_CONS_ERR = 0x11,
	CQ_TX_ERROP_SUBDC_ERR = 0x12,
	CQ_TX_ERROP_IMM_SIZE_OFLOW = 0x80,
	CQ_TX_ERROP_DATA_SEQUENCE_ERR = 0x81,
	CQ_TX_ERROP_MEM_SEQUENCE_ERR = 0x82,
	CQ_TX_ERROP_LOCK_VIOL = 0x83,
	CQ_TX_ERROP_DATA_FAULT = 0x84,
	CQ_TX_ERROP_TSTMP_CONFLICT = 0x85,
	CQ_TX_ERROP_TSTMP_TIMEOUT = 0x86,
	CQ_TX_ERROP_MEM_FAULT = 0x87,
	CQ_TX_ERROP_CK_OVERLAP = 0x88,
	CQ_TX_ERROP_CK_OFLOW = 0x89,
	CQ_TX_ERROP_ENUM_LAST = 0x8a,
};

struct cmp_queue_stats {
	struct tx_stats {
		uint64_t good;
		uint64_t desc_fault;
		uint64_t hdr_cons_err;
		uint64_t subdesc_err;
		uint64_t imm_size_oflow;
		uint64_t data_seq_err;
		uint64_t mem_seq_err;
		uint64_t lock_viol;
		uint64_t data_fault;
		uint64_t tstmp_conflict;
		uint64_t tstmp_timeout;
		uint64_t mem_fault;
		uint64_t csum_overlap;
		uint64_t csum_overflow;
	} tx;
} __aligned(CACHE_LINE_SIZE);

enum RQ_SQ_STATS {
	RQ_SQ_STATS_OCTS,
	RQ_SQ_STATS_PKTS,
};

struct rx_tx_queue_stats {
	uint64_t	bytes;
	uint64_t	pkts;
} __aligned(CACHE_LINE_SIZE);

struct q_desc_mem {
	bus_dma_tag_t	dmat;
	bus_dmamap_t	dmap;
	void		*base;
	bus_addr_t	phys_base;
	uint64_t	size;
	uint16_t	q_len;
};

struct rbdr {
	boolean_t		enable;
	uint32_t		dma_size;
	uint32_t		frag_len;
	uint32_t		thresh;		/* Threshold level for interrupt */
	void			*desc;
	uint32_t		head;
	uint32_t		tail;
	struct q_desc_mem	dmem;

	struct nicvf		*nic;
	int			idx;

	struct task		rbdr_task;
	struct task		rbdr_task_nowait;
	struct taskqueue	*rbdr_taskq;

	bus_dma_tag_t		rbdr_buff_dmat;
	bus_dmamap_t		*rbdr_buff_dmaps;
} __aligned(CACHE_LINE_SIZE);

struct rcv_queue {
	boolean_t	enable;
	struct	rbdr	*rbdr_start;
	struct	rbdr	*rbdr_cont;
	boolean_t	en_tcp_reassembly;
	uint8_t		cq_qs;  /* CQ's QS to which this RQ is assigned */
	uint8_t		cq_idx; /* CQ index (0 to 7) in the QS */
	uint8_t		cont_rbdr_qs;      /* Continue buffer ptrs - QS num */
	uint8_t		cont_qs_rbdr_idx;  /* RBDR idx in the cont QS */
	uint8_t		start_rbdr_qs;     /* First buffer ptrs - QS num */
	uint8_t		start_qs_rbdr_idx; /* RBDR idx in the above QS */
	uint8_t		caching;
	struct		rx_tx_queue_stats stats;

	boolean_t	lro_enabled;
	struct lro_ctrl	lro;
} __aligned(CACHE_LINE_SIZE);

struct cmp_queue {
	boolean_t		enable;
	uint16_t		thresh;

	struct nicvf		*nic;
	int			idx;	/* This queue index */

	struct buf_ring		*rx_br;	/* Reception buf ring */
	struct mtx		mtx;	/* lock to serialize processing CQEs */
	char			mtx_name[32];

	struct task		cmp_task;
	struct taskqueue	*cmp_taskq;
	u_int			cmp_cpuid; /* CPU to which bind the CQ task */

	void			*desc;
	struct q_desc_mem	dmem;
	struct cmp_queue_stats	stats;
	int			irq;
} __aligned(CACHE_LINE_SIZE);

struct snd_buff {
	bus_dmamap_t	dmap;
	struct mbuf	*mbuf;
};

struct snd_queue {
	boolean_t		enable;
	uint8_t			cq_qs;  /* CQ's QS to which this SQ is pointing */
	uint8_t			cq_idx; /* CQ index (0 to 7) in the above QS */
	uint16_t		thresh;
	volatile int		free_cnt;
	uint32_t		head;
	uint32_t		tail;
	uint64_t		*skbuff;
	void			*desc;

	struct nicvf		*nic;
	int			idx;	/* This queue index */

	bus_dma_tag_t		snd_buff_dmat;
	struct snd_buff		*snd_buff;

	struct buf_ring		*br;	/* Transmission buf ring */
	struct mtx		mtx;
	char			mtx_name[32];

	struct task		snd_task;
	struct taskqueue	*snd_taskq;

	struct q_desc_mem	dmem;
	struct rx_tx_queue_stats stats;
} __aligned(CACHE_LINE_SIZE);

struct queue_set {
	boolean_t	enable;
	boolean_t	be_en;
	uint8_t		vnic_id;
	uint8_t		rq_cnt;
	uint8_t		cq_cnt;
	uint64_t	cq_len;
	uint8_t		sq_cnt;
	uint64_t	sq_len;
	uint8_t		rbdr_cnt;
	uint64_t	rbdr_len;
	struct	rcv_queue	rq[MAX_RCV_QUEUES_PER_QS];
	struct	cmp_queue	cq[MAX_CMP_QUEUES_PER_QS];
	struct	snd_queue	sq[MAX_SND_QUEUES_PER_QS];
	struct	rbdr		rbdr[MAX_RCV_BUF_DESC_RINGS_PER_QS];

	struct task		qs_err_task;
	struct taskqueue	*qs_err_taskq;
} __aligned(CACHE_LINE_SIZE);

#define	GET_RBDR_DESC(RING, idx)				\
    (&(((struct rbdr_entry_t *)((RING)->desc))[(idx)]))
#define	GET_SQ_DESC(RING, idx)					\
    (&(((struct sq_hdr_subdesc *)((RING)->desc))[(idx)]))
#define	GET_CQ_DESC(RING, idx)					\
    (&(((union cq_desc_t *)((RING)->desc))[(idx)]))

/* CQ status bits */
#define	CQ_WR_FUL	(1UL << 26)
#define	CQ_WR_DISABLE	(1UL << 25)
#define	CQ_WR_FAULT	(1UL << 24)
#define	CQ_CQE_COUNT	(0xFFFF << 0)

#define	CQ_ERR_MASK	(CQ_WR_FUL | CQ_WR_DISABLE | CQ_WR_FAULT)

#define	NICVF_TX_LOCK(sq)		mtx_lock(&(sq)->mtx)
#define	NICVF_TX_TRYLOCK(sq)		mtx_trylock(&(sq)->mtx)
#define	NICVF_TX_UNLOCK(sq)		mtx_unlock(&(sq)->mtx)
#define	NICVF_TX_LOCK_ASSERT(sq)	mtx_assert(&(sq)->mtx, MA_OWNED)

#define	NICVF_CMP_LOCK(cq)		mtx_lock(&(cq)->mtx)
#define	NICVF_CMP_UNLOCK(cq)		mtx_unlock(&(cq)->mtx)

int nicvf_set_qset_resources(struct nicvf *);
int nicvf_config_data_transfer(struct nicvf *, boolean_t);
void nicvf_qset_config(struct nicvf *, boolean_t);

void nicvf_enable_intr(struct nicvf *, int, int);
void nicvf_disable_intr(struct nicvf *, int, int);
void nicvf_clear_intr(struct nicvf *, int, int);
int nicvf_is_intr_enabled(struct nicvf *, int, int);

int nicvf_xmit_locked(struct snd_queue *sq);

/* Register access APIs */
void nicvf_reg_write(struct nicvf *, uint64_t, uint64_t);
uint64_t nicvf_reg_read(struct nicvf *, uint64_t);
void nicvf_qset_reg_write(struct nicvf *, uint64_t, uint64_t);
uint64_t nicvf_qset_reg_read(struct nicvf *, uint64_t);
void nicvf_queue_reg_write(struct nicvf *, uint64_t, uint64_t, uint64_t);
uint64_t nicvf_queue_reg_read(struct nicvf *, uint64_t, uint64_t);

/* Stats */
void nicvf_update_rq_stats(struct nicvf *, int);
void nicvf_update_sq_stats(struct nicvf *, int);
int nicvf_check_cqe_rx_errs(struct nicvf *, struct cmp_queue *,
    struct cqe_rx_t *);
int nicvf_check_cqe_tx_errs(struct nicvf *,struct cmp_queue *,
    struct cqe_send_t *);
#endif /* NICVF_QUEUES_H */
