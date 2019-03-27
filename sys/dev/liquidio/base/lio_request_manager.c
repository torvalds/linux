/*
 *   BSD LICENSE
 *
 *   Copyright(c) 2017 Cavium, Inc.. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Cavium, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER(S) OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*$FreeBSD$*/

#include "lio_bsd.h"
#include "lio_common.h"
#include "lio_droq.h"
#include "lio_iq.h"
#include "lio_response_manager.h"
#include "lio_device.h"
#include "lio_main.h"
#include "lio_network.h"
#include "cn23xx_pf_device.h"
#include "lio_rxtx.h"

struct lio_iq_post_status {
	int	status;
	int	index;
};

static void	lio_check_db_timeout(void *arg, int pending);
static void	__lio_check_db_timeout(struct octeon_device *oct,
				       uint64_t iq_no);

/* Return 0 on success, 1 on failure */
int
lio_init_instr_queue(struct octeon_device *oct, union octeon_txpciq txpciq,
		     uint32_t num_descs)
{
	struct lio_instr_queue	*iq;
	struct lio_iq_config	*conf = NULL;
	struct lio_tq		*db_tq;
	struct lio_request_list	*request_buf;
	bus_size_t		max_size;
	uint32_t		iq_no = (uint32_t)txpciq.s.q_no;
	uint32_t		q_size;
	int			error, i;

	if (LIO_CN23XX_PF(oct))
		conf = &(LIO_GET_IQ_CFG(LIO_CHIP_CONF(oct, cn23xx_pf)));
	if (conf == NULL) {
		lio_dev_err(oct, "Unsupported Chip %x\n", oct->chip_id);
		return (1);
	}

	q_size = (uint32_t)conf->instr_type * num_descs;
	iq = oct->instr_queue[iq_no];
	iq->oct_dev = oct;

	max_size = LIO_CN23XX_PKI_MAX_FRAME_SIZE * num_descs;

	error = bus_dma_tag_create(bus_get_dma_tag(oct->device),	/* parent */
				   1, 0,				/* alignment, bounds */
				   BUS_SPACE_MAXADDR,			/* lowaddr */
				   BUS_SPACE_MAXADDR,			/* highaddr */
				   NULL, NULL,				/* filter, filterarg */
				   max_size,				/* maxsize */
				   LIO_MAX_SG,				/* nsegments */
				   PAGE_SIZE,				/* maxsegsize */
				   0,					/* flags */
				   NULL,				/* lockfunc */
				   NULL,				/* lockfuncarg */
				   &iq->txtag);
	if (error) {
		lio_dev_err(oct, "Cannot allocate memory for instr queue %d\n",
			    iq_no);
		return (1);
	}

	iq->base_addr = lio_dma_alloc(q_size, (vm_paddr_t *)&iq->base_addr_dma);
	if (!iq->base_addr) {
		lio_dev_err(oct, "Cannot allocate memory for instr queue %d\n",
			    iq_no);
		return (1);
	}

	iq->max_count = num_descs;

	/*
	 * Initialize a list to holds requests that have been posted to
	 * Octeon but has yet to be fetched by octeon
	 */
	iq->request_list = malloc(sizeof(*iq->request_list) * num_descs,
				  M_DEVBUF, M_NOWAIT | M_ZERO);
	if (iq->request_list == NULL) {
		lio_dev_err(oct, "Alloc failed for IQ[%d] nr free list\n",
			    iq_no);
		return (1);
	}

	lio_dev_dbg(oct, "IQ[%d]: base: %p basedma: %llx count: %d\n",
		    iq_no, iq->base_addr, LIO_CAST64(iq->base_addr_dma),
		    iq->max_count);

	/* Create the descriptor buffer dma maps */
	request_buf = iq->request_list;
	for (i = 0; i < num_descs; i++, request_buf++) {
		error = bus_dmamap_create(iq->txtag, 0, &request_buf->map);
		if (error) {
			lio_dev_err(oct, "Unable to create TX DMA map\n");
			return (1);
		}
	}

	iq->txpciq.txpciq64 = txpciq.txpciq64;
	iq->fill_cnt = 0;
	iq->host_write_index = 0;
	iq->octeon_read_index = 0;
	iq->flush_index = 0;
	iq->last_db_time = 0;
	iq->db_timeout = (uint32_t)conf->db_timeout;
	atomic_store_rel_int(&iq->instr_pending, 0);

	/* Initialize the lock for this instruction queue */
	mtx_init(&iq->lock, "Tx_lock", NULL, MTX_DEF);
	mtx_init(&iq->post_lock, "iq_post_lock", NULL, MTX_DEF);
	mtx_init(&iq->enq_lock, "enq_lock", NULL, MTX_DEF);

	mtx_init(&iq->iq_flush_running_lock, "iq_flush_running_lock", NULL,
		 MTX_DEF);

	oct->io_qmask.iq |= BIT_ULL(iq_no);

	/* Set the 32B/64B mode for each input queue */
	oct->io_qmask.iq64B |= ((conf->instr_type == 64) << iq_no);
	iq->iqcmd_64B = (conf->instr_type == 64);

	oct->fn_list.setup_iq_regs(oct, iq_no);

	db_tq = &oct->check_db_tq[iq_no];
	db_tq->tq = taskqueue_create("lio_check_db_timeout", M_WAITOK,
				     taskqueue_thread_enqueue, &db_tq->tq);
	if (db_tq->tq == NULL) {
		lio_dev_err(oct, "check db wq create failed for iq %d\n",
			    iq_no);
		return (1);
	}

	TIMEOUT_TASK_INIT(db_tq->tq, &db_tq->work, 0, lio_check_db_timeout,
			  (void *)db_tq);
	db_tq->ctxul = iq_no;
	db_tq->ctxptr = oct;

	taskqueue_start_threads(&db_tq->tq, 1, PI_NET,
				"lio%d_check_db_timeout:%d",
				oct->octeon_id, iq_no);
	taskqueue_enqueue_timeout(db_tq->tq, &db_tq->work, 1);

	/* Allocate a buf ring */
	oct->instr_queue[iq_no]->br =
		buf_ring_alloc(LIO_BR_SIZE, M_DEVBUF, M_WAITOK,
			       &oct->instr_queue[iq_no]->enq_lock);
	if (oct->instr_queue[iq_no]->br == NULL) {
		lio_dev_err(oct, "Critical Failure setting up buf ring\n");
		return (1);
	}

	return (0);
}

