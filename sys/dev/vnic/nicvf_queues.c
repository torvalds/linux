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
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bitset.h>
#include <sys/bitstring.h>
#include <sys/buf_ring.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/pciio.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/sockio.h>
#include <sys/socket.h>
#include <sys/stdatomic.h>
#include <sys/cpuset.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/smp.h>
#include <sys/taskqueue.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/bus.h>
#include <machine/vmparam.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>
#include <net/ifq.h>
#include <net/bpf.h>
#include <net/ethernet.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/sctp.h>
#include <netinet/tcp.h>
#include <netinet/tcp_lro.h>
#include <netinet/udp.h>

#include <netinet6/ip6_var.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include "thunder_bgx.h"
#include "nic_reg.h"
#include "nic.h"
#include "q_struct.h"
#include "nicvf_queues.h"

#define	DEBUG
#undef DEBUG

#ifdef DEBUG
#define	dprintf(dev, fmt, ...)	device_printf(dev, fmt, ##__VA_ARGS__)
#else
#define	dprintf(dev, fmt, ...)
#endif

MALLOC_DECLARE(M_NICVF);

static void nicvf_free_snd_queue(struct nicvf *, struct snd_queue *);
static struct mbuf * nicvf_get_rcv_mbuf(struct nicvf *, struct cqe_rx_t *);
static void nicvf_sq_disable(struct nicvf *, int);
static void nicvf_sq_enable(struct nicvf *, struct snd_queue *, int);
static void nicvf_put_sq_desc(struct snd_queue *, int);
static void nicvf_cmp_queue_config(struct nicvf *, struct queue_set *, int,
    boolean_t);
static void nicvf_sq_free_used_descs(struct nicvf *, struct snd_queue *, int);

static int nicvf_tx_mbuf_locked(struct snd_queue *, struct mbuf **);

static void nicvf_rbdr_task(void *, int);
static void nicvf_rbdr_task_nowait(void *, int);

struct rbuf_info {
	bus_dma_tag_t	dmat;
	bus_dmamap_t	dmap;
	struct mbuf *	mbuf;
};

#define GET_RBUF_INFO(x) ((struct rbuf_info *)((x) - NICVF_RCV_BUF_ALIGN_BYTES))

/* Poll a register for a specific value */
static int nicvf_poll_reg(struct nicvf *nic, int qidx,
			  uint64_t reg, int bit_pos, int bits, int val)
{
	uint64_t bit_mask;
	uint64_t reg_val;
	int timeout = 10;

	bit_mask = (1UL << bits) - 1;
	bit_mask = (bit_mask << bit_pos);

	while (timeout) {
		reg_val = nicvf_queue_reg_read(nic, reg, qidx);
		if (((reg_val & bit_mask) >> bit_pos) == val)
			return (0);

		DELAY(1000);
		timeout--;
	}
	device_printf(nic->dev, "Poll on reg 0x%lx failed\n", reg);
	return (ETIMEDOUT);
}

/* Callback for bus_dmamap_load() */
static void
nicvf_dmamap_q_cb(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	bus_addr_t *paddr;

	KASSERT(nseg == 1, ("wrong number of segments, should be 1"));
	paddr = arg;
	*paddr = segs->ds_addr;
}

/* Allocate memory for a queue's descriptors */
static int
nicvf_alloc_q_desc_mem(struct nicvf *nic, struct q_desc_mem *dmem,
    int q_len, int desc_size, int align_bytes)
{
	int err, err_dmat;

	/* Create DMA tag first */
	err = bus_dma_tag_create(
	    bus_get_dma_tag(nic->dev),		/* parent tag */
	    align_bytes,			/* alignment */
	    0,					/* boundary */
	    BUS_SPACE_MAXADDR,			/* lowaddr */
	    BUS_SPACE_MAXADDR,			/* highaddr */
	    NULL, NULL,				/* filtfunc, filtfuncarg */
	    (q_len * desc_size),		/* maxsize */
	    1,					/* nsegments */
	    (q_len * desc_size),		/* maxsegsize */
	    0,					/* flags */
	    NULL, NULL,				/* lockfunc, lockfuncarg */
	    &dmem->dmat);			/* dmat */

	if (err != 0) {
		device_printf(nic->dev,
		    "Failed to create busdma tag for descriptors ring\n");
		return (err);
	}

	/* Allocate segment of continuous DMA safe memory */
	err = bus_dmamem_alloc(
	    dmem->dmat,				/* DMA tag */
	    &dmem->base,			/* virtual address */
	    (BUS_DMA_NOWAIT | BUS_DMA_ZERO),	/* flags */
	    &dmem->dmap);			/* DMA map */
	if (err != 0) {
		device_printf(nic->dev, "Failed to allocate DMA safe memory for"
		    "descriptors ring\n");
		goto dmamem_fail;
	}

	err = bus_dmamap_load(
	    dmem->dmat,
	    dmem->dmap,
	    dmem->base,
	    (q_len * desc_size),		/* allocation size */
	    nicvf_dmamap_q_cb,			/* map to DMA address cb. */
	    &dmem->phys_base,			/* physical address */
	    BUS_DMA_NOWAIT);
	if (err != 0) {
		device_printf(nic->dev,
		    "Cannot load DMA map of descriptors ring\n");
		goto dmamap_fail;
	}

	dmem->q_len = q_len;
	dmem->size = (desc_size * q_len);

	return (0);

dmamap_fail:
	bus_dmamem_free(dmem->dmat, dmem->base, dmem->dmap);
	dmem->phys_base = 0;
dmamem_fail:
	err_dmat = bus_dma_tag_destroy(dmem->dmat);
	dmem->base = NULL;
	KASSERT(err_dmat == 0,
	    ("%s: Trying to destroy BUSY DMA tag", __func__));

	return (err);
}

/* Free queue's descriptor memory */
static void
nicvf_free_q_desc_mem(struct nicvf *nic, struct q_desc_mem *dmem)
{
	int err;

	if ((dmem == NULL) || (dmem->base == NULL))
		return;

	/* Unload a map */
	bus_dmamap_sync(dmem->dmat, dmem->dmap, BUS_DMASYNC_POSTREAD);
	bus_dmamap_unload(dmem->dmat, dmem->dmap);
	/* Free DMA memory */
	bus_dmamem_free(dmem->dmat, dmem->base, dmem->dmap);
	/* Destroy DMA tag */
	err = bus_dma_tag_destroy(dmem->dmat);

	KASSERT(err == 0,
	    ("%s: Trying to destroy BUSY DMA tag", __func__));

	dmem->phys_base = 0;
	dmem->base = NULL;
}

/*
 * Allocate buffer for packet reception
 * HW returns memory address where packet is DMA'ed but not a pointer
 * into RBDR ring, so save buffer address at the start of fragment and
 * align the start address to a cache aligned address
 */
static __inline int
nicvf_alloc_rcv_buffer(struct nicvf *nic, struct rbdr *rbdr,
    bus_dmamap_t dmap, int mflags, uint32_t buf_len, bus_addr_t *rbuf)
{
	struct mbuf *mbuf;
	struct rbuf_info *rinfo;
	bus_dma_segment_t segs[1];
	int nsegs;
	int err;

	mbuf = m_getjcl(mflags, MT_DATA, M_PKTHDR, MCLBYTES);
	if (mbuf == NULL)
		return (ENOMEM);

	/*
	 * The length is equal to the actual length + one 128b line
	 * used as a room for rbuf_info structure.
	 */
	mbuf->m_len = mbuf->m_pkthdr.len = buf_len;

	err = bus_dmamap_load_mbuf_sg(rbdr->rbdr_buff_dmat, dmap, mbuf, segs,
	    &nsegs, BUS_DMA_NOWAIT);
	if (err != 0) {
		device_printf(nic->dev,
		    "Failed to map mbuf into DMA visible memory, err: %d\n",
		    err);
		m_freem(mbuf);
		bus_dmamap_destroy(rbdr->rbdr_buff_dmat, dmap);
		return (err);
	}
	if (nsegs != 1)
		panic("Unexpected number of DMA segments for RB: %d", nsegs);
	/*
	 * Now use the room for rbuf_info structure
	 * and adjust mbuf data and length.
	 */
	rinfo = (struct rbuf_info *)mbuf->m_data;
	m_adj(mbuf, NICVF_RCV_BUF_ALIGN_BYTES);

	rinfo->dmat = rbdr->rbdr_buff_dmat;
	rinfo->dmap = dmap;
	rinfo->mbuf = mbuf;

	*rbuf = segs[0].ds_addr + NICVF_RCV_BUF_ALIGN_BYTES;

	return (0);
}

/* Retrieve mbuf for received packet */
static struct mbuf *
nicvf_rb_ptr_to_mbuf(struct nicvf *nic, bus_addr_t rb_ptr)
{
	struct mbuf *mbuf;
	struct rbuf_info *rinfo;

	/* Get buffer start address and alignment offset */
	rinfo = GET_RBUF_INFO(PHYS_TO_DMAP(rb_ptr));

	/* Now retrieve mbuf to give to stack */
	mbuf = rinfo->mbuf;
	if (__predict_false(mbuf == NULL)) {
		panic("%s: Received packet fragment with NULL mbuf",
		    device_get_nameunit(nic->dev));
	}
	/*
	 * Clear the mbuf in the descriptor to indicate
	 * that this slot is processed and free to use.
	 */
	rinfo->mbuf = NULL;

	bus_dmamap_sync(rinfo->dmat, rinfo->dmap, BUS_DMASYNC_POSTREAD);
	bus_dmamap_unload(rinfo->dmat, rinfo->dmap);

	return (mbuf);
}

