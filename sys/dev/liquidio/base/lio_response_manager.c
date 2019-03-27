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

static void	lio_poll_req_completion(void *arg, int pending);

int
lio_setup_response_list(struct octeon_device *oct)
{
	struct lio_tq	*ctq;
	int		i, ret = 0;

	for (i = 0; i < LIO_MAX_RESPONSE_LISTS; i++) {
		STAILQ_INIT(&oct->response_list[i].head);
		mtx_init(&oct->response_list[i].lock, "response_list_lock",
			 NULL, MTX_DEF);
		atomic_store_rel_int(&oct->response_list[i].pending_req_count,
				     0);
	}
	mtx_init(&oct->cmd_resp_wqlock, "cmd_resp_wqlock", NULL, MTX_DEF);

	ctq = &oct->dma_comp_tq;
	ctq->tq = taskqueue_create("lio_dma_comp", M_WAITOK,
				   taskqueue_thread_enqueue, &ctq->tq);
	if (ctq->tq == NULL) {
		lio_dev_err(oct, "failed to create wq thread\n");
		return (-ENOMEM);
	}

	TIMEOUT_TASK_INIT(ctq->tq, &ctq->work, 0, lio_poll_req_completion,
			  (void *)ctq);
	ctq->ctxptr = oct;

	oct->cmd_resp_state = LIO_DRV_ONLINE;
	taskqueue_start_threads(&ctq->tq, 1, PI_NET, "lio%d_dma_comp",
				oct->octeon_id);
	taskqueue_enqueue_timeout(ctq->tq, &ctq->work, lio_ms_to_ticks(50));

	return (ret);
}

void
lio_delete_response_list(struct octeon_device *oct)
{

	if (oct->dma_comp_tq.tq != NULL) {
		while (taskqueue_cancel_timeout(oct->dma_comp_tq.tq,
						&oct->dma_comp_tq.work, NULL))
			taskqueue_drain_timeout(oct->dma_comp_tq.tq,
						&oct->dma_comp_tq.work);
		taskqueue_free(oct->dma_comp_tq.tq);
		oct->dma_comp_tq.tq = NULL;
	}
}

int
lio_process_ordered_list(struct octeon_device *octeon_dev,
			 uint32_t force_quit)
{
	struct lio_response_list	*ordered_sc_list;
	struct lio_soft_command		*sc;
	uint64_t			status64;
	uint32_t			status;
	int				request_complete = 0;
	int				resp_to_process;

	resp_to_process = LIO_MAX_ORD_REQS_TO_PROCESS;

	ordered_sc_list = &octeon_dev->response_list[LIO_ORDERED_SC_LIST];

	do {
		mtx_lock(&ordered_sc_list->lock);

		if (STAILQ_EMPTY(&ordered_sc_list->head)) {
			/*
			 * ordered_sc_list is empty; there is nothing to
			 * process
			 */
			mtx_unlock(&ordered_sc_list->lock);
			return (1);
		}

		sc = LIO_STAILQ_FIRST_ENTRY(&ordered_sc_list->head,
					    struct lio_soft_command, node);

		status = LIO_REQUEST_PENDING;

		/*
		 * check if octeon has finished DMA'ing a response to where
		 * rptr is pointing to
		 */
		status64 = *sc->status_word;

		if (status64 != COMPLETION_WORD_INIT) {
			/*
			 * This logic ensures that all 64b have been written.
			 * 1. check byte 0 for non-FF
			 * 2. if non-FF, then swap result from BE to host order
			 * 3. check byte 7 (swapped to 0) for non-FF
			 * 4. if non-FF, use the low 32-bit status code
			 * 5. if either byte 0 or byte 7 is FF, don't use status
			 */
			if ((status64 & 0xff) != 0xff) {
				lio_swap_8B_data(&status64, 1);
				if (((status64 & 0xff) != 0xff)) {
					/* retrieve 16-bit firmware status */
					status = (uint32_t)(status64 &
							    0xffffULL);
					if (status) {
						status = LIO_FW_STATUS_CODE(
									status);
					} else {
						/* i.e. no error */
						status = LIO_REQUEST_DONE;
					}
				}
			}
		} else if (force_quit || (sc->timeout &&
			   lio_check_timeout(ticks, sc->timeout))) {
			lio_dev_err(octeon_dev, "%s: cmd failed, timeout (%u, %u)\n",
				    __func__, ticks, sc->timeout);
			status = LIO_REQUEST_TIMEOUT;
		}

		if (status != LIO_REQUEST_PENDING) {
			/* we have received a response or we have timed out */
			/* remove node from linked list */
			STAILQ_REMOVE(&octeon_dev->response_list
				      [LIO_ORDERED_SC_LIST].head,
				      &sc->node, lio_stailq_node, entries);
			atomic_subtract_int(&octeon_dev->response_list
					    [LIO_ORDERED_SC_LIST].
					    pending_req_count, 1);
			mtx_unlock(&ordered_sc_list->lock);

			if (sc->callback != NULL)
				sc->callback(octeon_dev, status,
					     sc->callback_arg);

			request_complete++;

		} else {
			/* no response yet */
			request_complete = 0;
			mtx_unlock(&ordered_sc_list->lock);
		}

		/*
		 * If we hit the Max Ordered requests to process every loop,
		 * we quit and let this function be invoked the next time
		 * the poll thread runs to process the remaining requests.
		 * This function can take up the entire CPU if there is no
		 * upper limit to the requests processed.
		 */
		if (request_complete >= resp_to_process)
			break;
	} while (request_complete);

	return (0);
}

static void
lio_poll_req_completion(void *arg, int pending)
{
	struct lio_tq		*ctq = (struct lio_tq *)arg;
	struct octeon_device	*oct = (struct octeon_device *)ctq->ctxptr;

	lio_process_ordered_list(oct, 0);
	taskqueue_enqueue_timeout(ctq->tq, &ctq->work, lio_ms_to_ticks(50));
}