int
lio_delete_instr_queue(struct octeon_device *oct, uint32_t iq_no)
{
	struct lio_instr_queue		*iq = oct->instr_queue[iq_no];
	struct lio_request_list		*request_buf;
	struct lio_mbuf_free_info	*finfo;
	uint64_t			desc_size = 0, q_size;
	int				i;

	lio_dev_dbg(oct, "%s[%d]\n", __func__, iq_no);

	if (oct->check_db_tq[iq_no].tq != NULL) {
		while (taskqueue_cancel_timeout(oct->check_db_tq[iq_no].tq,
						&oct->check_db_tq[iq_no].work,
						NULL))
			taskqueue_drain_timeout(oct->check_db_tq[iq_no].tq,
						&oct->check_db_tq[iq_no].work);
		taskqueue_free(oct->check_db_tq[iq_no].tq);
		oct->check_db_tq[iq_no].tq = NULL;
	}

	if (LIO_CN23XX_PF(oct))
		desc_size =
		    LIO_GET_IQ_INSTR_TYPE_CFG(LIO_CHIP_CONF(oct, cn23xx_pf));

	request_buf = iq->request_list;
	for (i = 0; i < iq->max_count; i++, request_buf++) {
		if ((request_buf->reqtype == LIO_REQTYPE_NORESP_NET) ||
		    (request_buf->reqtype == LIO_REQTYPE_NORESP_NET_SG)) {
			if (request_buf->buf != NULL) {
				finfo = request_buf->buf;
				bus_dmamap_sync(iq->txtag, request_buf->map,
						BUS_DMASYNC_POSTWRITE);
				bus_dmamap_unload(iq->txtag,
						  request_buf->map);
				m_freem(finfo->mb);
				request_buf->buf = NULL;
				if (request_buf->map != NULL) {
					bus_dmamap_destroy(iq->txtag,
							   request_buf->map);
					request_buf->map = NULL;
				}
			} else if (request_buf->map != NULL) {
				bus_dmamap_unload(iq->txtag, request_buf->map);
				bus_dmamap_destroy(iq->txtag, request_buf->map);
				request_buf->map = NULL;
			}
		}
	}

	if (iq->br != NULL) {
		buf_ring_free(iq->br, M_DEVBUF);
		iq->br = NULL;
	}

	if (iq->request_list != NULL) {
		free(iq->request_list, M_DEVBUF);
		iq->request_list = NULL;
	}

	if (iq->txtag != NULL) {
		bus_dma_tag_destroy(iq->txtag);
		iq->txtag = NULL;
	}

	if (iq->base_addr) {
		q_size = iq->max_count * desc_size;
		lio_dma_free((uint32_t)q_size, iq->base_addr);

		oct->io_qmask.iq &= ~(1ULL << iq_no);
		bzero(oct->instr_queue[iq_no], sizeof(struct lio_instr_queue));
		oct->num_iqs--;

		return (0);
	}

	return (1);
}