/* Allocate RBDR ring and populate receive buffers */
static int
nicvf_init_rbdr(struct nicvf *nic, struct rbdr *rbdr, int ring_len,
    int buf_size, int qidx)
{
	bus_dmamap_t dmap;
	bus_addr_t rbuf;
	struct rbdr_entry_t *desc;
	int idx;
	int err;

	/* Allocate rbdr descriptors ring */
	err = nicvf_alloc_q_desc_mem(nic, &rbdr->dmem, ring_len,
	    sizeof(struct rbdr_entry_t), NICVF_RCV_BUF_ALIGN_BYTES);
	if (err != 0) {
		device_printf(nic->dev,
		    "Failed to create RBDR descriptors ring\n");
		return (err);
	}

	rbdr->desc = rbdr->dmem.base;
	/*
	 * Buffer size has to be in multiples of 128 bytes.
	 * Make room for metadata of size of one line (128 bytes).
	 */
	rbdr->dma_size = buf_size - NICVF_RCV_BUF_ALIGN_BYTES;
	rbdr->enable = TRUE;
	rbdr->thresh = RBDR_THRESH;
	rbdr->nic = nic;
	rbdr->idx = qidx;

	/*
	 * Create DMA tag for Rx buffers.
	 * Each map created using this tag is intended to store Rx payload for
	 * one fragment and one header structure containing rbuf_info (thus
	 * additional 128 byte line since RB must be a multiple of 128 byte
	 * cache line).
	 */
	if (buf_size > MCLBYTES) {
		device_printf(nic->dev,
		    "Buffer size to large for mbuf cluster\n");
		return (EINVAL);
	}
	err = bus_dma_tag_create(
	    bus_get_dma_tag(nic->dev),		/* parent tag */
	    NICVF_RCV_BUF_ALIGN_BYTES,		/* alignment */
	    0,					/* boundary */
	    DMAP_MAX_PHYSADDR,			/* lowaddr */
	    DMAP_MIN_PHYSADDR,			/* highaddr */
	    NULL, NULL,				/* filtfunc, filtfuncarg */
	    roundup2(buf_size, MCLBYTES),	/* maxsize */
	    1,					/* nsegments */
	    roundup2(buf_size, MCLBYTES),	/* maxsegsize */
	    0,					/* flags */
	    NULL, NULL,				/* lockfunc, lockfuncarg */
	    &rbdr->rbdr_buff_dmat);		/* dmat */

	if (err != 0) {
		device_printf(nic->dev,
		    "Failed to create busdma tag for RBDR buffers\n");
		return (err);
	}

	rbdr->rbdr_buff_dmaps = malloc(sizeof(*rbdr->rbdr_buff_dmaps) *
	    ring_len, M_NICVF, (M_WAITOK | M_ZERO));

	for (idx = 0; idx < ring_len; idx++) {
		err = bus_dmamap_create(rbdr->rbdr_buff_dmat, 0, &dmap);
		if (err != 0) {
			device_printf(nic->dev,
			    "Failed to create DMA map for RB\n");
			return (err);
		}
		rbdr->rbdr_buff_dmaps[idx] = dmap;

		err = nicvf_alloc_rcv_buffer(nic, rbdr, dmap, M_WAITOK,
		    DMA_BUFFER_LEN, &rbuf);
		if (err != 0)
			return (err);

		desc = GET_RBDR_DESC(rbdr, idx);
		desc->buf_addr = (rbuf >> NICVF_RCV_BUF_ALIGN);
	}

	/* Allocate taskqueue */
	TASK_INIT(&rbdr->rbdr_task, 0, nicvf_rbdr_task, rbdr);
	TASK_INIT(&rbdr->rbdr_task_nowait, 0, nicvf_rbdr_task_nowait, rbdr);
	rbdr->rbdr_taskq = taskqueue_create_fast("nicvf_rbdr_taskq", M_WAITOK,
	    taskqueue_thread_enqueue, &rbdr->rbdr_taskq);
	taskqueue_start_threads(&rbdr->rbdr_taskq, 1, PI_NET, "%s: rbdr_taskq",
	    device_get_nameunit(nic->dev));

	return (0);
}

/* Free RBDR ring and its receive buffers */
static void
nicvf_free_rbdr(struct nicvf *nic, struct rbdr *rbdr)
{
	struct mbuf *mbuf;
	struct queue_set *qs;
	struct rbdr_entry_t *desc;
	struct rbuf_info *rinfo;
	bus_addr_t buf_addr;
	int head, tail, idx;
	int err;

	qs = nic->qs;

	if ((qs == NULL) || (rbdr == NULL))
		return;

	rbdr->enable = FALSE;
	if (rbdr->rbdr_taskq != NULL) {
		/* Remove tasks */
		while (taskqueue_cancel(rbdr->rbdr_taskq,
		    &rbdr->rbdr_task_nowait, NULL) != 0) {
			/* Finish the nowait task first */
			taskqueue_drain(rbdr->rbdr_taskq,
			    &rbdr->rbdr_task_nowait);
		}
		taskqueue_free(rbdr->rbdr_taskq);
		rbdr->rbdr_taskq = NULL;

		while (taskqueue_cancel(taskqueue_thread,
		    &rbdr->rbdr_task, NULL) != 0) {
			/* Now finish the sleepable task */
			taskqueue_drain(taskqueue_thread, &rbdr->rbdr_task);
		}
	}

	/*
	 * Free all of the memory under the RB descriptors.
	 * There are assumptions here:
	 * 1. Corresponding RBDR is disabled
	 *    - it is safe to operate using head and tail indexes
	 * 2. All bffers that were received are properly freed by
	 *    the receive handler
	 *    - there is no need to unload DMA map and free MBUF for other
	 *      descriptors than unused ones
	 */
	if (rbdr->rbdr_buff_dmat != NULL) {
		head = rbdr->head;
		tail = rbdr->tail;
		while (head != tail) {
			desc = GET_RBDR_DESC(rbdr, head);
			buf_addr = desc->buf_addr << NICVF_RCV_BUF_ALIGN;
			rinfo = GET_RBUF_INFO(PHYS_TO_DMAP(buf_addr));
			bus_dmamap_unload(rbdr->rbdr_buff_dmat, rinfo->dmap);
			mbuf = rinfo->mbuf;
			/* This will destroy everything including rinfo! */
			m_freem(mbuf);
			head++;
			head &= (rbdr->dmem.q_len - 1);
		}
		/* Free tail descriptor */
		desc = GET_RBDR_DESC(rbdr, tail);
		buf_addr = desc->buf_addr << NICVF_RCV_BUF_ALIGN;
		rinfo = GET_RBUF_INFO(PHYS_TO_DMAP(buf_addr));
		bus_dmamap_unload(rbdr->rbdr_buff_dmat, rinfo->dmap);
		mbuf = rinfo->mbuf;
		/* This will destroy everything including rinfo! */
		m_freem(mbuf);

		/* Destroy DMA maps */
		for (idx = 0; idx < qs->rbdr_len; idx++) {
			if (rbdr->rbdr_buff_dmaps[idx] == NULL)
				continue;
			err = bus_dmamap_destroy(rbdr->rbdr_buff_dmat,
			    rbdr->rbdr_buff_dmaps[idx]);
			KASSERT(err == 0,
			    ("%s: Could not destroy DMA map for RB, desc: %d",
			    __func__, idx));
			rbdr->rbdr_buff_dmaps[idx] = NULL;
		}

		/* Now destroy the tag */
		err = bus_dma_tag_destroy(rbdr->rbdr_buff_dmat);
		KASSERT(err == 0,
		    ("%s: Trying to destroy BUSY DMA tag", __func__));

		rbdr->head = 0;
		rbdr->tail = 0;
	}

	/* Free RBDR ring */
	nicvf_free_q_desc_mem(nic, &rbdr->dmem);
}

/*
 * Refill receive buffer descriptors with new buffers.
 */
static int
nicvf_refill_rbdr(struct rbdr *rbdr, int mflags)
{
	struct nicvf *nic;
	struct queue_set *qs;
	int rbdr_idx;
	int tail, qcount;
	int refill_rb_cnt;
	struct rbdr_entry_t *desc;
	bus_dmamap_t dmap;
	bus_addr_t rbuf;
	boolean_t rb_alloc_fail;
	int new_rb;

	rb_alloc_fail = TRUE;
	new_rb = 0;
	nic = rbdr->nic;
	qs = nic->qs;
	rbdr_idx = rbdr->idx;

	/* Check if it's enabled */
	if (!rbdr->enable)
		return (0);

	/* Get no of desc's to be refilled */
	qcount = nicvf_queue_reg_read(nic, NIC_QSET_RBDR_0_1_STATUS0, rbdr_idx);
	qcount &= 0x7FFFF;
	/* Doorbell can be ringed with a max of ring size minus 1 */
	if (qcount >= (qs->rbdr_len - 1)) {
		rb_alloc_fail = FALSE;
		goto out;
	} else
		refill_rb_cnt = qs->rbdr_len - qcount - 1;

	/* Start filling descs from tail */
	tail = nicvf_queue_reg_read(nic, NIC_QSET_RBDR_0_1_TAIL, rbdr_idx) >> 3;
	while (refill_rb_cnt) {
		tail++;
		tail &= (rbdr->dmem.q_len - 1);

		dmap = rbdr->rbdr_buff_dmaps[tail];
		if (nicvf_alloc_rcv_buffer(nic, rbdr, dmap, mflags,
		    DMA_BUFFER_LEN, &rbuf)) {
			/* Something went wrong. Resign */
			break;
		}
		desc = GET_RBDR_DESC(rbdr, tail);
		desc->buf_addr = (rbuf >> NICVF_RCV_BUF_ALIGN);
		refill_rb_cnt--;
		new_rb++;
	}

	/* make sure all memory stores are done before ringing doorbell */
	wmb();

	/* Check if buffer allocation failed */
	if (refill_rb_cnt == 0)
		rb_alloc_fail = FALSE;

	/* Notify HW */
	nicvf_queue_reg_write(nic, NIC_QSET_RBDR_0_1_DOOR,
			      rbdr_idx, new_rb);
out:
	if (!rb_alloc_fail) {
		/*
		 * Re-enable RBDR interrupts only
		 * if buffer allocation is success.
		 */
		nicvf_enable_intr(nic, NICVF_INTR_RBDR, rbdr_idx);

		return (0);
	}

	return (ENOMEM);
}

/* Refill RBs even if sleep is needed to reclaim memory */
static void
nicvf_rbdr_task(void *arg, int pending)
{
	struct rbdr *rbdr;
	int err;

	rbdr = (struct rbdr *)arg;

	err = nicvf_refill_rbdr(rbdr, M_WAITOK);
	if (__predict_false(err != 0)) {
		panic("%s: Failed to refill RBs even when sleep enabled",
		    __func__);
	}
}

/* Refill RBs as soon as possible without waiting */
static void
nicvf_rbdr_task_nowait(void *arg, int pending)
{
	struct rbdr *rbdr;
	int err;

	rbdr = (struct rbdr *)arg;

	err = nicvf_refill_rbdr(rbdr, M_NOWAIT);
	if (err != 0) {
		/*
		 * Schedule another, sleepable kernel thread
		 * that will for sure refill the buffers.
		 */
		taskqueue_enqueue(taskqueue_thread, &rbdr->rbdr_task);
	}
}

static int
nicvf_rcv_pkt_handler(struct nicvf *nic, struct cmp_queue *cq,
    struct cqe_rx_t *cqe_rx, int cqe_type)
{
	struct mbuf *mbuf;
	struct rcv_queue *rq;
	int rq_idx;
	int err = 0;

	rq_idx = cqe_rx->rq_idx;
	rq = &nic->qs->rq[rq_idx];

	/* Check for errors */
	err = nicvf_check_cqe_rx_errs(nic, cq, cqe_rx);
	if (err && !cqe_rx->rb_cnt)
		return (0);

	mbuf = nicvf_get_rcv_mbuf(nic, cqe_rx);
	if (mbuf == NULL) {
		dprintf(nic->dev, "Packet not received\n");
		return (0);
	}