/* Return 0 on success, 1 on failure */
int
lio_setup_iq(struct octeon_device *oct, int ifidx, int q_index,
	     union octeon_txpciq txpciq, uint32_t num_descs)
{
	uint32_t	iq_no = (uint32_t)txpciq.s.q_no;

	if (oct->instr_queue[iq_no]->oct_dev != NULL) {
		lio_dev_dbg(oct, "IQ is in use. Cannot create the IQ: %d again\n",
			    iq_no);
		oct->instr_queue[iq_no]->txpciq.txpciq64 = txpciq.txpciq64;
		return (0);
	}

	oct->instr_queue[iq_no]->q_index = q_index;
	oct->instr_queue[iq_no]->ifidx = ifidx;

	if (lio_init_instr_queue(oct, txpciq, num_descs)) {
		lio_delete_instr_queue(oct, iq_no);
		return (1);
	}

	oct->num_iqs++;
	if (oct->fn_list.enable_io_queues(oct))
		return (1);

	return (0);
}

int
lio_wait_for_instr_fetch(struct octeon_device *oct)
{
	int	i, retry = 1000, pending, instr_cnt = 0;

	do {
		instr_cnt = 0;

		for (i = 0; i < LIO_MAX_INSTR_QUEUES(oct); i++) {
			if (!(oct->io_qmask.iq & BIT_ULL(i)))
				continue;
			pending = atomic_load_acq_int(
					&oct->instr_queue[i]->instr_pending);
			if (pending)
				__lio_check_db_timeout(oct, i);
			instr_cnt += pending;
		}

		if (instr_cnt == 0)
			break;

		lio_sleep_timeout(1);

	} while (retry-- && instr_cnt);

	return (instr_cnt);
}

static inline void
lio_ring_doorbell(struct octeon_device *oct, struct lio_instr_queue *iq)
{

	if (atomic_load_acq_int(&oct->status) == LIO_DEV_RUNNING) {
		lio_write_csr32(oct, iq->doorbell_reg, iq->fill_cnt);
		/* make sure doorbell write goes through */
		__compiler_membar();
		iq->fill_cnt = 0;
		iq->last_db_time = ticks;
		return;
	}
}

static inline void
__lio_copy_cmd_into_iq(struct lio_instr_queue *iq, uint8_t *cmd)
{
	uint8_t	*iqptr, cmdsize;

	cmdsize = ((iq->iqcmd_64B) ? 64 : 32);
	iqptr = iq->base_addr + (cmdsize * iq->host_write_index);

	memcpy(iqptr, cmd, cmdsize);
}

static inline struct lio_iq_post_status
__lio_post_command2(struct lio_instr_queue *iq, uint8_t *cmd)
{
	struct lio_iq_post_status	st;

	st.status = LIO_IQ_SEND_OK;

	/*
	 * This ensures that the read index does not wrap around to the same
	 * position if queue gets full before Octeon could fetch any instr.
	 */
	if (atomic_load_acq_int(&iq->instr_pending) >=
	    (int32_t)(iq->max_count - 1)) {
		st.status = LIO_IQ_SEND_FAILED;
		st.index = -1;
		return (st);
	}

	if (atomic_load_acq_int(&iq->instr_pending) >=
	    (int32_t)(iq->max_count - 2))
		st.status = LIO_IQ_SEND_STOP;

	__lio_copy_cmd_into_iq(iq, cmd);

	/* "index" is returned, host_write_index is modified. */
	st.index = iq->host_write_index;
	iq->host_write_index = lio_incr_index(iq->host_write_index, 1,
					      iq->max_count);
	iq->fill_cnt++;

	/*
	 * Flush the command into memory. We need to be sure the data is in
	 * memory before indicating that the instruction is pending.
	 */
	wmb();

	atomic_add_int(&iq->instr_pending, 1);

	return (st);
}

static inline void
__lio_add_to_request_list(struct lio_instr_queue *iq, int idx, void *buf,
			  int reqtype)
{

	iq->request_list[idx].buf = buf;
	iq->request_list[idx].reqtype = reqtype;
}

/* Can only run in process context */
int
lio_process_iq_request_list(struct octeon_device *oct,
			    struct lio_instr_queue *iq, uint32_t budget)
{
	struct lio_soft_command		*sc;
	struct octeon_instr_irh		*irh = NULL;
	struct lio_mbuf_free_info	*finfo;
	void				*buf;
	uint32_t			inst_count = 0;
	uint32_t			old = iq->flush_index;
	int				reqtype;

	while (old != iq->octeon_read_index) {
		reqtype = iq->request_list[old].reqtype;
		buf = iq->request_list[old].buf;
		finfo = buf;

		if (reqtype == LIO_REQTYPE_NONE)
			goto skip_this;

		switch (reqtype) {
		case LIO_REQTYPE_NORESP_NET:
			lio_free_mbuf(iq, buf);
			break;
		case LIO_REQTYPE_NORESP_NET_SG:
			lio_free_sgmbuf(iq, buf);
			break;
		case LIO_REQTYPE_RESP_NET:
		case LIO_REQTYPE_SOFT_COMMAND:
			sc = buf;
			if (LIO_CN23XX_PF(oct))
				irh = (struct octeon_instr_irh *)
					&sc->cmd.cmd3.irh;
			if (irh->rflag) {
				/*
				 * We're expecting a response from Octeon.
				 * It's up to lio_process_ordered_list() to
				 * process  sc. Add sc to the ordered soft
				 * command response list because we expect
				 * a response from Octeon.
				 */
				mtx_lock(&oct->response_list
					 [LIO_ORDERED_SC_LIST].lock);
				atomic_add_int(&oct->response_list
					       [LIO_ORDERED_SC_LIST].
					       pending_req_count, 1);
				STAILQ_INSERT_TAIL(&oct->response_list
						   [LIO_ORDERED_SC_LIST].
						   head, &sc->node, entries);
				mtx_unlock(&oct->response_list
					   [LIO_ORDERED_SC_LIST].lock);
			} else {
				if (sc->callback != NULL) {
					/* This callback must not sleep */
					sc->callback(oct, LIO_REQUEST_DONE,
						     sc->callback_arg);
				}
			}

			break;
		default:
			lio_dev_err(oct, "%s Unknown reqtype: %d buf: %p at idx %d\n",
				    __func__, reqtype, buf, old);
		}

		iq->request_list[old].buf = NULL;
		iq->request_list[old].reqtype = 0;

skip_this:
		inst_count++;
		old = lio_incr_index(old, 1, iq->max_count);

		if ((budget) && (inst_count >= budget))
			break;
	}

	iq->flush_index = old;

	return (inst_count);
}

/* Can only be called from process context */
int
lio_flush_iq(struct octeon_device *oct, struct lio_instr_queue *iq,
	     uint32_t budget)
{
	uint32_t	inst_processed = 0;
	uint32_t	tot_inst_processed = 0;
	int		tx_done = 1;

	if (!mtx_trylock(&iq->iq_flush_running_lock))
		return (tx_done);

	mtx_lock(&iq->lock);

	iq->octeon_read_index = oct->fn_list.update_iq_read_idx(iq);

	do {
		/* Process any outstanding IQ packets. */
		if (iq->flush_index == iq->octeon_read_index)
			break;

		if (budget)
			inst_processed =
				lio_process_iq_request_list(oct, iq,
							    budget -
							    tot_inst_processed);
		else
			inst_processed =
				lio_process_iq_request_list(oct, iq, 0);

		if (inst_processed) {
			atomic_subtract_int(&iq->instr_pending, inst_processed);
			iq->stats.instr_processed += inst_processed;
		}
		tot_inst_processed += inst_processed;
		inst_processed = 0;

	} while (tot_inst_processed < budget);

	if (budget && (tot_inst_processed >= budget))
		tx_done = 0;

	iq->last_db_time = ticks;

	mtx_unlock(&iq->lock);

	mtx_unlock(&iq->iq_flush_running_lock);

	return (tx_done);
}