	/* If error packet */
	if (err != 0) {
		m_freem(mbuf);
		return (0);
	}

	if (rq->lro_enabled &&
	    ((cqe_rx->l3_type == L3TYPE_IPV4) && (cqe_rx->l4_type == L4TYPE_TCP)) &&
	    (mbuf->m_pkthdr.csum_flags & (CSUM_DATA_VALID | CSUM_PSEUDO_HDR)) ==
            (CSUM_DATA_VALID | CSUM_PSEUDO_HDR)) {
		/*
		 * At this point it is known that there are no errors in the
		 * packet. Attempt to LRO enqueue. Send to stack if no resources
		 * or enqueue error.
		 */
		if ((rq->lro.lro_cnt != 0) &&
		    (tcp_lro_rx(&rq->lro, mbuf, 0) == 0))
			return (0);
	}
	/*
	 * Push this packet to the stack later to avoid
	 * unlocking completion task in the middle of work.
	 */
	err = buf_ring_enqueue(cq->rx_br, mbuf);
	if (err != 0) {
		/*
		 * Failed to enqueue this mbuf.
		 * We don't drop it, just schedule another task.
		 */
		return (err);
	}

	return (0);
}

static void
nicvf_snd_pkt_handler(struct nicvf *nic, struct cmp_queue *cq,
    struct cqe_send_t *cqe_tx, int cqe_type)
{
	bus_dmamap_t dmap;
	struct mbuf *mbuf;
	struct snd_queue *sq;
	struct sq_hdr_subdesc *hdr;

	mbuf = NULL;
	sq = &nic->qs->sq[cqe_tx->sq_idx];

	hdr = (struct sq_hdr_subdesc *)GET_SQ_DESC(sq, cqe_tx->sqe_ptr);
	if (hdr->subdesc_type != SQ_DESC_TYPE_HEADER)
		return;

	dprintf(nic->dev,
	    "%s Qset #%d SQ #%d SQ ptr #%d subdesc count %d\n",
	    __func__, cqe_tx->sq_qs, cqe_tx->sq_idx,
	    cqe_tx->sqe_ptr, hdr->subdesc_cnt);

	dmap = (bus_dmamap_t)sq->snd_buff[cqe_tx->sqe_ptr].dmap;
	bus_dmamap_unload(sq->snd_buff_dmat, dmap);

	mbuf = (struct mbuf *)sq->snd_buff[cqe_tx->sqe_ptr].mbuf;
	if (mbuf != NULL) {
		m_freem(mbuf);
		sq->snd_buff[cqe_tx->sqe_ptr].mbuf = NULL;
		nicvf_put_sq_desc(sq, hdr->subdesc_cnt + 1);
	}

	nicvf_check_cqe_tx_errs(nic, cq, cqe_tx);
}

static int
nicvf_cq_intr_handler(struct nicvf *nic, uint8_t cq_idx)
{
	struct mbuf *mbuf;
	struct ifnet *ifp;
	int processed_cqe, work_done = 0, tx_done = 0;
	int cqe_count, cqe_head;
	struct queue_set *qs = nic->qs;
	struct cmp_queue *cq = &qs->cq[cq_idx];
	struct snd_queue *sq = &qs->sq[cq_idx];
	struct rcv_queue *rq;
	struct cqe_rx_t *cq_desc;
	struct lro_ctrl	*lro;
	int rq_idx;
	int cmp_err;

	NICVF_CMP_LOCK(cq);
	cmp_err = 0;
	processed_cqe = 0;
	/* Get no of valid CQ entries to process */
	cqe_count = nicvf_queue_reg_read(nic, NIC_QSET_CQ_0_7_STATUS, cq_idx);
	cqe_count &= CQ_CQE_COUNT;
	if (cqe_count == 0)
		goto out;

	/* Get head of the valid CQ entries */
	cqe_head = nicvf_queue_reg_read(nic, NIC_QSET_CQ_0_7_HEAD, cq_idx) >> 9;
	cqe_head &= 0xFFFF;

	dprintf(nic->dev, "%s CQ%d cqe_count %d cqe_head %d\n",
	    __func__, cq_idx, cqe_count, cqe_head);
	while (processed_cqe < cqe_count) {
		/* Get the CQ descriptor */
		cq_desc = (struct cqe_rx_t *)GET_CQ_DESC(cq, cqe_head);
		cqe_head++;
		cqe_head &= (cq->dmem.q_len - 1);
		/* Prefetch next CQ descriptor */
		__builtin_prefetch((struct cqe_rx_t *)GET_CQ_DESC(cq, cqe_head));

		dprintf(nic->dev, "CQ%d cq_desc->cqe_type %d\n", cq_idx,
		    cq_desc->cqe_type);
		switch (cq_desc->cqe_type) {
		case CQE_TYPE_RX:
			cmp_err = nicvf_rcv_pkt_handler(nic, cq, cq_desc,
			    CQE_TYPE_RX);
			if (__predict_false(cmp_err != 0)) {
				/*
				 * Ups. Cannot finish now.
				 * Let's try again later.
				 */
				goto done;
			}
			work_done++;
			break;
		case CQE_TYPE_SEND:
			nicvf_snd_pkt_handler(nic, cq, (void *)cq_desc,
			    CQE_TYPE_SEND);
			tx_done++;
			break;
		case CQE_TYPE_INVALID:
		case CQE_TYPE_RX_SPLIT:
		case CQE_TYPE_RX_TCP:
		case CQE_TYPE_SEND_PTP:
			/* Ignore for now */
			break;
		}
		processed_cqe++;
	}
done:
	dprintf(nic->dev,
	    "%s CQ%d processed_cqe %d work_done %d\n",
	    __func__, cq_idx, processed_cqe, work_done);

	/* Ring doorbell to inform H/W to reuse processed CQEs */
	nicvf_queue_reg_write(nic, NIC_QSET_CQ_0_7_DOOR, cq_idx, processed_cqe);

	if ((tx_done > 0) &&
	    ((if_getdrvflags(nic->ifp) & IFF_DRV_RUNNING) != 0)) {
		/* Reenable TXQ if its stopped earlier due to SQ full */
		if_setdrvflagbits(nic->ifp, IFF_DRV_RUNNING, IFF_DRV_OACTIVE);
		taskqueue_enqueue(sq->snd_taskq, &sq->snd_task);
	}
out:
	/*
	 * Flush any outstanding LRO work
	 */
	rq_idx = cq_idx;
	rq = &nic->qs->rq[rq_idx];
	lro = &rq->lro;
	tcp_lro_flush_all(lro);

	NICVF_CMP_UNLOCK(cq);

	ifp = nic->ifp;
	/* Push received MBUFs to the stack */
	while (!buf_ring_empty(cq->rx_br)) {
		mbuf = buf_ring_dequeue_mc(cq->rx_br);
		if (__predict_true(mbuf != NULL))
			(*ifp->if_input)(ifp, mbuf);
	}

	return (cmp_err);
}

/*
 * Qset error interrupt handler
 *
 * As of now only CQ errors are handled
 */
static void
nicvf_qs_err_task(void *arg, int pending)
{
	struct nicvf *nic;
	struct queue_set *qs;
	int qidx;
	uint64_t status;
	boolean_t enable = TRUE;

	nic = (struct nicvf *)arg;
	qs = nic->qs;

	/* Deactivate network interface */
	if_setdrvflagbits(nic->ifp, IFF_DRV_OACTIVE, IFF_DRV_RUNNING);

	/* Check if it is CQ err */
	for (qidx = 0; qidx < qs->cq_cnt; qidx++) {
		status = nicvf_queue_reg_read(nic, NIC_QSET_CQ_0_7_STATUS,
		    qidx);
		if ((status & CQ_ERR_MASK) == 0)
			continue;
		/* Process already queued CQEs and reconfig CQ */
		nicvf_disable_intr(nic, NICVF_INTR_CQ, qidx);
		nicvf_sq_disable(nic, qidx);
		(void)nicvf_cq_intr_handler(nic, qidx);
		nicvf_cmp_queue_config(nic, qs, qidx, enable);
		nicvf_sq_free_used_descs(nic, &qs->sq[qidx], qidx);
		nicvf_sq_enable(nic, &qs->sq[qidx], qidx);
		nicvf_enable_intr(nic, NICVF_INTR_CQ, qidx);
	}

	if_setdrvflagbits(nic->ifp, IFF_DRV_RUNNING, IFF_DRV_OACTIVE);
	/* Re-enable Qset error interrupt */
	nicvf_enable_intr(nic, NICVF_INTR_QS_ERR, 0);
}

static void
nicvf_cmp_task(void *arg, int pending)
{
	struct cmp_queue *cq;
	struct nicvf *nic;
	int cmp_err;

	cq = (struct cmp_queue *)arg;
	nic = cq->nic;

	/* Handle CQ descriptors */
	cmp_err = nicvf_cq_intr_handler(nic, cq->idx);
	if (__predict_false(cmp_err != 0)) {
		/*
		 * Schedule another thread here since we did not
		 * process the entire CQ due to Tx or Rx CQ parse error.
		 */
		taskqueue_enqueue(cq->cmp_taskq, &cq->cmp_task);

	}

	nicvf_clear_intr(nic, NICVF_INTR_CQ, cq->idx);
	/* Reenable interrupt (previously disabled in nicvf_intr_handler() */
	nicvf_enable_intr(nic, NICVF_INTR_CQ, cq->idx);

}

/* Initialize completion queue */
static int
nicvf_init_cmp_queue(struct nicvf *nic, struct cmp_queue *cq, int q_len,
    int qidx)
{
	int err;

	/* Initizalize lock */
	snprintf(cq->mtx_name, sizeof(cq->mtx_name), "%s: CQ(%d) lock",
	    device_get_nameunit(nic->dev), qidx);
	mtx_init(&cq->mtx, cq->mtx_name, NULL, MTX_DEF);

	err = nicvf_alloc_q_desc_mem(nic, &cq->dmem, q_len, CMP_QUEUE_DESC_SIZE,
				     NICVF_CQ_BASE_ALIGN_BYTES);

	if (err != 0) {
		device_printf(nic->dev,
		    "Could not allocate DMA memory for CQ\n");
		return (err);
	}

	cq->desc = cq->dmem.base;
	cq->thresh = pass1_silicon(nic->dev) ? 0 : CMP_QUEUE_CQE_THRESH;
	cq->nic = nic;
	cq->idx = qidx;
	nic->cq_coalesce_usecs = (CMP_QUEUE_TIMER_THRESH * 0.05) - 1;

	cq->rx_br = buf_ring_alloc(CMP_QUEUE_LEN * 8, M_DEVBUF, M_WAITOK,
	    &cq->mtx);

	/* Allocate taskqueue */
	TASK_INIT(&cq->cmp_task, 0, nicvf_cmp_task, cq);
	cq->cmp_taskq = taskqueue_create_fast("nicvf_cmp_taskq", M_WAITOK,
	    taskqueue_thread_enqueue, &cq->cmp_taskq);
	taskqueue_start_threads(&cq->cmp_taskq, 1, PI_NET, "%s: cmp_taskq(%d)",
	    device_get_nameunit(nic->dev), qidx);

	return (0);
}

static void
nicvf_free_cmp_queue(struct nicvf *nic, struct cmp_queue *cq)
{

	if (cq == NULL)
		return;
	/*
	 * The completion queue itself should be disabled by now
	 * (ref. nicvf_snd_queue_config()).
	 * Ensure that it is safe to disable it or panic.
	 */
	if (cq->enable)
		panic("%s: Trying to free working CQ(%d)", __func__, cq->idx);

	if (cq->cmp_taskq != NULL) {
		/* Remove task */
		while (taskqueue_cancel(cq->cmp_taskq, &cq->cmp_task, NULL) != 0)
			taskqueue_drain(cq->cmp_taskq, &cq->cmp_task);

		taskqueue_free(cq->cmp_taskq);
		cq->cmp_taskq = NULL;
	}
	/*
	 * Completion interrupt will possibly enable interrupts again
	 * so disable interrupting now after we finished processing
	 * completion task. It is safe to do so since the corresponding CQ
	 * was already disabled.
	 */
	nicvf_disable_intr(nic, NICVF_INTR_CQ, cq->idx);
	nicvf_clear_intr(nic, NICVF_INTR_CQ, cq->idx);

	NICVF_CMP_LOCK(cq);
	nicvf_free_q_desc_mem(nic, &cq->dmem);
	drbr_free(cq->rx_br, M_DEVBUF);
	NICVF_CMP_UNLOCK(cq);
	mtx_destroy(&cq->mtx);
	memset(cq->mtx_name, 0, sizeof(cq->mtx_name));
}

int
nicvf_xmit_locked(struct snd_queue *sq)
{
	struct nicvf *nic;
	struct ifnet *ifp;
	struct mbuf *next;
	int err;

	NICVF_TX_LOCK_ASSERT(sq);

	nic = sq->nic;
	ifp = nic->ifp;
	err = 0;

	while ((next = drbr_peek(ifp, sq->br)) != NULL) {
		/* Send a copy of the frame to the BPF listener */
		ETHER_BPF_MTAP(ifp, next);

		err = nicvf_tx_mbuf_locked(sq, &next);
		if (err != 0) {
			if (next == NULL)
				drbr_advance(ifp, sq->br);
			else
				drbr_putback(ifp, sq->br, next);

			break;
		}
		drbr_advance(ifp, sq->br);
	}
	return (err);
}