/*
 * Process instruction queue after timeout.
 * This routine gets called from a taskqueue or when removing the module.
 */
static void
__lio_check_db_timeout(struct octeon_device *oct, uint64_t iq_no)
{
	struct lio_instr_queue	*iq;
	uint64_t		next_time;

	if (oct == NULL)
		return;

	iq = oct->instr_queue[iq_no];
	if (iq == NULL)
		return;

	if (atomic_load_acq_int(&iq->instr_pending)) {
		/* If ticks - last_db_time < db_timeout do nothing  */
		next_time = iq->last_db_time + lio_ms_to_ticks(iq->db_timeout);
		if (!lio_check_timeout(ticks, next_time))
			return;

		iq->last_db_time = ticks;

		/* Flush the instruction queue */
		lio_flush_iq(oct, iq, 0);

		lio_enable_irq(NULL, iq);
	}

	if (oct->props.ifp != NULL && iq->br != NULL) {
		if (mtx_trylock(&iq->enq_lock)) {
			if (!drbr_empty(oct->props.ifp, iq->br))
				lio_mq_start_locked(oct->props.ifp, iq);

			mtx_unlock(&iq->enq_lock);
		}
	}
}

/*
 * Called by the Poll thread at regular intervals to check the instruction
 * queue for commands to be posted and for commands that were fetched by Octeon.
 */
static void
lio_check_db_timeout(void *arg, int pending)
{
	struct lio_tq		*db_tq = (struct lio_tq *)arg;
	struct octeon_device	*oct = db_tq->ctxptr;
	uint64_t		iq_no = db_tq->ctxul;
	uint32_t		delay = 10;

	__lio_check_db_timeout(oct, iq_no);
	taskqueue_enqueue_timeout(db_tq->tq, &db_tq->work,
				  lio_ms_to_ticks(delay));
}

int
lio_send_command(struct octeon_device *oct, uint32_t iq_no,
		 uint32_t force_db, void *cmd, void *buf,
		 uint32_t datasize, uint32_t reqtype)
{
	struct lio_iq_post_status	st;
	struct lio_instr_queue		*iq = oct->instr_queue[iq_no];

	/*
	 * Get the lock and prevent other tasks and tx interrupt handler
	 * from running.
	 */
	mtx_lock(&iq->post_lock);

	st = __lio_post_command2(iq, cmd);

	if (st.status != LIO_IQ_SEND_FAILED) {
		__lio_add_to_request_list(iq, st.index, buf, reqtype);
		LIO_INCR_INSTRQUEUE_PKT_COUNT(oct, iq_no, bytes_sent, datasize);
		LIO_INCR_INSTRQUEUE_PKT_COUNT(oct, iq_no, instr_posted, 1);

		if (force_db || (st.status == LIO_IQ_SEND_STOP))
			lio_ring_doorbell(oct, iq);
	} else {
		LIO_INCR_INSTRQUEUE_PKT_COUNT(oct, iq_no, instr_dropped, 1);
	}

	mtx_unlock(&iq->post_lock);

	/*
	 * This is only done here to expedite packets being flushed for
	 * cases where there are no IQ completion interrupts.
	 */

	return (st.status);
}

void
lio_prepare_soft_command(struct octeon_device *oct, struct lio_soft_command *sc,
			 uint8_t opcode, uint8_t subcode, uint32_t irh_ossp,
			 uint64_t ossp0, uint64_t ossp1)
{
	struct lio_config		*lio_cfg;
	struct octeon_instr_ih3		*ih3;
	struct octeon_instr_pki_ih3	*pki_ih3;
	struct octeon_instr_irh		*irh;
	struct octeon_instr_rdp		*rdp;

	KASSERT(opcode <= 15, ("%s, %d, opcode > 15", __func__, __LINE__));
	KASSERT(subcode <= 127, ("%s, %d, opcode > 127", __func__, __LINE__));

	lio_cfg = lio_get_conf(oct);

	if (LIO_CN23XX_PF(oct)) {
		ih3 = (struct octeon_instr_ih3 *)&sc->cmd.cmd3.ih3;

		ih3->pkind = oct->instr_queue[sc->iq_no]->txpciq.s.pkind;

		pki_ih3 = (struct octeon_instr_pki_ih3 *)&sc->cmd.cmd3.pki_ih3;

		pki_ih3->w = 1;
		pki_ih3->raw = 1;
		pki_ih3->utag = 1;
		pki_ih3->uqpg = oct->instr_queue[sc->iq_no]->txpciq.s.use_qpg;
		pki_ih3->utt = 1;
		pki_ih3->tag = LIO_CONTROL;
		pki_ih3->tagtype = LIO_ATOMIC_TAG;
		pki_ih3->qpg = oct->instr_queue[sc->iq_no]->txpciq.s.qpg;
		pki_ih3->pm = 0x7;
		pki_ih3->sl = 8;

		if (sc->datasize)
			ih3->dlengsz = sc->datasize;

		irh = (struct octeon_instr_irh *)&sc->cmd.cmd3.irh;
		irh->opcode = opcode;
		irh->subcode = subcode;

		/* opcode/subcode specific parameters (ossp) */
		irh->ossp = irh_ossp;
		sc->cmd.cmd3.ossp[0] = ossp0;
		sc->cmd.cmd3.ossp[1] = ossp1;

		if (sc->rdatasize) {
			rdp = (struct octeon_instr_rdp *)&sc->cmd.cmd3.rdp;
			rdp->pcie_port = oct->pcie_port;
			rdp->rlen = sc->rdatasize;

			irh->rflag = 1;
			/* PKI IH3 */
			/* pki_ih3 irh+ossp[0]+ossp[1]+rdp+rptr = 48 bytes */
			ih3->fsz = LIO_SOFTCMDRESP_IH3;
		} else {
			irh->rflag = 0;
			/* PKI IH3 */
			/* pki_h3 + irh + ossp[0] + ossp[1] = 32 bytes */
			ih3->fsz = LIO_PCICMD_O3;
		}
	}
}

int
lio_send_soft_command(struct octeon_device *oct, struct lio_soft_command *sc)
{
	struct octeon_instr_ih3	*ih3;
	struct octeon_instr_irh	*irh;
	uint32_t		len = 0;

	if (LIO_CN23XX_PF(oct)) {
		ih3 = (struct octeon_instr_ih3 *)&sc->cmd.cmd3.ih3;
		if (ih3->dlengsz) {
			KASSERT(sc->dmadptr, ("%s, %d, sc->dmadptr is NULL",
					      __func__, __LINE__));
			sc->cmd.cmd3.dptr = sc->dmadptr;
		}

		irh = (struct octeon_instr_irh *)&sc->cmd.cmd3.irh;
		if (irh->rflag) {
			KASSERT(sc->dmarptr, ("%s, %d, sc->dmarptr is NULL",
					      __func__, __LINE__));
			KASSERT(sc->status_word, ("%s, %d, sc->status_word is NULL",
						  __func__, __LINE__));
			*sc->status_word = COMPLETION_WORD_INIT;
			sc->cmd.cmd3.rptr = sc->dmarptr;
		}
		len = (uint32_t)ih3->dlengsz;
	}
	if (sc->wait_time)
		sc->timeout = ticks + lio_ms_to_ticks(sc->wait_time);

	return (lio_send_command(oct, sc->iq_no, 1, &sc->cmd, sc,
				 len, LIO_REQTYPE_SOFT_COMMAND));
}

int
lio_setup_sc_buffer_pool(struct octeon_device *oct)
{
	struct lio_soft_command	*sc;
	uint64_t		dma_addr;
	int			i;

	STAILQ_INIT(&oct->sc_buf_pool.head);
	mtx_init(&oct->sc_buf_pool.lock, "sc_pool_lock", NULL, MTX_DEF);
	atomic_store_rel_int(&oct->sc_buf_pool.alloc_buf_count, 0);

	for (i = 0; i < LIO_MAX_SOFT_COMMAND_BUFFERS; i++) {
		sc = (struct lio_soft_command *)
			lio_dma_alloc(LIO_SOFT_COMMAND_BUFFER_SIZE, (vm_paddr_t *)&dma_addr);
		if (sc == NULL) {
			lio_free_sc_buffer_pool(oct);
			return (1);
		}

		sc->dma_addr = dma_addr;
		sc->size = LIO_SOFT_COMMAND_BUFFER_SIZE;

		STAILQ_INSERT_TAIL(&oct->sc_buf_pool.head, &sc->node, entries);
	}

	return (0);
}