static void
nicvf_snd_task(void *arg, int pending)
{
	struct snd_queue *sq = (struct snd_queue *)arg;
	struct nicvf *nic;
	struct ifnet *ifp;
	int err;

	nic = sq->nic;
	ifp = nic->ifp;

	/*
	 * Skip sending anything if the driver is not running,
	 * SQ full or link is down.
	 */
	if (((if_getdrvflags(ifp) & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING) || !nic->link_up)
		return;

	NICVF_TX_LOCK(sq);
	err = nicvf_xmit_locked(sq);
	NICVF_TX_UNLOCK(sq);
	/* Try again */
	if (err != 0)
		taskqueue_enqueue(sq->snd_taskq, &sq->snd_task);
}

/* Initialize transmit queue */
static int
nicvf_init_snd_queue(struct nicvf *nic, struct snd_queue *sq, int q_len,
    int qidx)
{
	size_t i;
	int err;

	/* Initizalize TX lock for this queue */
	snprintf(sq->mtx_name, sizeof(sq->mtx_name), "%s: SQ(%d) lock",
	    device_get_nameunit(nic->dev), qidx);
	mtx_init(&sq->mtx, sq->mtx_name, NULL, MTX_DEF);

	NICVF_TX_LOCK(sq);
	/* Allocate buffer ring */
	sq->br = buf_ring_alloc(q_len / MIN_SQ_DESC_PER_PKT_XMIT, M_DEVBUF,
	    M_NOWAIT, &sq->mtx);
	if (sq->br == NULL) {
		device_printf(nic->dev,
		    "ERROR: Could not set up buf ring for SQ(%d)\n", qidx);
		err = ENOMEM;
		goto error;
	}

	/* Allocate DMA memory for Tx descriptors */
	err = nicvf_alloc_q_desc_mem(nic, &sq->dmem, q_len, SND_QUEUE_DESC_SIZE,
				     NICVF_SQ_BASE_ALIGN_BYTES);
	if (err != 0) {
		device_printf(nic->dev,
		    "Could not allocate DMA memory for SQ\n");
		goto error;
	}

	sq->desc = sq->dmem.base;
	sq->head = sq->tail = 0;
	atomic_store_rel_int(&sq->free_cnt, q_len - 1);
	sq->thresh = SND_QUEUE_THRESH;
	sq->idx = qidx;
	sq->nic = nic;

	/*
	 * Allocate DMA maps for Tx buffers
	 */

	/* Create DMA tag first */
	err = bus_dma_tag_create(
	    bus_get_dma_tag(nic->dev),		/* parent tag */
	    1,					/* alignment */
	    0,					/* boundary */
	    BUS_SPACE_MAXADDR,			/* lowaddr */
	    BUS_SPACE_MAXADDR,			/* highaddr */
	    NULL, NULL,				/* filtfunc, filtfuncarg */
	    NICVF_TSO_MAXSIZE,			/* maxsize */
	    NICVF_TSO_NSEGS,			/* nsegments */
	    MCLBYTES,				/* maxsegsize */
	    0,					/* flags */
	    NULL, NULL,				/* lockfunc, lockfuncarg */
	    &sq->snd_buff_dmat);		/* dmat */

	if (err != 0) {
		device_printf(nic->dev,
		    "Failed to create busdma tag for Tx buffers\n");
		goto error;
	}

	/* Allocate send buffers array */
	sq->snd_buff = malloc(sizeof(*sq->snd_buff) * q_len, M_NICVF,
	    (M_NOWAIT | M_ZERO));
	if (sq->snd_buff == NULL) {
		device_printf(nic->dev,
		    "Could not allocate memory for Tx buffers array\n");
		err = ENOMEM;
		goto error;
	}

	/* Now populate maps */
	for (i = 0; i < q_len; i++) {
		err = bus_dmamap_create(sq->snd_buff_dmat, 0,
		    &sq->snd_buff[i].dmap);
		if (err != 0) {
			device_printf(nic->dev,
			    "Failed to create DMA maps for Tx buffers\n");
			goto error;
		}
	}
	NICVF_TX_UNLOCK(sq);

	/* Allocate taskqueue */
	TASK_INIT(&sq->snd_task, 0, nicvf_snd_task, sq);
	sq->snd_taskq = taskqueue_create_fast("nicvf_snd_taskq", M_WAITOK,
	    taskqueue_thread_enqueue, &sq->snd_taskq);
	taskqueue_start_threads(&sq->snd_taskq, 1, PI_NET, "%s: snd_taskq(%d)",
	    device_get_nameunit(nic->dev), qidx);

	return (0);
error:
	NICVF_TX_UNLOCK(sq);
	return (err);
}

static void
nicvf_free_snd_queue(struct nicvf *nic, struct snd_queue *sq)
{
	struct queue_set *qs = nic->qs;
	size_t i;
	int err;

	if (sq == NULL)
		return;

	if (sq->snd_taskq != NULL) {
		/* Remove task */
		while (taskqueue_cancel(sq->snd_taskq, &sq->snd_task, NULL) != 0)
			taskqueue_drain(sq->snd_taskq, &sq->snd_task);

		taskqueue_free(sq->snd_taskq);
		sq->snd_taskq = NULL;
	}

	NICVF_TX_LOCK(sq);
	if (sq->snd_buff_dmat != NULL) {
		if (sq->snd_buff != NULL) {
			for (i = 0; i < qs->sq_len; i++) {
				m_freem(sq->snd_buff[i].mbuf);
				sq->snd_buff[i].mbuf = NULL;

				bus_dmamap_unload(sq->snd_buff_dmat,
				    sq->snd_buff[i].dmap);
				err = bus_dmamap_destroy(sq->snd_buff_dmat,
				    sq->snd_buff[i].dmap);
				/*
				 * If bus_dmamap_destroy fails it can cause
				 * random panic later if the tag is also
				 * destroyed in the process.
				 */
				KASSERT(err == 0,
				    ("%s: Could not destroy DMA map for SQ",
				    __func__));
			}
		}

		free(sq->snd_buff, M_NICVF);

		err = bus_dma_tag_destroy(sq->snd_buff_dmat);
		KASSERT(err == 0,
		    ("%s: Trying to destroy BUSY DMA tag", __func__));
	}

	/* Free private driver ring for this send queue */
	if (sq->br != NULL)
		drbr_free(sq->br, M_DEVBUF);

	if (sq->dmem.base != NULL)
		nicvf_free_q_desc_mem(nic, &sq->dmem);

	NICVF_TX_UNLOCK(sq);
	/* Destroy Tx lock */
	mtx_destroy(&sq->mtx);
	memset(sq->mtx_name, 0, sizeof(sq->mtx_name));
}

static void
nicvf_reclaim_snd_queue(struct nicvf *nic, struct queue_set *qs, int qidx)
{

	/* Disable send queue */
	nicvf_queue_reg_write(nic, NIC_QSET_SQ_0_7_CFG, qidx, 0);
	/* Check if SQ is stopped */
	if (nicvf_poll_reg(nic, qidx, NIC_QSET_SQ_0_7_STATUS, 21, 1, 0x01))
		return;
	/* Reset send queue */
	nicvf_queue_reg_write(nic, NIC_QSET_SQ_0_7_CFG, qidx, NICVF_SQ_RESET);
}

static void
nicvf_reclaim_rcv_queue(struct nicvf *nic, struct queue_set *qs, int qidx)
{
	union nic_mbx mbx = {};

	/* Make sure all packets in the pipeline are written back into mem */
	mbx.msg.msg = NIC_MBOX_MSG_RQ_SW_SYNC;
	nicvf_send_msg_to_pf(nic, &mbx);
}

static void
nicvf_reclaim_cmp_queue(struct nicvf *nic, struct queue_set *qs, int qidx)
{

	/* Disable timer threshold (doesn't get reset upon CQ reset */
	nicvf_queue_reg_write(nic, NIC_QSET_CQ_0_7_CFG2, qidx, 0);
	/* Disable completion queue */
	nicvf_queue_reg_write(nic, NIC_QSET_CQ_0_7_CFG, qidx, 0);
	/* Reset completion queue */
	nicvf_queue_reg_write(nic, NIC_QSET_CQ_0_7_CFG, qidx, NICVF_CQ_RESET);
}

static void
nicvf_reclaim_rbdr(struct nicvf *nic, struct rbdr *rbdr, int qidx)
{
	uint64_t tmp, fifo_state;
	int timeout = 10;

	/* Save head and tail pointers for feeing up buffers */
	rbdr->head =
	    nicvf_queue_reg_read(nic, NIC_QSET_RBDR_0_1_HEAD, qidx) >> 3;
	rbdr->tail =
	    nicvf_queue_reg_read(nic, NIC_QSET_RBDR_0_1_TAIL, qidx) >> 3;

	/*
	 * If RBDR FIFO is in 'FAIL' state then do a reset first
	 * before relaiming.
	 */
	fifo_state = nicvf_queue_reg_read(nic, NIC_QSET_RBDR_0_1_STATUS0, qidx);
	if (((fifo_state >> 62) & 0x03) == 0x3) {
		nicvf_queue_reg_write(nic, NIC_QSET_RBDR_0_1_CFG,
		    qidx, NICVF_RBDR_RESET);
	}

	/* Disable RBDR */
	nicvf_queue_reg_write(nic, NIC_QSET_RBDR_0_1_CFG, qidx, 0);
	if (nicvf_poll_reg(nic, qidx, NIC_QSET_RBDR_0_1_STATUS0, 62, 2, 0x00))
		return;
	while (1) {
		tmp = nicvf_queue_reg_read(nic,
		    NIC_QSET_RBDR_0_1_PREFETCH_STATUS, qidx);
		if ((tmp & 0xFFFFFFFF) == ((tmp >> 32) & 0xFFFFFFFF))
			break;

		DELAY(1000);
		timeout--;
		if (!timeout) {
			device_printf(nic->dev,
			    "Failed polling on prefetch status\n");
			return;
		}
	}
	nicvf_queue_reg_write(nic, NIC_QSET_RBDR_0_1_CFG, qidx,
	    NICVF_RBDR_RESET);

	if (nicvf_poll_reg(nic, qidx, NIC_QSET_RBDR_0_1_STATUS0, 62, 2, 0x02))
		return;
	nicvf_queue_reg_write(nic, NIC_QSET_RBDR_0_1_CFG, qidx, 0x00);
	if (nicvf_poll_reg(nic, qidx, NIC_QSET_RBDR_0_1_STATUS0, 62, 2, 0x00))
		return;
}

/* Configures receive queue */
static void
nicvf_rcv_queue_config(struct nicvf *nic, struct queue_set *qs,
    int qidx, bool enable)
{
	union nic_mbx mbx = {};
	struct rcv_queue *rq;
	struct rq_cfg rq_cfg;
	struct ifnet *ifp;
	struct lro_ctrl	*lro;

	ifp = nic->ifp;

	rq = &qs->rq[qidx];
	rq->enable = enable;

	lro = &rq->lro;

	/* Disable receive queue */
	nicvf_queue_reg_write(nic, NIC_QSET_RQ_0_7_CFG, qidx, 0);

	if (!rq->enable) {
		nicvf_reclaim_rcv_queue(nic, qs, qidx);
		/* Free LRO memory */
		tcp_lro_free(lro);
		rq->lro_enabled = FALSE;
		return;
	}

	/* Configure LRO if enabled */
	rq->lro_enabled = FALSE;
	if ((if_getcapenable(ifp) & IFCAP_LRO) != 0) {
		if (tcp_lro_init(lro) != 0) {
			device_printf(nic->dev,
			    "Failed to initialize LRO for RXQ%d\n", qidx);
		} else {
			rq->lro_enabled = TRUE;
			lro->ifp = nic->ifp;
		}
	}

	rq->cq_qs = qs->vnic_id;
	rq->cq_idx = qidx;
	rq->start_rbdr_qs = qs->vnic_id;
	rq->start_qs_rbdr_idx = qs->rbdr_cnt - 1;
	rq->cont_rbdr_qs = qs->vnic_id;
	rq->cont_qs_rbdr_idx = qs->rbdr_cnt - 1;
	/* all writes of RBDR data to be loaded into L2 Cache as well*/
	rq->caching = 1;

	/* Send a mailbox msg to PF to config RQ */
	mbx.rq.msg = NIC_MBOX_MSG_RQ_CFG;
	mbx.rq.qs_num = qs->vnic_id;
	mbx.rq.rq_num = qidx;
	mbx.rq.cfg = (rq->caching << 26) | (rq->cq_qs << 19) |
	    (rq->cq_idx << 16) | (rq->cont_rbdr_qs << 9) |
	    (rq->cont_qs_rbdr_idx << 8) | (rq->start_rbdr_qs << 1) |
	    (rq->start_qs_rbdr_idx);
	nicvf_send_msg_to_pf(nic, &mbx);

	mbx.rq.msg = NIC_MBOX_MSG_RQ_BP_CFG;
	mbx.rq.cfg = (1UL << 63) | (1UL << 62) | (qs->vnic_id << 0);
	nicvf_send_msg_to_pf(nic, &mbx);

	/*
	 * RQ drop config
	 * Enable CQ drop to reserve sufficient CQEs for all tx packets
	 */
	mbx.rq.msg = NIC_MBOX_MSG_RQ_DROP_CFG;
	mbx.rq.cfg = (1UL << 62) | (RQ_CQ_DROP << 8);
	nicvf_send_msg_to_pf(nic, &mbx);

	nicvf_queue_reg_write(nic, NIC_QSET_RQ_GEN_CFG, 0, 0x00);

	/* Enable Receive queue */
	rq_cfg.ena = 1;
	rq_cfg.tcp_ena = 0;
	nicvf_queue_reg_write(nic, NIC_QSET_RQ_0_7_CFG, qidx,
	    *(uint64_t *)&rq_cfg);
}

/* Configures completion queue */
static void
nicvf_cmp_queue_config(struct nicvf *nic, struct queue_set *qs,
    int qidx, boolean_t enable)
{
	struct cmp_queue *cq;
	struct cq_cfg cq_cfg;

	cq = &qs->cq[qidx];
	cq->enable = enable;

	if (!cq->enable) {
		nicvf_reclaim_cmp_queue(nic, qs, qidx);
		return;
	}

	/* Reset completion queue */
	nicvf_queue_reg_write(nic, NIC_QSET_CQ_0_7_CFG, qidx, NICVF_CQ_RESET);

	/* Set completion queue base address */
	nicvf_queue_reg_write(nic, NIC_QSET_CQ_0_7_BASE, qidx,
	    (uint64_t)(cq->dmem.phys_base));

	/* Enable Completion queue */
	cq_cfg.ena = 1;
	cq_cfg.reset = 0;
	cq_cfg.caching = 0;
	cq_cfg.qsize = CMP_QSIZE;
	cq_cfg.avg_con = 0;
	nicvf_queue_reg_write(nic, NIC_QSET_CQ_0_7_CFG, qidx, *(uint64_t *)&cq_cfg);

	/* Set threshold value for interrupt generation */
	nicvf_queue_reg_write(nic, NIC_QSET_CQ_0_7_THRESH, qidx, cq->thresh);
	nicvf_queue_reg_write(nic, NIC_QSET_CQ_0_7_CFG2, qidx,
	    nic->cq_coalesce_usecs);
}

/* Configures transmit queue */
static void
nicvf_snd_queue_config(struct nicvf *nic, struct queue_set *qs, int qidx,
    boolean_t enable)
{
	union nic_mbx mbx = {};
	struct snd_queue *sq;
	struct sq_cfg sq_cfg;

	sq = &qs->sq[qidx];
	sq->enable = enable;

	if (!sq->enable) {
		nicvf_reclaim_snd_queue(nic, qs, qidx);
		return;
	}

	/* Reset send queue */
	nicvf_queue_reg_write(nic, NIC_QSET_SQ_0_7_CFG, qidx, NICVF_SQ_RESET);

	sq->cq_qs = qs->vnic_id;
	sq->cq_idx = qidx;

	/* Send a mailbox msg to PF to config SQ */
	mbx.sq.msg = NIC_MBOX_MSG_SQ_CFG;
	mbx.sq.qs_num = qs->vnic_id;
	mbx.sq.sq_num = qidx;
	mbx.sq.sqs_mode = nic->sqs_mode;
	mbx.sq.cfg = (sq->cq_qs << 3) | sq->cq_idx;
	nicvf_send_msg_to_pf(nic, &mbx);

	/* Set queue base address */
	nicvf_queue_reg_write(nic, NIC_QSET_SQ_0_7_BASE, qidx,
	    (uint64_t)(sq->dmem.phys_base));

	/* Enable send queue  & set queue size */
	sq_cfg.ena = 1;
	sq_cfg.reset = 0;
	sq_cfg.ldwb = 0;
	sq_cfg.qsize = SND_QSIZE;
	sq_cfg.tstmp_bgx_intf = 0;
	nicvf_queue_reg_write(nic, NIC_QSET_SQ_0_7_CFG, qidx, *(uint64_t *)&sq_cfg);

	/* Set threshold value for interrupt generation */
	nicvf_queue_reg_write(nic, NIC_QSET_SQ_0_7_THRESH, qidx, sq->thresh);
}

/* Configures receive buffer descriptor ring */
static void
nicvf_rbdr_config(struct nicvf *nic, struct queue_set *qs, int qidx,
    boolean_t enable)
{
	struct rbdr *rbdr;
	struct rbdr_cfg rbdr_cfg;

	rbdr = &qs->rbdr[qidx];
	nicvf_reclaim_rbdr(nic, rbdr, qidx);
	if (!enable)
		return;

	/* Set descriptor base address */
	nicvf_queue_reg_write(nic, NIC_QSET_RBDR_0_1_BASE, qidx,
	    (uint64_t)(rbdr->dmem.phys_base));

	/* Enable RBDR  & set queue size */
	/* Buffer size should be in multiples of 128 bytes */
	rbdr_cfg.ena = 1;
	rbdr_cfg.reset = 0;
	rbdr_cfg.ldwb = 0;
	rbdr_cfg.qsize = RBDR_SIZE;
	rbdr_cfg.avg_con = 0;
	rbdr_cfg.lines = rbdr->dma_size / 128;
	nicvf_queue_reg_write(nic, NIC_QSET_RBDR_0_1_CFG, qidx,
	    *(uint64_t *)&rbdr_cfg);

	/* Notify HW */
	nicvf_queue_reg_write(nic, NIC_QSET_RBDR_0_1_DOOR, qidx,
	    qs->rbdr_len - 1);

	/* Set threshold value for interrupt generation */
	nicvf_queue_reg_write(nic, NIC_QSET_RBDR_0_1_THRESH, qidx,
	    rbdr->thresh - 1);
}

/* Requests PF to assign and enable Qset */
void
nicvf_qset_config(struct nicvf *nic, boolean_t enable)
{
	union nic_mbx mbx = {};
	struct queue_set *qs;
	struct qs_cfg *qs_cfg;

	qs = nic->qs;
	if (qs == NULL) {
		device_printf(nic->dev,
		    "Qset is still not allocated, don't init queues\n");
		return;
	}

	qs->enable = enable;
	qs->vnic_id = nic->vf_id;

	/* Send a mailbox msg to PF to config Qset */
	mbx.qs.msg = NIC_MBOX_MSG_QS_CFG;
	mbx.qs.num = qs->vnic_id;

	mbx.qs.cfg = 0;
	qs_cfg = (struct qs_cfg *)&mbx.qs.cfg;
	if (qs->enable) {
		qs_cfg->ena = 1;
		qs_cfg->vnic = qs->vnic_id;
	}
	nicvf_send_msg_to_pf(nic, &mbx);
}

static void
nicvf_free_resources(struct nicvf *nic)
{
	int qidx;
	struct queue_set *qs;

	qs = nic->qs;
	/*
	 * Remove QS error task first since it has to be dead
	 * to safely free completion queue tasks.
	 */
	if (qs->qs_err_taskq != NULL) {
		/* Shut down QS error tasks */
		while (taskqueue_cancel(qs->qs_err_taskq,
		    &qs->qs_err_task,  NULL) != 0) {
			taskqueue_drain(qs->qs_err_taskq, &qs->qs_err_task);

		}
		taskqueue_free(qs->qs_err_taskq);
		qs->qs_err_taskq = NULL;
	}
	/* Free receive buffer descriptor ring */
	for (qidx = 0; qidx < qs->rbdr_cnt; qidx++)
		nicvf_free_rbdr(nic, &qs->rbdr[qidx]);

	/* Free completion queue */
	for (qidx = 0; qidx < qs->cq_cnt; qidx++)
		nicvf_free_cmp_queue(nic, &qs->cq[qidx]);

	/* Free send queue */
	for (qidx = 0; qidx < qs->sq_cnt; qidx++)
		nicvf_free_snd_queue(nic, &qs->sq[qidx]);
}

static int
nicvf_alloc_resources(struct nicvf *nic)
{
	struct queue_set *qs = nic->qs;
	int qidx;

	/* Alloc receive buffer descriptor ring */
	for (qidx = 0; qidx < qs->rbdr_cnt; qidx++) {
		if (nicvf_init_rbdr(nic, &qs->rbdr[qidx], qs->rbdr_len,
				    DMA_BUFFER_LEN, qidx))
			goto alloc_fail;
	}

	/* Alloc send queue */
	for (qidx = 0; qidx < qs->sq_cnt; qidx++) {
		if (nicvf_init_snd_queue(nic, &qs->sq[qidx], qs->sq_len, qidx))
			goto alloc_fail;
	}

	/* Alloc completion queue */
	for (qidx = 0; qidx < qs->cq_cnt; qidx++) {
		if (nicvf_init_cmp_queue(nic, &qs->cq[qidx], qs->cq_len, qidx))
			goto alloc_fail;
	}

	/* Allocate QS error taskqueue */
	TASK_INIT(&qs->qs_err_task, 0, nicvf_qs_err_task, nic);
	qs->qs_err_taskq = taskqueue_create_fast("nicvf_qs_err_taskq", M_WAITOK,
	    taskqueue_thread_enqueue, &qs->qs_err_taskq);
	taskqueue_start_threads(&qs->qs_err_taskq, 1, PI_NET, "%s: qs_taskq",
	    device_get_nameunit(nic->dev));

	return (0);
alloc_fail:
	nicvf_free_resources(nic);
	return (ENOMEM);
}

int
nicvf_set_qset_resources(struct nicvf *nic)
{
	struct queue_set *qs;

	qs = malloc(sizeof(*qs), M_NICVF, (M_ZERO | M_WAITOK));
	nic->qs = qs;

	/* Set count of each queue */
	qs->rbdr_cnt = RBDR_CNT;
	qs->rq_cnt = RCV_QUEUE_CNT;

	qs->sq_cnt = SND_QUEUE_CNT;
	qs->cq_cnt = CMP_QUEUE_CNT;

	/* Set queue lengths */
	qs->rbdr_len = RCV_BUF_COUNT;
	qs->sq_len = SND_QUEUE_LEN;
	qs->cq_len = CMP_QUEUE_LEN;

	nic->rx_queues = qs->rq_cnt;
	nic->tx_queues = qs->sq_cnt;

	return (0);
}

int
nicvf_config_data_transfer(struct nicvf *nic, boolean_t enable)
{
	boolean_t disable = FALSE;
	struct queue_set *qs;
	int qidx;

	qs = nic->qs;
	if (qs == NULL)
		return (0);

	if (enable) {
		if (nicvf_alloc_resources(nic) != 0)
			return (ENOMEM);

		for (qidx = 0; qidx < qs->sq_cnt; qidx++)
			nicvf_snd_queue_config(nic, qs, qidx, enable);
		for (qidx = 0; qidx < qs->cq_cnt; qidx++)
			nicvf_cmp_queue_config(nic, qs, qidx, enable);
		for (qidx = 0; qidx < qs->rbdr_cnt; qidx++)
			nicvf_rbdr_config(nic, qs, qidx, enable);
		for (qidx = 0; qidx < qs->rq_cnt; qidx++)
			nicvf_rcv_queue_config(nic, qs, qidx, enable);
	} else {
		for (qidx = 0; qidx < qs->rq_cnt; qidx++)
			nicvf_rcv_queue_config(nic, qs, qidx, disable);
		for (qidx = 0; qidx < qs->rbdr_cnt; qidx++)
			nicvf_rbdr_config(nic, qs, qidx, disable);
		for (qidx = 0; qidx < qs->sq_cnt; qidx++)
			nicvf_snd_queue_config(nic, qs, qidx, disable);
		for (qidx = 0; qidx < qs->cq_cnt; qidx++)
			nicvf_cmp_queue_config(nic, qs, qidx, disable);

		nicvf_free_resources(nic);
	}

	return (0);
}

/*
 * Get a free desc from SQ
 * returns descriptor ponter & descriptor number
 */
static __inline int
nicvf_get_sq_desc(struct snd_queue *sq, int desc_cnt)
{
	int qentry;

	qentry = sq->tail;
	atomic_subtract_int(&sq->free_cnt, desc_cnt);
	sq->tail += desc_cnt;
	sq->tail &= (sq->dmem.q_len - 1);

	return (qentry);
}

/* Free descriptor back to SQ for future use */
static void
nicvf_put_sq_desc(struct snd_queue *sq, int desc_cnt)
{

	atomic_add_int(&sq->free_cnt, desc_cnt);
	sq->head += desc_cnt;
	sq->head &= (sq->dmem.q_len - 1);
}

static __inline int
nicvf_get_nxt_sqentry(struct snd_queue *sq, int qentry)
{
	qentry++;
	qentry &= (sq->dmem.q_len - 1);
	return (qentry);
}

static void
nicvf_sq_enable(struct nicvf *nic, struct snd_queue *sq, int qidx)
{
	uint64_t sq_cfg;

	sq_cfg = nicvf_queue_reg_read(nic, NIC_QSET_SQ_0_7_CFG, qidx);
	sq_cfg |= NICVF_SQ_EN;
	nicvf_queue_reg_write(nic, NIC_QSET_SQ_0_7_CFG, qidx, sq_cfg);
	/* Ring doorbell so that H/W restarts processing SQEs */
	nicvf_queue_reg_write(nic, NIC_QSET_SQ_0_7_DOOR, qidx, 0);
}

static void
nicvf_sq_disable(struct nicvf *nic, int qidx)
{
	uint64_t sq_cfg;

	sq_cfg = nicvf_queue_reg_read(nic, NIC_QSET_SQ_0_7_CFG, qidx);
	sq_cfg &= ~NICVF_SQ_EN;
	nicvf_queue_reg_write(nic, NIC_QSET_SQ_0_7_CFG, qidx, sq_cfg);
}

static void
nicvf_sq_free_used_descs(struct nicvf *nic, struct snd_queue *sq, int qidx)
{
	uint64_t head;
	struct snd_buff *snd_buff;
	struct sq_hdr_subdesc *hdr;

	NICVF_TX_LOCK(sq);
	head = nicvf_queue_reg_read(nic, NIC_QSET_SQ_0_7_HEAD, qidx) >> 4;
	while (sq->head != head) {
		hdr = (struct sq_hdr_subdesc *)GET_SQ_DESC(sq, sq->head);
		if (hdr->subdesc_type != SQ_DESC_TYPE_HEADER) {
			nicvf_put_sq_desc(sq, 1);
			continue;
		}
		snd_buff = &sq->snd_buff[sq->head];
		if (snd_buff->mbuf != NULL) {
			bus_dmamap_unload(sq->snd_buff_dmat, snd_buff->dmap);
			m_freem(snd_buff->mbuf);
			sq->snd_buff[sq->head].mbuf = NULL;
		}
		nicvf_put_sq_desc(sq, hdr->subdesc_cnt + 1);
	}
	NICVF_TX_UNLOCK(sq);
}

/*
 * Add SQ HEADER subdescriptor.
 * First subdescriptor for every send descriptor.
 */
static __inline int
nicvf_sq_add_hdr_subdesc(struct snd_queue *sq, int qentry,
			 int subdesc_cnt, struct mbuf *mbuf, int len)
{
	struct nicvf *nic;
	struct sq_hdr_subdesc *hdr;
	struct ether_vlan_header *eh;
#ifdef INET
	struct ip *ip;
	struct tcphdr *th;
#endif
	uint16_t etype;
	int ehdrlen, iphlen, poff, proto;

	nic = sq->nic;

	hdr = (struct sq_hdr_subdesc *)GET_SQ_DESC(sq, qentry);
	sq->snd_buff[qentry].mbuf = mbuf;

	memset(hdr, 0, SND_QUEUE_DESC_SIZE);
	hdr->subdesc_type = SQ_DESC_TYPE_HEADER;
	/* Enable notification via CQE after processing SQE */
	hdr->post_cqe = 1;
	/* No of subdescriptors following this */
	hdr->subdesc_cnt = subdesc_cnt;
	hdr->tot_len = len;

	eh = mtod(mbuf, struct ether_vlan_header *);
	if (eh->evl_encap_proto == htons(ETHERTYPE_VLAN)) {
		ehdrlen = ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN;
		etype = ntohs(eh->evl_proto);
	} else {
		ehdrlen = ETHER_HDR_LEN;
		etype = ntohs(eh->evl_encap_proto);
	}

	poff = proto = -1;
	switch (etype) {
#ifdef INET6
	case ETHERTYPE_IPV6:
		if (mbuf->m_len < ehdrlen + sizeof(struct ip6_hdr)) {
			mbuf = m_pullup(mbuf, ehdrlen +sizeof(struct ip6_hdr));
			sq->snd_buff[qentry].mbuf = NULL;
			if (mbuf == NULL)
				return (ENOBUFS);
		}
		poff = ip6_lasthdr(mbuf, ehdrlen, IPPROTO_IPV6, &proto);
		if (poff < 0)
			return (ENOBUFS);
		poff += ehdrlen;
		break;
#endif
#ifdef INET
	case ETHERTYPE_IP:
		if (mbuf->m_len < ehdrlen + sizeof(struct ip)) {
			mbuf = m_pullup(mbuf, ehdrlen + sizeof(struct ip));
			sq->snd_buff[qentry].mbuf = mbuf;
			if (mbuf == NULL)
				return (ENOBUFS);
		}
		if (mbuf->m_pkthdr.csum_flags & CSUM_IP)
			hdr->csum_l3 = 1; /* Enable IP csum calculation */

		ip = (struct ip *)(mbuf->m_data + ehdrlen);
		iphlen = ip->ip_hl << 2;
		poff = ehdrlen + iphlen;
		proto = ip->ip_p;
		break;
#endif
	}

#if defined(INET6) || defined(INET)
	if (poff > 0 && mbuf->m_pkthdr.csum_flags != 0) {
		switch (proto) {
		case IPPROTO_TCP:
			if ((mbuf->m_pkthdr.csum_flags & CSUM_TCP) == 0)
				break;

			if (mbuf->m_len < (poff + sizeof(struct tcphdr))) {
				mbuf = m_pullup(mbuf, poff + sizeof(struct tcphdr));
				sq->snd_buff[qentry].mbuf = mbuf;
				if (mbuf == NULL)
					return (ENOBUFS);
			}
			hdr->csum_l4 = SEND_L4_CSUM_TCP;
			break;
		case IPPROTO_UDP:
			if ((mbuf->m_pkthdr.csum_flags & CSUM_UDP) == 0)
				break;

			if (mbuf->m_len < (poff + sizeof(struct udphdr))) {
				mbuf = m_pullup(mbuf, poff + sizeof(struct udphdr));
				sq->snd_buff[qentry].mbuf = mbuf;
				if (mbuf == NULL)
					return (ENOBUFS);
			}
			hdr->csum_l4 = SEND_L4_CSUM_UDP;
			break;
		case IPPROTO_SCTP:
			if ((mbuf->m_pkthdr.csum_flags & CSUM_SCTP) == 0)
				break;

			if (mbuf->m_len < (poff + sizeof(struct sctphdr))) {
				mbuf = m_pullup(mbuf, poff + sizeof(struct sctphdr));
				sq->snd_buff[qentry].mbuf = mbuf;
				if (mbuf == NULL)
					return (ENOBUFS);
			}
			hdr->csum_l4 = SEND_L4_CSUM_SCTP;
			break;
		default:
			break;
		}
		hdr->l3_offset = ehdrlen;
		hdr->l4_offset = poff;
	}

	if ((mbuf->m_pkthdr.tso_segsz != 0) && nic->hw_tso) {
		th = (struct tcphdr *)((caddr_t)(mbuf->m_data + poff));

		hdr->tso = 1;
		hdr->tso_start = poff + (th->th_off * 4);
		hdr->tso_max_paysize = mbuf->m_pkthdr.tso_segsz;
		hdr->inner_l3_offset = ehdrlen - 2;
		nic->drv_stats.tx_tso++;
	}
#endif

	return (0);
}

/*
 * SQ GATHER subdescriptor
 * Must follow HDR descriptor
 */
static inline void nicvf_sq_add_gather_subdesc(struct snd_queue *sq, int qentry,
					       int size, uint64_t data)
{
	struct sq_gather_subdesc *gather;

	qentry &= (sq->dmem.q_len - 1);
	gather = (struct sq_gather_subdesc *)GET_SQ_DESC(sq, qentry);

	memset(gather, 0, SND_QUEUE_DESC_SIZE);
	gather->subdesc_type = SQ_DESC_TYPE_GATHER;
	gather->ld_type = NIC_SEND_LD_TYPE_E_LDD;
	gather->size = size;
	gather->addr = data;
}

/* Put an mbuf to a SQ for packet transfer. */
static int
nicvf_tx_mbuf_locked(struct snd_queue *sq, struct mbuf **mbufp)
{
	bus_dma_segment_t segs[256];
	struct snd_buff *snd_buff;
	size_t seg;
	int nsegs, qentry;
	int subdesc_cnt;
	int err;

	NICVF_TX_LOCK_ASSERT(sq);

	if (sq->free_cnt == 0)
		return (ENOBUFS);

	snd_buff = &sq->snd_buff[sq->tail];

	err = bus_dmamap_load_mbuf_sg(sq->snd_buff_dmat, snd_buff->dmap,
	    *mbufp, segs, &nsegs, BUS_DMA_NOWAIT);
	if (__predict_false(err != 0)) {
		/* ARM64TODO: Add mbuf defragmenting if we lack maps */
		m_freem(*mbufp);
		*mbufp = NULL;
		return (err);
	}

	/* Set how many subdescriptors is required */
	subdesc_cnt = MIN_SQ_DESC_PER_PKT_XMIT + nsegs - 1;
	if (subdesc_cnt > sq->free_cnt) {
		/* ARM64TODO: Add mbuf defragmentation if we lack descriptors */
		bus_dmamap_unload(sq->snd_buff_dmat, snd_buff->dmap);
		return (ENOBUFS);
	}

	qentry = nicvf_get_sq_desc(sq, subdesc_cnt);

	/* Add SQ header subdesc */
	err = nicvf_sq_add_hdr_subdesc(sq, qentry, subdesc_cnt - 1, *mbufp,
	    (*mbufp)->m_pkthdr.len);
	if (err != 0) {
		nicvf_put_sq_desc(sq, subdesc_cnt);
		bus_dmamap_unload(sq->snd_buff_dmat, snd_buff->dmap);
		if (err == ENOBUFS) {
			m_freem(*mbufp);
			*mbufp = NULL;
		}
		return (err);
	}

	/* Add SQ gather subdescs */
	for (seg = 0; seg < nsegs; seg++) {
		qentry = nicvf_get_nxt_sqentry(sq, qentry);
		nicvf_sq_add_gather_subdesc(sq, qentry, segs[seg].ds_len,
		    segs[seg].ds_addr);
	}

	/* make sure all memory stores are done before ringing doorbell */
	bus_dmamap_sync(sq->dmem.dmat, sq->dmem.dmap, BUS_DMASYNC_PREWRITE);

	dprintf(sq->nic->dev, "%s: sq->idx: %d, subdesc_cnt: %d\n",
	    __func__, sq->idx, subdesc_cnt);
	/* Inform HW to xmit new packet */
	nicvf_queue_reg_write(sq->nic, NIC_QSET_SQ_0_7_DOOR,
	    sq->idx, subdesc_cnt);
	return (0);
}

static __inline u_int
frag_num(u_int i)
{
#if BYTE_ORDER == BIG_ENDIAN
	return ((i & ~3) + 3 - (i & 3));
#else
	return (i);
#endif
}

/* Returns MBUF for a received packet */
struct mbuf *
nicvf_get_rcv_mbuf(struct nicvf *nic, struct cqe_rx_t *cqe_rx)
{
	int frag;
	int payload_len = 0;
	struct mbuf *mbuf;
	struct mbuf *mbuf_frag;
	uint16_t *rb_lens = NULL;
	uint64_t *rb_ptrs = NULL;

	mbuf = NULL;
	rb_lens = (uint16_t *)((uint8_t *)cqe_rx + (3 * sizeof(uint64_t)));
	rb_ptrs = (uint64_t *)((uint8_t *)cqe_rx + (6 * sizeof(uint64_t)));

	dprintf(nic->dev, "%s rb_cnt %d rb0_ptr %lx rb0_sz %d\n",
	    __func__, cqe_rx->rb_cnt, cqe_rx->rb0_ptr, cqe_rx->rb0_sz);

	for (frag = 0; frag < cqe_rx->rb_cnt; frag++) {
		payload_len = rb_lens[frag_num(frag)];
		if (frag == 0) {
			/* First fragment */
			mbuf = nicvf_rb_ptr_to_mbuf(nic,
			    (*rb_ptrs - cqe_rx->align_pad));
			mbuf->m_len = payload_len;
			mbuf->m_data += cqe_rx->align_pad;
			if_setrcvif(mbuf, nic->ifp);
		} else {
			/* Add fragments */
			mbuf_frag = nicvf_rb_ptr_to_mbuf(nic, *rb_ptrs);
			m_append(mbuf, payload_len, mbuf_frag->m_data);
			m_freem(mbuf_frag);
		}
		/* Next buffer pointer */
		rb_ptrs++;
	}

	if (__predict_true(mbuf != NULL)) {
		m_fixhdr(mbuf);
		mbuf->m_pkthdr.flowid = cqe_rx->rq_idx;
		M_HASHTYPE_SET(mbuf, M_HASHTYPE_OPAQUE);
		if (__predict_true((if_getcapenable(nic->ifp) & IFCAP_RXCSUM) != 0)) {
			/*
			 * HW by default verifies IP & TCP/UDP/SCTP checksums
			 */
			if (__predict_true(cqe_rx->l3_type == L3TYPE_IPV4)) {
				mbuf->m_pkthdr.csum_flags =
				    (CSUM_IP_CHECKED | CSUM_IP_VALID);
			}

			switch (cqe_rx->l4_type) {
			case L4TYPE_UDP:
			case L4TYPE_TCP: /* fall through */
				mbuf->m_pkthdr.csum_flags |=
				    (CSUM_DATA_VALID | CSUM_PSEUDO_HDR);
				mbuf->m_pkthdr.csum_data = 0xffff;
				break;
			case L4TYPE_SCTP:
				mbuf->m_pkthdr.csum_flags |= CSUM_SCTP_VALID;
				break;
			default:
				break;
			}
		}
	}

	return (mbuf);
}

/* Enable interrupt */
void
nicvf_enable_intr(struct nicvf *nic, int int_type, int q_idx)
{
	uint64_t reg_val;

	reg_val = nicvf_reg_read(nic, NIC_VF_ENA_W1S);

	switch (int_type) {
	case NICVF_INTR_CQ:
		reg_val |= ((1UL << q_idx) << NICVF_INTR_CQ_SHIFT);
		break;
	case NICVF_INTR_SQ:
		reg_val |= ((1UL << q_idx) << NICVF_INTR_SQ_SHIFT);
		break;
	case NICVF_INTR_RBDR:
		reg_val |= ((1UL << q_idx) << NICVF_INTR_RBDR_SHIFT);
		break;
	case NICVF_INTR_PKT_DROP:
		reg_val |= (1UL << NICVF_INTR_PKT_DROP_SHIFT);
		break;
	case NICVF_INTR_TCP_TIMER:
		reg_val |= (1UL << NICVF_INTR_TCP_TIMER_SHIFT);
		break;
	case NICVF_INTR_MBOX:
		reg_val |= (1UL << NICVF_INTR_MBOX_SHIFT);
		break;
	case NICVF_INTR_QS_ERR:
		reg_val |= (1UL << NICVF_INTR_QS_ERR_SHIFT);
		break;
	default:
		device_printf(nic->dev,
			   "Failed to enable interrupt: unknown type\n");
		break;
	}

	nicvf_reg_write(nic, NIC_VF_ENA_W1S, reg_val);
}

/* Disable interrupt */
void
nicvf_disable_intr(struct nicvf *nic, int int_type, int q_idx)
{
	uint64_t reg_val = 0;

	switch (int_type) {
	case NICVF_INTR_CQ:
		reg_val |= ((1UL << q_idx) << NICVF_INTR_CQ_SHIFT);
		break;
	case NICVF_INTR_SQ:
		reg_val |= ((1UL << q_idx) << NICVF_INTR_SQ_SHIFT);
		break;
	case NICVF_INTR_RBDR:
		reg_val |= ((1UL << q_idx) << NICVF_INTR_RBDR_SHIFT);
		break;
	case NICVF_INTR_PKT_DROP:
		reg_val |= (1UL << NICVF_INTR_PKT_DROP_SHIFT);
		break;
	case NICVF_INTR_TCP_TIMER:
		reg_val |= (1UL << NICVF_INTR_TCP_TIMER_SHIFT);
		break;
	case NICVF_INTR_MBOX:
		reg_val |= (1UL << NICVF_INTR_MBOX_SHIFT);
		break;
	case NICVF_INTR_QS_ERR:
		reg_val |= (1UL << NICVF_INTR_QS_ERR_SHIFT);
		break;
	default:
		device_printf(nic->dev,
			   "Failed to disable interrupt: unknown type\n");
		break;
	}

	nicvf_reg_write(nic, NIC_VF_ENA_W1C, reg_val);
}

/* Clear interrupt */
void
nicvf_clear_intr(struct nicvf *nic, int int_type, int q_idx)
{
	uint64_t reg_val = 0;

	switch (int_type) {
	case NICVF_INTR_CQ:
		reg_val = ((1UL << q_idx) << NICVF_INTR_CQ_SHIFT);
		break;
	case NICVF_INTR_SQ:
		reg_val = ((1UL << q_idx) << NICVF_INTR_SQ_SHIFT);
		break;
	case NICVF_INTR_RBDR:
		reg_val = ((1UL << q_idx) << NICVF_INTR_RBDR_SHIFT);
		break;
	case NICVF_INTR_PKT_DROP:
		reg_val = (1UL << NICVF_INTR_PKT_DROP_SHIFT);
		break;
	case NICVF_INTR_TCP_TIMER:
		reg_val = (1UL << NICVF_INTR_TCP_TIMER_SHIFT);
		break;
	case NICVF_INTR_MBOX:
		reg_val = (1UL << NICVF_INTR_MBOX_SHIFT);
		break;
	case NICVF_INTR_QS_ERR:
		reg_val |= (1UL << NICVF_INTR_QS_ERR_SHIFT);
		break;
	default:
		device_printf(nic->dev,
			   "Failed to clear interrupt: unknown type\n");
		break;
	}

	nicvf_reg_write(nic, NIC_VF_INT, reg_val);
}

/* Check if interrupt is enabled */
int
nicvf_is_intr_enabled(struct nicvf *nic, int int_type, int q_idx)
{
	uint64_t reg_val;
	uint64_t mask = 0xff;

	reg_val = nicvf_reg_read(nic, NIC_VF_ENA_W1S);

	switch (int_type) {
	case NICVF_INTR_CQ:
		mask = ((1UL << q_idx) << NICVF_INTR_CQ_SHIFT);
		break;
	case NICVF_INTR_SQ:
		mask = ((1UL << q_idx) << NICVF_INTR_SQ_SHIFT);
		break;
	case NICVF_INTR_RBDR:
		mask = ((1UL << q_idx) << NICVF_INTR_RBDR_SHIFT);
		break;
	case NICVF_INTR_PKT_DROP:
		mask = NICVF_INTR_PKT_DROP_MASK;
		break;
	case NICVF_INTR_TCP_TIMER:
		mask = NICVF_INTR_TCP_TIMER_MASK;
		break;
	case NICVF_INTR_MBOX:
		mask = NICVF_INTR_MBOX_MASK;
		break;
	case NICVF_INTR_QS_ERR:
		mask = NICVF_INTR_QS_ERR_MASK;
		break;
	default:
		device_printf(nic->dev,
			   "Failed to check interrupt enable: unknown type\n");
		break;
	}

	return (reg_val & mask);
}

void
nicvf_update_rq_stats(struct nicvf *nic, int rq_idx)
{
	struct rcv_queue *rq;

#define GET_RQ_STATS(reg) \
	nicvf_reg_read(nic, NIC_QSET_RQ_0_7_STAT_0_1 |\
			    (rq_idx << NIC_Q_NUM_SHIFT) | (reg << 3))

	rq = &nic->qs->rq[rq_idx];
	rq->stats.bytes = GET_RQ_STATS(RQ_SQ_STATS_OCTS);
	rq->stats.pkts = GET_RQ_STATS(RQ_SQ_STATS_PKTS);
}

void
nicvf_update_sq_stats(struct nicvf *nic, int sq_idx)
{
	struct snd_queue *sq;

#define GET_SQ_STATS(reg) \
	nicvf_reg_read(nic, NIC_QSET_SQ_0_7_STAT_0_1 |\
			    (sq_idx << NIC_Q_NUM_SHIFT) | (reg << 3))

	sq = &nic->qs->sq[sq_idx];
	sq->stats.bytes = GET_SQ_STATS(RQ_SQ_STATS_OCTS);
	sq->stats.pkts = GET_SQ_STATS(RQ_SQ_STATS_PKTS);
}

/* Check for errors in the receive cmp.queue entry */
int
nicvf_check_cqe_rx_errs(struct nicvf *nic, struct cmp_queue *cq,
    struct cqe_rx_t *cqe_rx)
{
	struct nicvf_hw_stats *stats = &nic->hw_stats;
	struct nicvf_drv_stats *drv_stats = &nic->drv_stats;

	if (!cqe_rx->err_level && !cqe_rx->err_opcode) {
		drv_stats->rx_frames_ok++;
		return (0);
	}

	switch (cqe_rx->err_opcode) {
	case CQ_RX_ERROP_RE_PARTIAL:
		stats->rx_bgx_truncated_pkts++;
		break;
	case CQ_RX_ERROP_RE_JABBER:
		stats->rx_jabber_errs++;
		break;
	case CQ_RX_ERROP_RE_FCS:
		stats->rx_fcs_errs++;
		break;
	case CQ_RX_ERROP_RE_RX_CTL:
		stats->rx_bgx_errs++;
		break;
	case CQ_RX_ERROP_PREL2_ERR:
		stats->rx_prel2_errs++;
		break;
	case CQ_RX_ERROP_L2_MAL:
		stats->rx_l2_hdr_malformed++;
		break;
	case CQ_RX_ERROP_L2_OVERSIZE:
		stats->rx_oversize++;
		break;
	case CQ_RX_ERROP_L2_UNDERSIZE:
		stats->rx_undersize++;
		break;
	case CQ_RX_ERROP_L2_LENMISM:
		stats->rx_l2_len_mismatch++;
		break;
	case CQ_RX_ERROP_L2_PCLP:
		stats->rx_l2_pclp++;
		break;
	case CQ_RX_ERROP_IP_NOT:
		stats->rx_ip_ver_errs++;
		break;
	case CQ_RX_ERROP_IP_CSUM_ERR:
		stats->rx_ip_csum_errs++;
		break;
	case CQ_RX_ERROP_IP_MAL:
		stats->rx_ip_hdr_malformed++;
		break;
	case CQ_RX_ERROP_IP_MALD:
		stats->rx_ip_payload_malformed++;
		break;
	case CQ_RX_ERROP_IP_HOP:
		stats->rx_ip_ttl_errs++;
		break;
	case CQ_RX_ERROP_L3_PCLP:
		stats->rx_l3_pclp++;
		break;
	case CQ_RX_ERROP_L4_MAL:
		stats->rx_l4_malformed++;
		break;
	case CQ_RX_ERROP_L4_CHK:
		stats->rx_l4_csum_errs++;
		break;
	case CQ_RX_ERROP_UDP_LEN:
		stats->rx_udp_len_errs++;
		break;
	case CQ_RX_ERROP_L4_PORT:
		stats->rx_l4_port_errs++;
		break;
	case CQ_RX_ERROP_TCP_FLAG:
		stats->rx_tcp_flag_errs++;
		break;
	case CQ_RX_ERROP_TCP_OFFSET:
		stats->rx_tcp_offset_errs++;
		break;
	case CQ_RX_ERROP_L4_PCLP:
		stats->rx_l4_pclp++;
		break;
	case CQ_RX_ERROP_RBDR_TRUNC:
		stats->rx_truncated_pkts++;
		break;
	}

	return (1);
}

/* Check for errors in the send cmp.queue entry */
int
nicvf_check_cqe_tx_errs(struct nicvf *nic, struct cmp_queue *cq,
    struct cqe_send_t *cqe_tx)
{
	struct cmp_queue_stats *stats = &cq->stats;

	switch (cqe_tx->send_status) {
	case CQ_TX_ERROP_GOOD:
		stats->tx.good++;
		return (0);
	case CQ_TX_ERROP_DESC_FAULT:
		stats->tx.desc_fault++;
		break;
	case CQ_TX_ERROP_HDR_CONS_ERR:
		stats->tx.hdr_cons_err++;
		break;
	case CQ_TX_ERROP_SUBDC_ERR:
		stats->tx.subdesc_err++;
		break;
	case CQ_TX_ERROP_IMM_SIZE_OFLOW:
		stats->tx.imm_size_oflow++;
		break;
	case CQ_TX_ERROP_DATA_SEQUENCE_ERR:
		stats->tx.data_seq_err++;
		break;
	case CQ_TX_ERROP_MEM_SEQUENCE_ERR:
		stats->tx.mem_seq_err++;
		break;
	case CQ_TX_ERROP_LOCK_VIOL:
		stats->tx.lock_viol++;
		break;
	case CQ_TX_ERROP_DATA_FAULT:
		stats->tx.data_fault++;
		break;
	case CQ_TX_ERROP_TSTMP_CONFLICT:
		stats->tx.tstmp_conflict++;
		break;
	case CQ_TX_ERROP_TSTMP_TIMEOUT:
		stats->tx.tstmp_timeout++;
		break;
	case CQ_TX_ERROP_MEM_FAULT:
		stats->tx.mem_fault++;
		break;
	case CQ_TX_ERROP_CK_OVERLAP:
		stats->tx.csum_overlap++;
		break;
	case CQ_TX_ERROP_CK_OFLOW:
		stats->tx.csum_overflow++;
		break;
	}

	return (1);
}