int
lio_free_sc_buffer_pool(struct octeon_device *oct)
{
	struct lio_stailq_node	*tmp, *tmp2;
	struct lio_soft_command	*sc;

	mtx_lock(&oct->sc_buf_pool.lock);

	STAILQ_FOREACH_SAFE(tmp, &oct->sc_buf_pool.head, entries, tmp2) {
		sc = LIO_STAILQ_FIRST_ENTRY(&oct->sc_buf_pool.head,
					    struct lio_soft_command, node);

		STAILQ_REMOVE_HEAD(&oct->sc_buf_pool.head, entries);

		lio_dma_free(sc->size, sc);
	}

	STAILQ_INIT(&oct->sc_buf_pool.head);

	mtx_unlock(&oct->sc_buf_pool.lock);

	return (0);
}

struct lio_soft_command *
lio_alloc_soft_command(struct octeon_device *oct, uint32_t datasize,
		       uint32_t rdatasize, uint32_t ctxsize)
{
	struct lio_soft_command	*sc = NULL;
	struct lio_stailq_node	*tmp;
	uint64_t		dma_addr;
	uint32_t		size;
	uint32_t		offset = sizeof(struct lio_soft_command);

	KASSERT((offset + datasize + rdatasize + ctxsize) <=
		LIO_SOFT_COMMAND_BUFFER_SIZE,
		("%s, %d, offset + datasize + rdatasize + ctxsize > LIO_SOFT_COMMAND_BUFFER_SIZE",
		 __func__, __LINE__));

	mtx_lock(&oct->sc_buf_pool.lock);

	if (STAILQ_EMPTY(&oct->sc_buf_pool.head)) {
		mtx_unlock(&oct->sc_buf_pool.lock);
		return (NULL);
	}
	tmp = STAILQ_LAST(&oct->sc_buf_pool.head, lio_stailq_node, entries);

	STAILQ_REMOVE(&oct->sc_buf_pool.head, tmp, lio_stailq_node, entries);

	atomic_add_int(&oct->sc_buf_pool.alloc_buf_count, 1);

	mtx_unlock(&oct->sc_buf_pool.lock);

	sc = (struct lio_soft_command *)tmp;

	dma_addr = sc->dma_addr;
	size = sc->size;

	bzero(sc, sc->size);

	sc->dma_addr = dma_addr;
	sc->size = size;

	if (ctxsize) {
		sc->ctxptr = (uint8_t *)sc + offset;
		sc->ctxsize = ctxsize;
	}

	/* Start data at 128 byte boundary */
	offset = (offset + ctxsize + 127) & 0xffffff80;

	if (datasize) {
		sc->virtdptr = (uint8_t *)sc + offset;
		sc->dmadptr = dma_addr + offset;
		sc->datasize = datasize;
	}
	/* Start rdata at 128 byte boundary */
	offset = (offset + datasize + 127) & 0xffffff80;

	if (rdatasize) {
		KASSERT(rdatasize >= 16, ("%s, %d, rdatasize < 16", __func__,
					  __LINE__));
		sc->virtrptr = (uint8_t *)sc + offset;
		sc->dmarptr = dma_addr + offset;
		sc->rdatasize = rdatasize;
		sc->status_word = (uint64_t *)((uint8_t *)(sc->virtrptr) +
					       rdatasize - 8);
	}
	return (sc);
}

void
lio_free_soft_command(struct octeon_device *oct,
		      struct lio_soft_command *sc)
{

	mtx_lock(&oct->sc_buf_pool.lock);

	STAILQ_INSERT_TAIL(&oct->sc_buf_pool.head, &sc->node, entries);

	atomic_subtract_int(&oct->sc_buf_pool.alloc_buf_count, 1);

	mtx_unlock(&oct->sc_buf_pool.lock);
}
