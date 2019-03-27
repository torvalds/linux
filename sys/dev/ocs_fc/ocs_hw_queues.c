/*-
 * Copyright (c) 2017 Broadcom. All rights reserved.
 * The term "Broadcom" refers to Broadcom Limited and/or its subsidiaries.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/**
 * @file
 *
 */

#include "ocs_os.h"
#include "ocs_hw.h"
#include "ocs_hw_queues.h"

#define HW_QTOP_DEBUG		0

/**
 * @brief Initialize queues
 *
 * Given the parsed queue topology spec, the SLI queues are created and
 * initialized
 *
 * @param hw pointer to HW object
 * @param qtop pointer to queue topology
 *
 * @return returns 0 for success, an error code value for failure.
 */
ocs_hw_rtn_e
ocs_hw_init_queues(ocs_hw_t *hw, ocs_hw_qtop_t *qtop)
{
	uint32_t i, j;
	uint32_t default_lengths[QTOP_LAST], len;
	uint32_t rqset_len = 0, rqset_ulp = 0, rqset_count = 0;
	uint8_t rqset_filter_mask = 0;
	hw_eq_t *eqs[hw->config.n_rq];
	hw_cq_t *cqs[hw->config.n_rq];
	hw_rq_t *rqs[hw->config.n_rq];
	ocs_hw_qtop_entry_t *qt, *next_qt;
	ocs_hw_mrq_t mrq;
	bool use_mrq = FALSE;

	hw_eq_t *eq = NULL;
	hw_cq_t *cq = NULL;
	hw_wq_t *wq = NULL;
	hw_rq_t *rq = NULL;
	hw_mq_t *mq = NULL;

	mrq.num_pairs = 0;
	default_lengths[QTOP_EQ] = 1024;
	default_lengths[QTOP_CQ] = hw->num_qentries[SLI_QTYPE_CQ];
	default_lengths[QTOP_WQ] = hw->num_qentries[SLI_QTYPE_WQ];
	default_lengths[QTOP_RQ] = hw->num_qentries[SLI_QTYPE_RQ];
	default_lengths[QTOP_MQ] = OCS_HW_MQ_DEPTH;

	ocs_hw_verify(hw != NULL, OCS_HW_RTN_INVALID_ARG);

	hw->eq_count = 0;
	hw->cq_count = 0;
	hw->mq_count = 0;
	hw->wq_count = 0;
	hw->rq_count = 0;
	hw->hw_rq_count = 0;
	ocs_list_init(&hw->eq_list, hw_eq_t, link);

	/* If MRQ is requested, Check if it is supported by SLI. */
	if ((hw->config.n_rq > 1 ) && !hw->sli.config.features.flag.mrqp) {
		ocs_log_err(hw->os, "MRQ topology not supported by SLI4.\n");
		return OCS_HW_RTN_ERROR;
	}

	if (hw->config.n_rq > 1)
		use_mrq = TRUE;

	/* Allocate class WQ pools */
	for (i = 0; i < ARRAY_SIZE(hw->wq_class_array); i++) {
		hw->wq_class_array[i] = ocs_varray_alloc(hw->os, OCS_HW_MAX_NUM_WQ);
		if (hw->wq_class_array[i] == NULL) {
			ocs_log_err(hw->os, "ocs_varray_alloc for wq_class failed\n");
			return OCS_HW_RTN_NO_MEMORY;
		}
	}

	/* Allocate per CPU WQ pools */
	for (i = 0; i < ARRAY_SIZE(hw->wq_cpu_array); i++) {
		hw->wq_cpu_array[i] = ocs_varray_alloc(hw->os, OCS_HW_MAX_NUM_WQ);
		if (hw->wq_cpu_array[i] == NULL) {
			ocs_log_err(hw->os, "ocs_varray_alloc for wq_class failed\n");
			return OCS_HW_RTN_NO_MEMORY;
		}
	}


	ocs_hw_assert(qtop != NULL);

	for (i = 0, qt = qtop->entries; i < qtop->inuse_count; i++, qt++) {
		if (i == qtop->inuse_count - 1)
			next_qt = NULL;
		else
			next_qt = qt + 1;

		switch(qt->entry) {
		case QTOP_EQ:
			len = (qt->len) ? qt->len : default_lengths[QTOP_EQ];

			if (qt->set_default) {
				default_lengths[QTOP_EQ] = len;
				break;
			}

			eq = hw_new_eq(hw, len);
			if (eq == NULL) {
				hw_queue_teardown(hw);
				return OCS_HW_RTN_NO_MEMORY;
			}
			break;

		case QTOP_CQ:
			len = (qt->len) ? qt->len : default_lengths[QTOP_CQ];

			if (qt->set_default) {
				default_lengths[QTOP_CQ] = len;
				break;
			}
			
			if (!eq || !next_qt) {
				goto fail;
			}

			/* If this CQ is for MRQ, then delay the creation */
			if (!use_mrq || next_qt->entry != QTOP_RQ) {
				cq = hw_new_cq(eq, len);
				if (cq == NULL) {
					goto fail;
				}
			}
			break;

		case QTOP_WQ: {

			len = (qt->len) ? qt->len : default_lengths[QTOP_WQ];
			if (qt->set_default) {
				default_lengths[QTOP_WQ] = len;
				break;
			}

			if ((hw->ulp_start + qt->ulp) > hw->ulp_max) {
				ocs_log_err(hw->os, "invalid ULP %d for WQ\n", qt->ulp);
				hw_queue_teardown(hw);
				return OCS_HW_RTN_NO_MEMORY;
			}
			
			if (cq == NULL)
				goto fail;

			wq = hw_new_wq(cq, len, qt->class, hw->ulp_start + qt->ulp);
			if (wq == NULL) {
				goto fail;
			}

			/* Place this WQ on the EQ WQ array */
			if (ocs_varray_add(eq->wq_array, wq)) {
				ocs_log_err(hw->os, "QTOP_WQ: EQ ocs_varray_add failed\n");
				hw_queue_teardown(hw);
				return OCS_HW_RTN_ERROR;
			}

			/* Place this WQ on the HW class array */
			if (qt->class < ARRAY_SIZE(hw->wq_class_array)) {
				if (ocs_varray_add(hw->wq_class_array[qt->class], wq)) {
					ocs_log_err(hw->os, "HW wq_class_array ocs_varray_add failed\n");
					hw_queue_teardown(hw);
					return OCS_HW_RTN_ERROR;
				}
			} else {
				ocs_log_err(hw->os, "Invalid class value: %d\n", qt->class);
				hw_queue_teardown(hw);
				return OCS_HW_RTN_ERROR;
			}

			/*
			 * Place this WQ on the per CPU list, asumming that EQs are mapped to cpu given
			 * by the EQ instance modulo number of CPUs
			 */
			if (ocs_varray_add(hw->wq_cpu_array[eq->instance % ocs_get_num_cpus()], wq)) {
				ocs_log_err(hw->os, "HW wq_cpu_array ocs_varray_add failed\n");
				hw_queue_teardown(hw);
				return OCS_HW_RTN_ERROR;
			}

			break;
		}
		case QTOP_RQ: {
			len = (qt->len) ? qt->len : default_lengths[QTOP_RQ];
			if (qt->set_default) {
				default_lengths[QTOP_RQ] = len;
				break;
			}

			if ((hw->ulp_start + qt->ulp) > hw->ulp_max) {
				ocs_log_err(hw->os, "invalid ULP %d for RQ\n", qt->ulp);
				hw_queue_teardown(hw);
				return OCS_HW_RTN_NO_MEMORY;
			}

			if (use_mrq) {
				mrq.rq_cfg[mrq.num_pairs].len = len;
				mrq.rq_cfg[mrq.num_pairs].ulp = hw->ulp_start + qt->ulp; 
				mrq.rq_cfg[mrq.num_pairs].filter_mask = qt->filter_mask;
				mrq.rq_cfg[mrq.num_pairs].eq = eq;
				mrq.num_pairs ++;
			} else {
				rq = hw_new_rq(cq, len, hw->ulp_start + qt->ulp);
				if (rq == NULL) {
					hw_queue_teardown(hw);
					return OCS_HW_RTN_NO_MEMORY;
				}
				rq->filter_mask = qt->filter_mask;
			}
			break;
		}

		case QTOP_MQ:
			len = (qt->len) ? qt->len : default_lengths[QTOP_MQ];
			if (qt->set_default) {
				default_lengths[QTOP_MQ] = len;
				break;
			}

			if (cq == NULL)
				goto fail;

			mq = hw_new_mq(cq, len);
			if (mq == NULL) {
				goto fail;
			}
			break;

		default:
			ocs_hw_assert(0);
			break;
		}
	}

	if (mrq.num_pairs) {
		/* First create normal RQs. */
		for (i = 0; i < mrq.num_pairs; i++) {
			for (j = 0; j < mrq.num_pairs; j++) {
				if ((i != j) && (mrq.rq_cfg[i].filter_mask == mrq.rq_cfg[j].filter_mask)) {
					/* This should be created using set */
					if (rqset_filter_mask && (rqset_filter_mask != mrq.rq_cfg[i].filter_mask)) {
						ocs_log_crit(hw->os, "Cant create morethan one RQ Set\n");
						hw_queue_teardown(hw);
						return OCS_HW_RTN_ERROR;
					} else if (!rqset_filter_mask){
						rqset_filter_mask = mrq.rq_cfg[i].filter_mask;
						rqset_len = mrq.rq_cfg[i].len;
						rqset_ulp = mrq.rq_cfg[i].ulp;
					}
					eqs[rqset_count] = mrq.rq_cfg[i].eq;
					rqset_count++;
					break;
				}
			}
			if (j == mrq.num_pairs) {
				/* Normal RQ */
				cq = hw_new_cq(mrq.rq_cfg[i].eq, default_lengths[QTOP_CQ]);
				if (cq == NULL) {
					hw_queue_teardown(hw);
					return OCS_HW_RTN_NO_MEMORY;
				}

				rq = hw_new_rq(cq, mrq.rq_cfg[i].len, mrq.rq_cfg[i].ulp);
				if (rq == NULL) {
					hw_queue_teardown(hw);
					return OCS_HW_RTN_NO_MEMORY;
				}
				rq->filter_mask = mrq.rq_cfg[i].filter_mask;
			}
		}

		/* Now create RQ Set */
		if (rqset_count) {
			if (rqset_count > OCE_HW_MAX_NUM_MRQ_PAIRS) {
				ocs_log_crit(hw->os,
					     "Max Supported MRQ pairs = %d\n",
					     OCE_HW_MAX_NUM_MRQ_PAIRS);
				hw_queue_teardown(hw);
				return OCS_HW_RTN_ERROR;
			}

			/* Create CQ set */
			if (hw_new_cq_set(eqs, cqs, rqset_count, default_lengths[QTOP_CQ])) {
				hw_queue_teardown(hw);
				return OCS_HW_RTN_ERROR;
			}

			/* Create RQ set */
			if (hw_new_rq_set(cqs, rqs, rqset_count, rqset_len, rqset_ulp)) {
				hw_queue_teardown(hw);
				return OCS_HW_RTN_ERROR;
			}

			for (i = 0; i < rqset_count ; i++) {
				rqs[i]->filter_mask = rqset_filter_mask;
				rqs[i]->is_mrq = TRUE;
				rqs[i]->base_mrq_id = rqs[0]->hdr->id;
			}

			hw->hw_mrq_count = rqset_count;
		}
	}

	return OCS_HW_RTN_SUCCESS;
fail:
	hw_queue_teardown(hw);
	return OCS_HW_RTN_NO_MEMORY;

}

/**
 * @brief Allocate a new EQ object
 *
 * A new EQ object is instantiated
 *
 * @param hw pointer to HW object
 * @param entry_count number of entries in the EQ
 *
 * @return pointer to allocated EQ object
 */
hw_eq_t*
hw_new_eq(ocs_hw_t *hw, uint32_t entry_count)
{
	hw_eq_t *eq = ocs_malloc(hw->os, sizeof(*eq), OCS_M_ZERO | OCS_M_NOWAIT);

	if (eq != NULL) {
		eq->type = SLI_QTYPE_EQ;
		eq->hw = hw;
		eq->entry_count = entry_count;
		eq->instance = hw->eq_count++;
		eq->queue = &hw->eq[eq->instance];
		ocs_list_init(&eq->cq_list, hw_cq_t, link);

		eq->wq_array = ocs_varray_alloc(hw->os, OCS_HW_MAX_NUM_WQ);
		if (eq->wq_array == NULL) {
			ocs_free(hw->os, eq, sizeof(*eq));
			eq = NULL;
		} else {
			if (sli_queue_alloc(&hw->sli, SLI_QTYPE_EQ, eq->queue, entry_count, NULL, 0)) {
				ocs_log_err(hw->os, "EQ[%d] allocation failure\n", eq->instance);
				ocs_free(hw->os, eq, sizeof(*eq));
				eq = NULL;
			} else {
				sli_eq_modify_delay(&hw->sli, eq->queue, 1, 0, 8);
				hw->hw_eq[eq->instance] = eq;
				ocs_list_add_tail(&hw->eq_list, eq);
				ocs_log_debug(hw->os, "create eq[%2d] id %3d len %4d\n", eq->instance, eq->queue->id,
					eq->entry_count);
			}
		}
	}
	return eq;
}

/**
 * @brief Allocate a new CQ object
 *
 * A new CQ object is instantiated
 *
 * @param eq pointer to parent EQ object
 * @param entry_count number of entries in the CQ
 *
 * @return pointer to allocated CQ object
 */
hw_cq_t*
hw_new_cq(hw_eq_t *eq, uint32_t entry_count)
{
	ocs_hw_t *hw = eq->hw;
	hw_cq_t *cq = ocs_malloc(hw->os, sizeof(*cq), OCS_M_ZERO | OCS_M_NOWAIT);

	if (cq != NULL) {
		cq->eq = eq;
		cq->type = SLI_QTYPE_CQ;
		cq->instance = eq->hw->cq_count++;
		cq->entry_count = entry_count;
		cq->queue = &hw->cq[cq->instance];

		ocs_list_init(&cq->q_list, hw_q_t, link);

		if (sli_queue_alloc(&hw->sli, SLI_QTYPE_CQ, cq->queue, cq->entry_count, eq->queue, 0)) {
			ocs_log_err(hw->os, "CQ[%d] allocation failure len=%d\n",
				eq->instance,
				eq->entry_count);
			ocs_free(hw->os, cq, sizeof(*cq));
			cq = NULL;
		} else {
			hw->hw_cq[cq->instance] = cq;
			ocs_list_add_tail(&eq->cq_list, cq);
			ocs_log_debug(hw->os, "create cq[%2d] id %3d len %4d\n", cq->instance, cq->queue->id,
				cq->entry_count);
		}
	}
	return cq;
}

/**
 * @brief Allocate a new CQ Set of objects.
 *
 * @param eqs pointer to a set of EQ objects.
 * @param cqs pointer to a set of CQ objects to be returned.
 * @param num_cqs number of CQ queues in the set.
 * @param entry_count number of entries in the CQ.
 *
 * @return 0 on success and -1 on failure.
 */
uint32_t
hw_new_cq_set(hw_eq_t *eqs[], hw_cq_t *cqs[], uint32_t num_cqs, uint32_t entry_count)
{
	uint32_t i;
	ocs_hw_t *hw = eqs[0]->hw;
	sli4_t *sli4 = &hw->sli;
	hw_cq_t *cq = NULL;
	sli4_queue_t *qs[SLI_MAX_CQ_SET_COUNT], *assocs[SLI_MAX_CQ_SET_COUNT];

	/* Initialise CQS pointers to NULL */
	for (i = 0; i < num_cqs; i++) {
		cqs[i] = NULL;
	}

	for (i = 0; i < num_cqs; i++) {
		cq = ocs_malloc(hw->os, sizeof(*cq), OCS_M_ZERO | OCS_M_NOWAIT);
		if (cq == NULL)
			goto error;

		cqs[i]          = cq;
		cq->eq          = eqs[i];
		cq->type        = SLI_QTYPE_CQ;
		cq->instance    = hw->cq_count++;
		cq->entry_count = entry_count;
		cq->queue       = &hw->cq[cq->instance];
		qs[i]           = cq->queue;
		assocs[i]       = eqs[i]->queue;
		ocs_list_init(&cq->q_list, hw_q_t, link);
	}

	if (sli_cq_alloc_set(sli4, qs, num_cqs, entry_count, assocs)) {
		ocs_log_err(NULL, "Failed to create CQ Set. \n");
		goto error;
	}

	for (i = 0; i < num_cqs; i++) {
		hw->hw_cq[cqs[i]->instance] = cqs[i];
		ocs_list_add_tail(&cqs[i]->eq->cq_list, cqs[i]);
	}

	return 0;

error:
	for (i = 0; i < num_cqs; i++) {
		if (cqs[i]) {
			ocs_free(hw->os, cqs[i], sizeof(*cqs[i]));
			cqs[i] = NULL;
		}
	}
	return -1;
}


/**
 * @brief Allocate a new MQ object
 *
 * A new MQ object is instantiated
 *
 * @param cq pointer to parent CQ object
 * @param entry_count number of entries in the MQ
 *
 * @return pointer to allocated MQ object
 */
hw_mq_t*
hw_new_mq(hw_cq_t *cq, uint32_t entry_count)
{
	ocs_hw_t *hw = cq->eq->hw;
	hw_mq_t *mq = ocs_malloc(hw->os, sizeof(*mq), OCS_M_ZERO | OCS_M_NOWAIT);

	if (mq != NULL) {
		mq->cq = cq;
		mq->type = SLI_QTYPE_MQ;
		mq->instance = cq->eq->hw->mq_count++;
		mq->entry_count = entry_count;
		mq->entry_size = OCS_HW_MQ_DEPTH;
		mq->queue = &hw->mq[mq->instance];

		if (sli_queue_alloc(&hw->sli, SLI_QTYPE_MQ,
				    mq->queue,
				    mq->entry_size,
				    cq->queue, 0)) {
			ocs_log_err(hw->os, "MQ allocation failure\n");
			ocs_free(hw->os, mq, sizeof(*mq));
			mq = NULL;
		} else {
			hw->hw_mq[mq->instance] = mq;
			ocs_list_add_tail(&cq->q_list, mq);
			ocs_log_debug(hw->os, "create mq[%2d] id %3d len %4d\n", mq->instance, mq->queue->id,
				mq->entry_count);
		}
	}
	return mq;
}

/**
 * @brief Allocate a new WQ object
 *
 * A new WQ object is instantiated
 *
 * @param cq pointer to parent CQ object
 * @param entry_count number of entries in the WQ
 * @param class WQ class
 * @param ulp index of chute
 *
 * @return pointer to allocated WQ object
 */
hw_wq_t*
hw_new_wq(hw_cq_t *cq, uint32_t entry_count, uint32_t class, uint32_t ulp)
{
	ocs_hw_t *hw = cq->eq->hw;
	hw_wq_t *wq = ocs_malloc(hw->os, sizeof(*wq), OCS_M_ZERO | OCS_M_NOWAIT);

	if (wq != NULL) {
		wq->hw = cq->eq->hw;
		wq->cq = cq;
		wq->type = SLI_QTYPE_WQ;
		wq->instance = cq->eq->hw->wq_count++;
		wq->entry_count = entry_count;
		wq->queue = &hw->wq[wq->instance];
		wq->ulp = ulp;
		wq->wqec_set_count = OCS_HW_WQEC_SET_COUNT;
		wq->wqec_count = wq->wqec_set_count;
		wq->free_count = wq->entry_count - 1;
		wq->class = class;
		ocs_list_init(&wq->pending_list, ocs_hw_wqe_t, link);

		if (sli_queue_alloc(&hw->sli, SLI_QTYPE_WQ, wq->queue, wq->entry_count, cq->queue, ulp)) {
			ocs_log_err(hw->os, "WQ allocation failure\n");
			ocs_free(hw->os, wq, sizeof(*wq));
			wq = NULL;
		} else {
			hw->hw_wq[wq->instance] = wq;
			ocs_list_add_tail(&cq->q_list, wq);
			ocs_log_debug(hw->os, "create wq[%2d] id %3d len %4d cls %d ulp %d\n", wq->instance, wq->queue->id,
				wq->entry_count, wq->class, wq->ulp);
		}
	}
	return wq;
}

/**
 * @brief Allocate a hw_rq_t object
 *
 * Allocate an RQ object, which encapsulates 2 SLI queues (for rq pair)
 *
 * @param cq pointer to parent CQ object
 * @param entry_count number of entries in the RQs
 * @param ulp ULP index for this RQ
 *
 * @return pointer to newly allocated hw_rq_t
 */
hw_rq_t*
hw_new_rq(hw_cq_t *cq, uint32_t entry_count, uint32_t ulp)
{
	ocs_hw_t *hw = cq->eq->hw;
	hw_rq_t *rq = ocs_malloc(hw->os, sizeof(*rq), OCS_M_ZERO | OCS_M_NOWAIT);
	uint32_t max_hw_rq;

	ocs_hw_get(hw, OCS_HW_MAX_RQ_ENTRIES, &max_hw_rq);


	if (rq != NULL) {
		rq->instance = hw->hw_rq_count++;
		rq->cq = cq;
		rq->type = SLI_QTYPE_RQ;
		rq->ulp = ulp;

		rq->entry_count = OCS_MIN(entry_count, OCS_MIN(max_hw_rq, OCS_HW_RQ_NUM_HDR));

		/* Create the header RQ */
		ocs_hw_assert(hw->rq_count < ARRAY_SIZE(hw->rq));
		rq->hdr = &hw->rq[hw->rq_count];
		rq->hdr_entry_size = OCS_HW_RQ_HEADER_SIZE;

		if (sli_fc_rq_alloc(&hw->sli, rq->hdr,
				    rq->entry_count,
				    rq->hdr_entry_size,
				    cq->queue,
				    ulp, TRUE)) {
			ocs_log_err(hw->os, "RQ allocation failure - header\n");
			ocs_free(hw->os, rq, sizeof(*rq));
			return NULL;
		}
		hw->hw_rq_lookup[hw->rq_count] = rq->instance;	/* Update hw_rq_lookup[] */
		hw->rq_count++;
		ocs_log_debug(hw->os, "create rq[%2d] id %3d len %4d hdr  size %4d ulp %d\n",
			rq->instance, rq->hdr->id, rq->entry_count, rq->hdr_entry_size, rq->ulp);

		/* Create the default data RQ */
		ocs_hw_assert(hw->rq_count < ARRAY_SIZE(hw->rq));
		rq->data = &hw->rq[hw->rq_count];
		rq->data_entry_size = hw->config.rq_default_buffer_size;

		if (sli_fc_rq_alloc(&hw->sli, rq->data,
				    rq->entry_count,
				    rq->data_entry_size,
				    cq->queue,
				    ulp, FALSE)) {
			ocs_log_err(hw->os, "RQ allocation failure - first burst\n");
			ocs_free(hw->os, rq, sizeof(*rq));
			return NULL;
		}
		hw->hw_rq_lookup[hw->rq_count] = rq->instance;	/* Update hw_rq_lookup[] */
		hw->rq_count++;
		ocs_log_debug(hw->os, "create rq[%2d] id %3d len %4d data size %4d ulp %d\n", rq->instance,
			rq->data->id, rq->entry_count, rq->data_entry_size, rq->ulp);

		hw->hw_rq[rq->instance] = rq;
		ocs_list_add_tail(&cq->q_list, rq);

		rq->rq_tracker = ocs_malloc(hw->os, sizeof(ocs_hw_sequence_t*) *
					    rq->entry_count, OCS_M_ZERO | OCS_M_NOWAIT);
		if (rq->rq_tracker == NULL) {
			ocs_log_err(hw->os, "RQ tracker buf allocation failure\n");
			return NULL;
		}
	}
	return rq;
}


/**
 * @brief Allocate a hw_rq_t object SET
 *
 * Allocate an RQ object SET, where each element in set
 * encapsulates 2 SLI queues (for rq pair)
 *
 * @param cqs pointers to be associated with RQs.
 * @param rqs RQ pointers to be returned on success.
 * @param num_rq_pairs number of rq pairs in the Set.
 * @param entry_count number of entries in the RQs
 * @param ulp ULP index for this RQ
 *
 * @return 0 in success and -1 on failure.
 */
uint32_t
hw_new_rq_set(hw_cq_t *cqs[], hw_rq_t *rqs[], uint32_t num_rq_pairs, uint32_t entry_count, uint32_t ulp)
{
	ocs_hw_t *hw = cqs[0]->eq->hw;
	hw_rq_t *rq = NULL;
	sli4_queue_t *qs[SLI_MAX_RQ_SET_COUNT * 2] = { NULL };
	uint32_t max_hw_rq, i, q_count;

	ocs_hw_get(hw, OCS_HW_MAX_RQ_ENTRIES, &max_hw_rq);

	/* Initialise RQS pointers */
	for (i = 0; i < num_rq_pairs; i++) {
		rqs[i] = NULL;
	}

	for (i = 0, q_count = 0; i < num_rq_pairs; i++, q_count += 2) {
		rq = ocs_malloc(hw->os, sizeof(*rq), OCS_M_ZERO | OCS_M_NOWAIT);
		if (rq == NULL)
			goto error;

		rqs[i] = rq;
		rq->instance = hw->hw_rq_count++;
		rq->cq = cqs[i];
		rq->type = SLI_QTYPE_RQ;
		rq->ulp = ulp;
		rq->entry_count = OCS_MIN(entry_count, OCS_MIN(max_hw_rq, OCS_HW_RQ_NUM_HDR));

		/* Header RQ */
		rq->hdr = &hw->rq[hw->rq_count];
		rq->hdr_entry_size = OCS_HW_RQ_HEADER_SIZE;
		hw->hw_rq_lookup[hw->rq_count] = rq->instance;
		hw->rq_count++;
		qs[q_count] = rq->hdr;

		/* Data RQ */
		rq->data = &hw->rq[hw->rq_count];
		rq->data_entry_size = hw->config.rq_default_buffer_size;
		hw->hw_rq_lookup[hw->rq_count] = rq->instance;
		hw->rq_count++;
		qs[q_count + 1] = rq->data;

		rq->rq_tracker = NULL;
	}

	if (sli_fc_rq_set_alloc(&hw->sli, num_rq_pairs, qs,
			    cqs[0]->queue->id,
			    rqs[0]->entry_count,
			    rqs[0]->hdr_entry_size,
			    rqs[0]->data_entry_size,
			    ulp)) {
		ocs_log_err(hw->os, "RQ Set allocation failure for base CQ=%d\n", cqs[0]->queue->id);
		goto error;
	}


	for (i = 0; i < num_rq_pairs; i++) {
		hw->hw_rq[rqs[i]->instance] = rqs[i];
		ocs_list_add_tail(&cqs[i]->q_list, rqs[i]);
		rqs[i]->rq_tracker = ocs_malloc(hw->os, sizeof(ocs_hw_sequence_t*) *
					    rqs[i]->entry_count, OCS_M_ZERO | OCS_M_NOWAIT);
		if (rqs[i]->rq_tracker == NULL) {
			ocs_log_err(hw->os, "RQ tracker buf allocation failure\n");
			goto error;
		}
	}

	return 0;

error:
	for (i = 0; i < num_rq_pairs; i++) {
		if (rqs[i] != NULL) {
			if (rqs[i]->rq_tracker != NULL) {
				ocs_free(hw->os, rqs[i]->rq_tracker,
					 sizeof(ocs_hw_sequence_t*) *
					 rqs[i]->entry_count);
			}
			ocs_free(hw->os, rqs[i], sizeof(*rqs[i]));
		}
	}

	return -1;
}


/**
 * @brief Free an EQ object
 *
 * The EQ object and any child queue objects are freed
 *
 * @param eq pointer to EQ object
 *
 * @return none
 */
void
hw_del_eq(hw_eq_t *eq)
{
	if (eq != NULL) {
		hw_cq_t *cq;
		hw_cq_t *cq_next;

		ocs_list_foreach_safe(&eq->cq_list, cq, cq_next) {
			hw_del_cq(cq);
		}
		ocs_varray_free(eq->wq_array);
		ocs_list_remove(&eq->hw->eq_list, eq);
		eq->hw->hw_eq[eq->instance] = NULL;
		ocs_free(eq->hw->os, eq, sizeof(*eq));
	}
}

/**
 * @brief Free a CQ object
 *
 * The CQ object and any child queue objects are freed
 *
 * @param cq pointer to CQ object
 *
 * @return none
 */
void
hw_del_cq(hw_cq_t *cq)
{
	if (cq != NULL) {
		hw_q_t *q;
		hw_q_t *q_next;

		ocs_list_foreach_safe(&cq->q_list, q, q_next) {
			switch(q->type) {
			case SLI_QTYPE_MQ:
				hw_del_mq((hw_mq_t*) q);
				break;
			case SLI_QTYPE_WQ:
				hw_del_wq((hw_wq_t*) q);
				break;
			case SLI_QTYPE_RQ:
				hw_del_rq((hw_rq_t*) q);
				break;
			default:
				break;
			}
		}
		ocs_list_remove(&cq->eq->cq_list, cq);
		cq->eq->hw->hw_cq[cq->instance] = NULL;
		ocs_free(cq->eq->hw->os, cq, sizeof(*cq));
	}
}

/**
 * @brief Free a MQ object
 *
 * The MQ object is freed
 *
 * @param mq pointer to MQ object
 *
 * @return none
 */
void
hw_del_mq(hw_mq_t *mq)
{
	if (mq != NULL) {
		ocs_list_remove(&mq->cq->q_list, mq);
		mq->cq->eq->hw->hw_mq[mq->instance] = NULL;
		ocs_free(mq->cq->eq->hw->os, mq, sizeof(*mq));
	}
}

/**
 * @brief Free a WQ object
 *
 * The WQ object is freed
 *
 * @param wq pointer to WQ object
 *
 * @return none
 */
void
hw_del_wq(hw_wq_t *wq)
{
	if (wq != NULL) {
		ocs_list_remove(&wq->cq->q_list, wq);
		wq->cq->eq->hw->hw_wq[wq->instance] = NULL;
		ocs_free(wq->cq->eq->hw->os, wq, sizeof(*wq));
	}
}

/**
 * @brief Free an RQ object
 *
 * The RQ object is freed
 *
 * @param rq pointer to RQ object
 *
 * @return none
 */
void
hw_del_rq(hw_rq_t *rq)
{

	if (rq != NULL) {
		ocs_hw_t *hw = rq->cq->eq->hw;
		/* Free RQ tracker */
		if (rq->rq_tracker != NULL) {
			ocs_free(hw->os, rq->rq_tracker, sizeof(ocs_hw_sequence_t*) * rq->entry_count);
			rq->rq_tracker = NULL;
		}
		ocs_list_remove(&rq->cq->q_list, rq);
		hw->hw_rq[rq->instance] = NULL;
		ocs_free(hw->os, rq, sizeof(*rq));
	}
}

/**
 * @brief Display HW queue objects
 *
 * The HW queue objects are displayed using ocs_log
 *
 * @param hw pointer to HW object
 *
 * @return none
 */
void
hw_queue_dump(ocs_hw_t *hw)
{
	hw_eq_t *eq;
	hw_cq_t *cq;
	hw_q_t *q;
	hw_mq_t *mq;
	hw_wq_t *wq;
	hw_rq_t *rq;

	ocs_list_foreach(&hw->eq_list, eq) {
		ocs_printf("eq[%d] id %2d\n", eq->instance, eq->queue->id);
		ocs_list_foreach(&eq->cq_list, cq) {
			ocs_printf("  cq[%d] id %2d current\n", cq->instance, cq->queue->id);
			ocs_list_foreach(&cq->q_list, q) {
				switch(q->type) {
				case SLI_QTYPE_MQ:
					mq = (hw_mq_t *) q;
					ocs_printf("    mq[%d] id %2d\n", mq->instance, mq->queue->id);
					break;
				case SLI_QTYPE_WQ:
					wq = (hw_wq_t *) q;
					ocs_printf("    wq[%d] id %2d\n", wq->instance, wq->queue->id);
					break;
				case SLI_QTYPE_RQ:
					rq = (hw_rq_t *) q;
					ocs_printf("    rq[%d] hdr id %2d\n", rq->instance, rq->hdr->id);
					break;
				default:
					break;
				}
			}
		}
	}
}

/**
 * @brief Teardown HW queue objects
 *
 * The HW queue objects are freed
 *
 * @param hw pointer to HW object
 *
 * @return none
 */
void
hw_queue_teardown(ocs_hw_t *hw)
{
	uint32_t i;
	hw_eq_t *eq;
	hw_eq_t *eq_next;

	if (ocs_list_valid(&hw->eq_list)) {
		ocs_list_foreach_safe(&hw->eq_list, eq, eq_next) {
			hw_del_eq(eq);
		}
	}
	for (i = 0; i < ARRAY_SIZE(hw->wq_cpu_array); i++) {
		ocs_varray_free(hw->wq_cpu_array[i]);
		hw->wq_cpu_array[i] = NULL;
	}
	for (i = 0; i < ARRAY_SIZE(hw->wq_class_array); i++) {
		ocs_varray_free(hw->wq_class_array[i]);
		hw->wq_class_array[i] = NULL;
	}
}

/**
 * @brief Allocate a WQ to an IO object
 *
 * The next work queue index is used to assign a WQ to an IO.
 *
 * If wq_steering is OCS_HW_WQ_STEERING_CLASS, a WQ from io->wq_class is
 * selected.
 *
 * If wq_steering is OCS_HW_WQ_STEERING_REQUEST, then a WQ from the EQ that
 * the IO request came in on is selected.
 *
 * If wq_steering is OCS_HW_WQ_STEERING_CPU, then a WQ associted with the
 * CPU the request is made on is selected.
 *
 * @param hw pointer to HW object
 * @param io pointer to IO object
 *
 * @return Return pointer to next WQ
 */
hw_wq_t *
ocs_hw_queue_next_wq(ocs_hw_t *hw, ocs_hw_io_t *io)
{
	hw_eq_t *eq;
	hw_wq_t *wq = NULL;

	switch(io->wq_steering) {
	case OCS_HW_WQ_STEERING_CLASS:
		if (likely(io->wq_class < ARRAY_SIZE(hw->wq_class_array))) {
			wq = ocs_varray_iter_next(hw->wq_class_array[io->wq_class]);
		}
		break;
	case OCS_HW_WQ_STEERING_REQUEST:
		eq = io->eq;
		if (likely(eq != NULL)) {
			wq = ocs_varray_iter_next(eq->wq_array);
		}
		break;
	case OCS_HW_WQ_STEERING_CPU: {
		uint32_t cpuidx = ocs_thread_getcpu();

		if (likely(cpuidx < ARRAY_SIZE(hw->wq_cpu_array))) {
			wq = ocs_varray_iter_next(hw->wq_cpu_array[cpuidx]);
		}
		break;
	}
	}

	if (unlikely(wq == NULL)) {
		wq = hw->hw_wq[0];
	}

	return wq;
}

/**
 * @brief Return count of EQs for a queue topology object
 *
 * The EQ count for in the HWs queue topology (hw->qtop) object is returned
 *
 * @param hw pointer to HW object
 *
 * @return count of EQs
 */
uint32_t
ocs_hw_qtop_eq_count(ocs_hw_t *hw)
{
	return hw->qtop->entry_counts[QTOP_EQ];
}

#define TOKEN_LEN		32

/**
 * @brief return string given a QTOP entry
 *
 * @param entry QTOP entry
 *
 * @return returns string or "unknown"
 */
#if HW_QTOP_DEBUG
static char *
qtopentry2s(ocs_hw_qtop_entry_e entry) {
	switch(entry) {
	#define P(x)	case x: return #x;
	P(QTOP_EQ)
	P(QTOP_CQ)
	P(QTOP_WQ)
	P(QTOP_RQ)
	P(QTOP_MQ)
	P(QTOP_THREAD_START)
	P(QTOP_THREAD_END)
	P(QTOP_LAST)
	#undef P
	}
	return "unknown";
}
#endif

/**
 * @brief Declare token types
 */
typedef enum {
	TOK_LPAREN = 1,
	TOK_RPAREN,
	TOK_COLON,
	TOK_EQUALS,
	TOK_QUEUE,
	TOK_ATTR_NAME,
	TOK_NUMBER,
	TOK_NUMBER_VALUE,
	TOK_NUMBER_LIST,
} tok_type_e;

/**
 * @brief Declare token sub-types
 */
typedef enum {
	TOK_SUB_EQ = 100,
	TOK_SUB_CQ,
	TOK_SUB_RQ,
	TOK_SUB_MQ,
	TOK_SUB_WQ,
	TOK_SUB_LEN,
	TOK_SUB_CLASS,
	TOK_SUB_ULP,
	TOK_SUB_FILTER,
} tok_subtype_e;

/**
 * @brief convert queue subtype to QTOP entry
 *
 * @param q queue subtype
 *
 * @return QTOP entry or 0
 */
static ocs_hw_qtop_entry_e
subtype2qtop(tok_subtype_e q)
{
	switch(q) {
	case TOK_SUB_EQ:	return QTOP_EQ;
	case TOK_SUB_CQ:	return QTOP_CQ;
	case TOK_SUB_RQ:	return QTOP_RQ;
	case TOK_SUB_MQ:	return QTOP_MQ;
	case TOK_SUB_WQ:	return QTOP_WQ;
	default:
		break;
	}
	return 0;
}

/**
 * @brief Declare token object
 */
typedef struct {
	tok_type_e type;
	tok_subtype_e subtype;
	char string[TOKEN_LEN];
} tok_t;

/**
 * @brief Declare token array object
 */
typedef struct {
	tok_t *tokens;			/* Pointer to array of tokens */
	uint32_t alloc_count;		/* Number of tokens in the array */
	uint32_t inuse_count;		/* Number of tokens posted to array */
	uint32_t iter_idx;		/* Iterator index */
} tokarray_t;

/**
 * @brief Declare token match structure
 */
typedef struct {
	char *s;
	tok_type_e type;
	tok_subtype_e subtype;
} tokmatch_t;

/**
 * @brief test if character is ID start character
 *
 * @param c character to test
 *
 * @return TRUE if character is an ID start character
 */
static int32_t
idstart(int c)
{
	return	isalpha(c) || (c == '_') || (c == '$');
}

/**
 * @brief test if character is an ID character
 *
 * @param c character to test
 *
 * @return TRUE if character is an ID character
 */
static int32_t
idchar(int c)
{
	return idstart(c) || ocs_isdigit(c);
}

/**
 * @brief Declare single character matches
 */
static tokmatch_t cmatches[] = {
	{"(", TOK_LPAREN},
	{")", TOK_RPAREN},
	{":", TOK_COLON},
	{"=", TOK_EQUALS},
};

/**
 * @brief Declare identifier match strings
 */
static tokmatch_t smatches[] = {
	{"eq", TOK_QUEUE, TOK_SUB_EQ},
	{"cq", TOK_QUEUE, TOK_SUB_CQ},
	{"rq", TOK_QUEUE, TOK_SUB_RQ},
	{"mq", TOK_QUEUE, TOK_SUB_MQ},
	{"wq", TOK_QUEUE, TOK_SUB_WQ},
	{"len", TOK_ATTR_NAME, TOK_SUB_LEN},
	{"class", TOK_ATTR_NAME, TOK_SUB_CLASS},
	{"ulp", TOK_ATTR_NAME, TOK_SUB_ULP},
	{"filter", TOK_ATTR_NAME, TOK_SUB_FILTER},
};

/**
 * @brief Scan string and return next token
 *
 * The string is scanned and the next token is returned
 *
 * @param s input string to scan
 * @param tok pointer to place scanned token
 *
 * @return pointer to input string following scanned token, or NULL
 */
static const char *
tokenize(const char *s, tok_t *tok)
{
	uint32_t i;

	memset(tok, 0, sizeof(*tok));

	/* Skip over whitespace */
	while (*s && ocs_isspace(*s)) {
		s++;
	}

	/* Return if nothing left in this string */
	if (*s == 0) {
		return NULL;
	}

	/* Look for single character matches */
	for (i = 0; i < ARRAY_SIZE(cmatches); i++) {
		if (cmatches[i].s[0] == *s) {
			tok->type = cmatches[i].type;
			tok->subtype = cmatches[i].subtype;
			tok->string[0] = *s++;
			return s;
		}
	}

	/* Scan for a hex number or decimal */
	if ((s[0] == '0') && ((s[1] == 'x') || (s[1] == 'X'))) {
		char *p = tok->string;

		tok->type = TOK_NUMBER;

		*p++ = *s++;
		*p++ = *s++;
		while ((*s == '.') || ocs_isxdigit(*s)) {
			if ((p - tok->string) < (int32_t)sizeof(tok->string)) {
				*p++ = *s;
			}
			if (*s == ',') {
				tok->type = TOK_NUMBER_LIST;
			}
			s++;
		}
		*p = 0;
		return s;
	} else if (ocs_isdigit(*s)) {
		char *p = tok->string;

		tok->type = TOK_NUMBER;
		while ((*s == ',') || ocs_isdigit(*s)) {
			if ((p - tok->string) < (int32_t)sizeof(tok->string)) {
				*p++ = *s;
			}
			if (*s == ',') {
				tok->type = TOK_NUMBER_LIST;
			}
			s++;
		}
		*p = 0;
		return s;
	}

	/* Scan for an ID */
	if (idstart(*s)) {
		char *p = tok->string;

		for (*p++ = *s++; idchar(*s); s++) {
			if ((p - tok->string) < TOKEN_LEN) {
				*p++ = *s;
			}
		}

		/* See if this is a $ number value */
		if (tok->string[0] == '$') {
			tok->type = TOK_NUMBER_VALUE;
		} else {
			/* Look for a string match */
			for (i = 0; i < ARRAY_SIZE(smatches); i++) {
				if (strcmp(smatches[i].s, tok->string) == 0) {
					tok->type = smatches[i].type;
					tok->subtype = smatches[i].subtype;
					return s;
				}
			}
		}
	}
	return s;
}

/**
 * @brief convert token type to string
 *
 * @param type token type
 *
 * @return string, or "unknown"
 */
static const char *
token_type2s(tok_type_e type)
{
	switch(type) {
	#define P(x)	case x: return #x;
	P(TOK_LPAREN)
	P(TOK_RPAREN)
	P(TOK_COLON)
	P(TOK_EQUALS)
	P(TOK_QUEUE)
	P(TOK_ATTR_NAME)
	P(TOK_NUMBER)
	P(TOK_NUMBER_VALUE)
	P(TOK_NUMBER_LIST)
	#undef P
	}
	return "unknown";
}

/**
 * @brief convert token sub-type to string
 *
 * @param subtype token sub-type
 *
 * @return string, or "unknown"
 */
static const char *
token_subtype2s(tok_subtype_e subtype)
{
	switch(subtype) {
	#define P(x)	case x: return #x;
	P(TOK_SUB_EQ)
	P(TOK_SUB_CQ)
	P(TOK_SUB_RQ)
	P(TOK_SUB_MQ)
	P(TOK_SUB_WQ)
	P(TOK_SUB_LEN)
	P(TOK_SUB_CLASS)
	P(TOK_SUB_ULP)
	P(TOK_SUB_FILTER)
	#undef P
	}
	return "";
}

/**
 * @brief Generate syntax error message
 *
 * A syntax error message is found, the input tokens are dumped up to and including
 * the token that failed as indicated by the current iterator index.
 *
 * @param hw pointer to HW object
 * @param tokarray pointer to token array object
 *
 * @return none
 */
static void
tok_syntax(ocs_hw_t *hw, tokarray_t *tokarray)
{
	uint32_t i;
	tok_t *tok;

	ocs_log_test(hw->os, "Syntax error:\n");

	for (i = 0, tok = tokarray->tokens; (i <= tokarray->inuse_count); i++, tok++) {
		ocs_log_test(hw->os, "%s [%2d]    %-16s %-16s %s\n", (i == tokarray->iter_idx) ? ">>>" : "   ", i,
			token_type2s(tok->type), token_subtype2s(tok->subtype), tok->string);
	}
}

/**
 * @brief parse a number
 *
 * Parses tokens of type TOK_NUMBER and TOK_NUMBER_VALUE, returning a numeric value
 *
 * @param hw pointer to HW object
 * @param qtop pointer to QTOP object
 * @param tok pointer to token to parse
 *
 * @return numeric value
 */
static uint32_t
tok_getnumber(ocs_hw_t *hw, ocs_hw_qtop_t *qtop, tok_t *tok)
{
	uint32_t rval = 0;
	uint32_t num_cpus = ocs_get_num_cpus();

	switch(tok->type) {
	case TOK_NUMBER_VALUE:
		if (ocs_strcmp(tok->string, "$ncpu") == 0) {
			rval = num_cpus;
		} else if (ocs_strcmp(tok->string, "$ncpu1") == 0) {
			rval = num_cpus - 1;
		} else if (ocs_strcmp(tok->string, "$nwq") == 0) {
			if (hw != NULL) {
				rval = hw->config.n_wq;
			}
		} else if (ocs_strcmp(tok->string, "$maxmrq") == 0) {
			rval = MIN(num_cpus, OCS_HW_MAX_MRQS);
		} else if (ocs_strcmp(tok->string, "$nulp") == 0) {
			rval = hw->ulp_max - hw->ulp_start + 1;
		} else if ((qtop->rptcount_idx > 0) && ocs_strcmp(tok->string, "$rpt0") == 0) {
			rval = qtop->rptcount[qtop->rptcount_idx-1];
		} else if ((qtop->rptcount_idx > 1) && ocs_strcmp(tok->string, "$rpt1") == 0) {
			rval = qtop->rptcount[qtop->rptcount_idx-2];
		} else if ((qtop->rptcount_idx > 2) && ocs_strcmp(tok->string, "$rpt2") == 0) {
			rval = qtop->rptcount[qtop->rptcount_idx-3];
		} else if ((qtop->rptcount_idx > 3) && ocs_strcmp(tok->string, "$rpt3") == 0) {
			rval = qtop->rptcount[qtop->rptcount_idx-4];
		} else {
			rval = ocs_strtoul(tok->string, 0, 0);
		}
		break;
	case TOK_NUMBER:
		rval = ocs_strtoul(tok->string, 0, 0);
		break;
	default:
		break;
	}
	return rval;
}


/**
 * @brief parse an array of tokens
 *
 * The tokens are semantically parsed, to generate QTOP entries.
 *
 * @param hw pointer to HW object
 * @param tokarray array array of tokens
 * @param qtop ouptut QTOP object
 *
 * @return returns 0 for success, a negative error code value for failure.
 */
static int32_t
parse_topology(ocs_hw_t *hw, tokarray_t *tokarray, ocs_hw_qtop_t *qtop)
{
	ocs_hw_qtop_entry_t *qt = qtop->entries + qtop->inuse_count;
	tok_t *tok;

	for (; (tokarray->iter_idx < tokarray->inuse_count) &&
	     ((tok = &tokarray->tokens[tokarray->iter_idx]) != NULL); ) {
		if (qtop->inuse_count >= qtop->alloc_count) {
			return -1;
		}

		qt = qtop->entries + qtop->inuse_count;

		switch (tok[0].type)
		{
		case TOK_QUEUE:
			qt->entry = subtype2qtop(tok[0].subtype);
			qt->set_default = FALSE;
			qt->len = 0;
			qt->class = 0;
			qtop->inuse_count++;

			tokarray->iter_idx++;		/* Advance current token index */

			/* Parse for queue attributes, possibly multiple instances */
			while ((tokarray->iter_idx + 4) <= tokarray->inuse_count) {
				tok = &tokarray->tokens[tokarray->iter_idx];
				if(	(tok[0].type == TOK_COLON) &&
					(tok[1].type == TOK_ATTR_NAME) &&
					(tok[2].type == TOK_EQUALS) &&
					((tok[3].type == TOK_NUMBER) ||
					 (tok[3].type == TOK_NUMBER_VALUE) ||
					 (tok[3].type == TOK_NUMBER_LIST))) {

					switch (tok[1].subtype) {
					case TOK_SUB_LEN:
						qt->len = tok_getnumber(hw, qtop, &tok[3]);
						break;

					case TOK_SUB_CLASS:
						qt->class = tok_getnumber(hw, qtop, &tok[3]);
						break;

					case TOK_SUB_ULP:
						qt->ulp = tok_getnumber(hw, qtop, &tok[3]);
						break;

					case TOK_SUB_FILTER:
						if (tok[3].type == TOK_NUMBER_LIST) {
							uint32_t mask = 0;
							char *p = tok[3].string;

							while ((p != NULL) && *p) {
								uint32_t v;

								v = ocs_strtoul(p, 0, 0);
								if (v < 32) {
									mask |= (1U << v);
								}

								p = ocs_strchr(p, ',');
								if (p != NULL) {
									p++;
								}
							}
							qt->filter_mask = mask;
						} else {
							qt->filter_mask = (1U << tok_getnumber(hw, qtop, &tok[3]));
						}
						break;
					default:
						break;
					}
					/* Advance current token index */
					tokarray->iter_idx += 4;
				} else {
					break;
				}
			}
			qtop->entry_counts[qt->entry]++;
			break;

		case TOK_ATTR_NAME:
			if (	((tokarray->iter_idx + 5) <= tokarray->inuse_count) &&
				(tok[1].type == TOK_COLON) &&
				(tok[2].type == TOK_QUEUE) &&
				(tok[3].type == TOK_EQUALS) &&
				((tok[4].type == TOK_NUMBER) || (tok[4].type == TOK_NUMBER_VALUE))) {
				qt->entry = subtype2qtop(tok[2].subtype);
				qt->set_default = TRUE;
				switch(tok[0].subtype) {
				case TOK_SUB_LEN:
					qt->len = tok_getnumber(hw, qtop, &tok[4]);
					break;
				case TOK_SUB_CLASS:
					qt->class = tok_getnumber(hw, qtop, &tok[4]);
					break;
				case TOK_SUB_ULP:
					qt->ulp = tok_getnumber(hw, qtop, &tok[4]);
					break;
				default:
					break;
				}
				qtop->inuse_count++;
				tokarray->iter_idx += 5;
			} else {
				tok_syntax(hw, tokarray);
				return -1;
			}
			break;

		case TOK_NUMBER:
		case TOK_NUMBER_VALUE: {
			uint32_t rpt_count = 1;
			uint32_t i;

			rpt_count = tok_getnumber(hw, qtop, tok);

			if (tok[1].type == TOK_LPAREN) {
				uint32_t iter_idx_save;

				tokarray->iter_idx += 2;

				/* save token array iteration index */
				iter_idx_save = tokarray->iter_idx;

				for (i = 0; i < rpt_count; i++) {
					uint32_t rptcount_idx = qtop->rptcount_idx;

					if (qtop->rptcount_idx < ARRAY_SIZE(qtop->rptcount)) {
						qtop->rptcount[qtop->rptcount_idx++] = i;
					}

					/* restore token array iteration index */
					tokarray->iter_idx = iter_idx_save;

					/* parse, append to qtop */
					parse_topology(hw, tokarray, qtop);

					qtop->rptcount_idx = rptcount_idx;
				}
			}
			break;
		}

		case TOK_RPAREN:
			tokarray->iter_idx++;
			return 0;

		default:
			tok_syntax(hw, tokarray);
			return -1;
		}
	}
	return 0;
}

/**
 * @brief Parse queue topology string
 *
 * The queue topology object is allocated, and filled with the results of parsing the
 * passed in queue topology string
 *
 * @param hw pointer to HW object
 * @param qtop_string input queue topology string
 *
 * @return pointer to allocated QTOP object, or NULL if there was an error
 */
ocs_hw_qtop_t *
ocs_hw_qtop_parse(ocs_hw_t *hw, const char *qtop_string)
{
	ocs_hw_qtop_t *qtop;
	tokarray_t tokarray;
	const char *s;
#if HW_QTOP_DEBUG
	uint32_t i;
	ocs_hw_qtop_entry_t *qt;
#endif

	ocs_log_debug(hw->os, "queue topology: %s\n", qtop_string);

	/* Allocate a token array */
	tokarray.tokens = ocs_malloc(hw->os, MAX_TOKENS * sizeof(*tokarray.tokens), OCS_M_ZERO | OCS_M_NOWAIT);
	if (tokarray.tokens == NULL) {
		return NULL;
	}
	tokarray.alloc_count = MAX_TOKENS;
	tokarray.inuse_count = 0;
	tokarray.iter_idx = 0;

	/* Parse the tokens */
	for (s = qtop_string; (tokarray.inuse_count < tokarray.alloc_count) &&
	     ((s = tokenize(s, &tokarray.tokens[tokarray.inuse_count]))) != NULL; ) {
		tokarray.inuse_count++;;
	}

	/* Allocate a queue topology structure */
	qtop = ocs_malloc(hw->os, sizeof(*qtop), OCS_M_ZERO | OCS_M_NOWAIT);
	if (qtop == NULL) {
		ocs_free(hw->os, tokarray.tokens, MAX_TOKENS * sizeof(*tokarray.tokens));
		ocs_log_err(hw->os, "malloc qtop failed\n");
		return NULL;
	}
	qtop->os = hw->os;

	/* Allocate queue topology entries */
	qtop->entries = ocs_malloc(hw->os, OCS_HW_MAX_QTOP_ENTRIES*sizeof(*qtop->entries), OCS_M_ZERO | OCS_M_NOWAIT);
	if (qtop->entries == NULL) {
		ocs_log_err(hw->os, "malloc qtop entries failed\n");
		ocs_free(hw->os, qtop, sizeof(*qtop));
		ocs_free(hw->os, tokarray.tokens, MAX_TOKENS * sizeof(*tokarray.tokens));
		return NULL;
	}
	qtop->alloc_count = OCS_HW_MAX_QTOP_ENTRIES;
	qtop->inuse_count = 0;

	/* Parse the tokens */
	parse_topology(hw, &tokarray, qtop);
#if HW_QTOP_DEBUG
	for (i = 0, qt = qtop->entries; i < qtop->inuse_count; i++, qt++) {
		ocs_log_debug(hw->os, "entry %s set_df %d len %4d class %d ulp %d\n", qtopentry2s(qt->entry), qt->set_default, qt->len,
		       qt->class, qt->ulp);
	}
#endif

	/* Free the tokens array */
	ocs_free(hw->os, tokarray.tokens, MAX_TOKENS * sizeof(*tokarray.tokens));

	return qtop;
}

/**
 * @brief free queue topology object
 *
 * @param qtop pointer to QTOP object
 *
 * @return none
 */
void
ocs_hw_qtop_free(ocs_hw_qtop_t *qtop)
{
	if (qtop != NULL) {
		if (qtop->entries != NULL) {
			ocs_free(qtop->os, qtop->entries, qtop->alloc_count*sizeof(*qtop->entries));
		}
		ocs_free(qtop->os, qtop, sizeof(*qtop));
	}
}

/* Uncomment this to turn on RQ debug */
// #define ENABLE_DEBUG_RQBUF

static int32_t ocs_hw_rqpair_find(ocs_hw_t *hw, uint16_t rq_id);
static ocs_hw_sequence_t * ocs_hw_rqpair_get(ocs_hw_t *hw, uint16_t rqindex, uint16_t bufindex);
static int32_t ocs_hw_rqpair_put(ocs_hw_t *hw, ocs_hw_sequence_t *seq);
static ocs_hw_rtn_e ocs_hw_rqpair_auto_xfer_rdy_buffer_sequence_reset(ocs_hw_t *hw, ocs_hw_sequence_t *seq);

/**
 * @brief Process receive queue completions for RQ Pair mode.
 *
 * @par Description
 * RQ completions are processed. In RQ pair mode, a single header and single payload
 * buffer are received, and passed to the function that has registered for unsolicited
 * callbacks.
 *
 * @param hw Hardware context.
 * @param cq Pointer to HW completion queue.
 * @param cqe Completion queue entry.
 *
 * @return Returns 0 for success, or a negative error code value for failure.
 */

int32_t
ocs_hw_rqpair_process_rq(ocs_hw_t *hw, hw_cq_t *cq, uint8_t *cqe)
{
	uint16_t rq_id;
	uint32_t index;
	int32_t rqindex;
	int32_t	 rq_status;
	uint32_t h_len;
	uint32_t p_len;
	ocs_hw_sequence_t *seq;

	rq_status = sli_fc_rqe_rqid_and_index(&hw->sli, cqe, &rq_id, &index);
	if (0 != rq_status) {
		switch (rq_status) {
		case SLI4_FC_ASYNC_RQ_BUF_LEN_EXCEEDED:
		case SLI4_FC_ASYNC_RQ_DMA_FAILURE:
			/* just get RQ buffer then return to chip */
			rqindex = ocs_hw_rqpair_find(hw, rq_id);
			if (rqindex < 0) {
				ocs_log_test(hw->os, "status=%#x: rq_id lookup failed for id=%#x\n",
					     rq_status, rq_id);
				break;
			}

			/* get RQ buffer */
			seq = ocs_hw_rqpair_get(hw, rqindex, index);

			/* return to chip */
			if (ocs_hw_rqpair_sequence_free(hw, seq)) {
				ocs_log_test(hw->os, "status=%#x, failed to return buffers to RQ\n",
					     rq_status);
				break;
			}
			break;
		case SLI4_FC_ASYNC_RQ_INSUFF_BUF_NEEDED:
		case SLI4_FC_ASYNC_RQ_INSUFF_BUF_FRM_DISC:
			/* since RQ buffers were not consumed, cannot return them to chip */
			/* fall through */
			ocs_log_debug(hw->os, "Warning: RCQE status=%#x, \n", rq_status);
		default:
			break;
		}
		return -1;
	}

	rqindex = ocs_hw_rqpair_find(hw, rq_id);
	if (rqindex < 0) {
		ocs_log_test(hw->os, "Error: rq_id lookup failed for id=%#x\n", rq_id);
		return -1;
	}

	OCS_STAT({ hw_rq_t *rq = hw->hw_rq[hw->hw_rq_lookup[rqindex]]; rq->use_count++; rq->hdr_use_count++;
		 rq->payload_use_count++;})

	seq = ocs_hw_rqpair_get(hw, rqindex, index);
	ocs_hw_assert(seq != NULL);

	seq->hw = hw;
	seq->auto_xrdy = 0;
	seq->out_of_xris = 0;
	seq->xri = 0;
	seq->hio = NULL;

	sli_fc_rqe_length(&hw->sli, cqe, &h_len, &p_len);
	seq->header->dma.len = h_len;
	seq->payload->dma.len = p_len;
	seq->fcfi = sli_fc_rqe_fcfi(&hw->sli, cqe);
	seq->hw_priv = cq->eq;

	/* bounce enabled, single RQ, we snoop the ox_id to choose the cpuidx */
	if (hw->config.bounce) {
		fc_header_t *hdr = seq->header->dma.virt;
		uint32_t s_id = fc_be24toh(hdr->s_id);
		uint32_t d_id = fc_be24toh(hdr->d_id);
		uint32_t ox_id =  ocs_be16toh(hdr->ox_id);
		if (hw->callback.bounce != NULL) {
			(*hw->callback.bounce)(ocs_hw_unsol_process_bounce, seq, s_id, d_id, ox_id);
		}
	} else {
		hw->callback.unsolicited(hw->args.unsolicited, seq);
	}

	return 0;
}

/**
 * @brief Process receive queue completions for RQ Pair mode - Auto xfer rdy
 *
 * @par Description
 * RQ completions are processed. In RQ pair mode, a single header and single payload
 * buffer are received, and passed to the function that has registered for unsolicited
 * callbacks.
 *
 * @param hw Hardware context.
 * @param cq Pointer to HW completion queue.
 * @param cqe Completion queue entry.
 *
 * @return Returns 0 for success, or a negative error code value for failure.
 */

int32_t
ocs_hw_rqpair_process_auto_xfr_rdy_cmd(ocs_hw_t *hw, hw_cq_t *cq, uint8_t *cqe)
{
	/* Seems silly to call a SLI function to decode - use the structure directly for performance */
	sli4_fc_optimized_write_cmd_cqe_t *opt_wr = (sli4_fc_optimized_write_cmd_cqe_t*)cqe;
	uint16_t rq_id;
	uint32_t index;
	int32_t rqindex;
	int32_t	 rq_status;
	uint32_t h_len;
	uint32_t p_len;
	ocs_hw_sequence_t *seq;
	uint8_t axr_lock_taken = 0;
#if defined(OCS_DISC_SPIN_DELAY)
	uint32_t 	delay = 0;
	char 		prop_buf[32];
#endif

	rq_status = sli_fc_rqe_rqid_and_index(&hw->sli, cqe, &rq_id, &index);
	if (0 != rq_status) {
		switch (rq_status) {
		case SLI4_FC_ASYNC_RQ_BUF_LEN_EXCEEDED:
		case SLI4_FC_ASYNC_RQ_DMA_FAILURE:
			/* just get RQ buffer then return to chip */
			rqindex = ocs_hw_rqpair_find(hw, rq_id);
			if (rqindex < 0) {
				ocs_log_err(hw->os, "status=%#x: rq_id lookup failed for id=%#x\n",
					    rq_status, rq_id);
				break;
			}

			/* get RQ buffer */
			seq = ocs_hw_rqpair_get(hw, rqindex, index);

			/* return to chip */
			if (ocs_hw_rqpair_sequence_free(hw, seq)) {
				ocs_log_err(hw->os, "status=%#x, failed to return buffers to RQ\n",
					    rq_status);
				break;
			}
			break;
		case SLI4_FC_ASYNC_RQ_INSUFF_BUF_NEEDED:
		case SLI4_FC_ASYNC_RQ_INSUFF_BUF_FRM_DISC:
			/* since RQ buffers were not consumed, cannot return them to chip */
			ocs_log_debug(hw->os, "Warning: RCQE status=%#x, \n", rq_status);
			/* fall through */
		default:
			break;
		}
		return -1;
	}

	rqindex = ocs_hw_rqpair_find(hw, rq_id);
	if (rqindex < 0) {
		ocs_log_err(hw->os, "Error: rq_id lookup failed for id=%#x\n", rq_id);
		return -1;
	}

	OCS_STAT({ hw_rq_t *rq = hw->hw_rq[hw->hw_rq_lookup[rqindex]]; rq->use_count++; rq->hdr_use_count++;
		 rq->payload_use_count++;})

	seq = ocs_hw_rqpair_get(hw, rqindex, index);
	ocs_hw_assert(seq != NULL);

	seq->hw = hw;
	seq->auto_xrdy = opt_wr->agxr;
	seq->out_of_xris = opt_wr->oox;
	seq->xri = opt_wr->xri;
	seq->hio = NULL;

	sli_fc_rqe_length(&hw->sli, cqe, &h_len, &p_len);
	seq->header->dma.len = h_len;
	seq->payload->dma.len = p_len;
	seq->fcfi = sli_fc_rqe_fcfi(&hw->sli, cqe);
	seq->hw_priv = cq->eq;

	if (seq->auto_xrdy) {
		fc_header_t *fc_hdr = seq->header->dma.virt;

		seq->hio = ocs_hw_io_lookup(hw, seq->xri);
		ocs_lock(&seq->hio->axr_lock);
		axr_lock_taken = 1;

		/* save the FCFI, src_id, dest_id and ox_id because we need it for the sequence object when the data comes. */
		seq->hio->axr_buf->fcfi = seq->fcfi;
		seq->hio->axr_buf->hdr.ox_id = fc_hdr->ox_id;
		seq->hio->axr_buf->hdr.s_id = fc_hdr->s_id;
		seq->hio->axr_buf->hdr.d_id = fc_hdr->d_id;
		seq->hio->axr_buf->cmd_cqe = 1;

		/*
		 * Since auto xfer rdy is used for this IO, then clear the sequence
		 * initiative bit in the header so that the upper layers wait for the
		 * data. This should flow exactly like the first burst case.
		 */
		fc_hdr->f_ctl &= fc_htobe24(~FC_FCTL_SEQUENCE_INITIATIVE);

		/* If AXR CMD CQE came before previous TRSP CQE of same XRI */
		if (seq->hio->type == OCS_HW_IO_TARGET_RSP) {
			seq->hio->axr_buf->call_axr_cmd = 1;
			seq->hio->axr_buf->cmd_seq = seq;
			goto exit_ocs_hw_rqpair_process_auto_xfr_rdy_cmd;
		}
	}

	/* bounce enabled, single RQ, we snoop the ox_id to choose the cpuidx */
	if (hw->config.bounce) {
		fc_header_t *hdr = seq->header->dma.virt;
		uint32_t s_id = fc_be24toh(hdr->s_id);
		uint32_t d_id = fc_be24toh(hdr->d_id);
		uint32_t ox_id =  ocs_be16toh(hdr->ox_id);
		if (hw->callback.bounce != NULL) {
			(*hw->callback.bounce)(ocs_hw_unsol_process_bounce, seq, s_id, d_id, ox_id);
		}
	} else {
		hw->callback.unsolicited(hw->args.unsolicited, seq);
	}

	if (seq->auto_xrdy) {
		/* If data cqe came before cmd cqe in out of order in case of AXR */
		if(seq->hio->axr_buf->data_cqe == 1) {

#if defined(OCS_DISC_SPIN_DELAY)
			if (ocs_get_property("disk_spin_delay", prop_buf, sizeof(prop_buf)) == 0) {
				delay = ocs_strtoul(prop_buf, 0, 0);
				ocs_udelay(delay);
			}
#endif
			/* bounce enabled, single RQ, we snoop the ox_id to choose the cpuidx */
			if (hw->config.bounce) {
				fc_header_t *hdr = seq->header->dma.virt;
				uint32_t s_id = fc_be24toh(hdr->s_id);
				uint32_t d_id = fc_be24toh(hdr->d_id);
				uint32_t ox_id =  ocs_be16toh(hdr->ox_id);
				if (hw->callback.bounce != NULL) {
					(*hw->callback.bounce)(ocs_hw_unsol_process_bounce, &seq->hio->axr_buf->seq, s_id, d_id, ox_id);
				}
			} else {
				hw->callback.unsolicited(hw->args.unsolicited, &seq->hio->axr_buf->seq);
			}
		}
	}

exit_ocs_hw_rqpair_process_auto_xfr_rdy_cmd:
	if(axr_lock_taken) {
		ocs_unlock(&seq->hio->axr_lock);
	}
	return 0;
}

/**
 * @brief Process CQ completions for Auto xfer rdy data phases.
 *
 * @par Description
 * The data is DMA'd into the data buffer posted to the SGL prior to the XRI
 * being assigned to an IO. When the completion is received, All of the data
 * is in the single buffer.
 *
 * @param hw Hardware context.
 * @param cq Pointer to HW completion queue.
 * @param cqe Completion queue entry.
 *
 * @return Returns 0 for success, or a negative error code value for failure.
 */

int32_t
ocs_hw_rqpair_process_auto_xfr_rdy_data(ocs_hw_t *hw, hw_cq_t *cq, uint8_t *cqe)
{
	/* Seems silly to call a SLI function to decode - use the structure directly for performance */
	sli4_fc_optimized_write_data_cqe_t *opt_wr = (sli4_fc_optimized_write_data_cqe_t*)cqe;
	ocs_hw_sequence_t *seq;
	ocs_hw_io_t *io;
	ocs_hw_auto_xfer_rdy_buffer_t *buf;
#if defined(OCS_DISC_SPIN_DELAY)
	uint32_t 	delay = 0;
	char 		prop_buf[32];
#endif
	/* Look up the IO */
	io = ocs_hw_io_lookup(hw, opt_wr->xri);
	ocs_lock(&io->axr_lock);
	buf = io->axr_buf;
	buf->data_cqe = 1;
	seq = &buf->seq;
	seq->hw = hw;
	seq->auto_xrdy = 1;
	seq->out_of_xris = 0;
	seq->xri = opt_wr->xri;
	seq->hio = io;
	seq->header = &buf->header;
	seq->payload = &buf->payload;

	seq->header->dma.len = sizeof(fc_header_t);
	seq->payload->dma.len = opt_wr->total_data_placed;
	seq->fcfi = buf->fcfi;
	seq->hw_priv = cq->eq;


	if (opt_wr->status == SLI4_FC_WCQE_STATUS_SUCCESS) {
		seq->status = OCS_HW_UNSOL_SUCCESS;
	} else if (opt_wr->status == SLI4_FC_WCQE_STATUS_REMOTE_STOP) {
		seq->status = OCS_HW_UNSOL_ABTS_RCVD;
	} else {
		seq->status = OCS_HW_UNSOL_ERROR;
	}

 	/* If AXR CMD CQE came before previous TRSP CQE of same XRI */
	if(io->type == OCS_HW_IO_TARGET_RSP) {
		io->axr_buf->call_axr_data = 1;
		goto exit_ocs_hw_rqpair_process_auto_xfr_rdy_data;
	}

	if(!buf->cmd_cqe) {
		/* if data cqe came before cmd cqe, return here, cmd cqe will handle */
		goto exit_ocs_hw_rqpair_process_auto_xfr_rdy_data;
	}
#if defined(OCS_DISC_SPIN_DELAY)
	if (ocs_get_property("disk_spin_delay", prop_buf, sizeof(prop_buf)) == 0) {
		delay = ocs_strtoul(prop_buf, 0, 0);
		ocs_udelay(delay);
	}
#endif

	/* bounce enabled, single RQ, we snoop the ox_id to choose the cpuidx */
	if (hw->config.bounce) {
		fc_header_t *hdr = seq->header->dma.virt;
		uint32_t s_id = fc_be24toh(hdr->s_id);
		uint32_t d_id = fc_be24toh(hdr->d_id);
		uint32_t ox_id =  ocs_be16toh(hdr->ox_id);
		if (hw->callback.bounce != NULL) {
			(*hw->callback.bounce)(ocs_hw_unsol_process_bounce, seq, s_id, d_id, ox_id);
		}
	} else {
		hw->callback.unsolicited(hw->args.unsolicited, seq);
	}

exit_ocs_hw_rqpair_process_auto_xfr_rdy_data:
	ocs_unlock(&io->axr_lock);
	return 0;
}

/**
 * @brief Return pointer to RQ buffer entry.
 *
 * @par Description
 * Returns a pointer to the RQ buffer entry given by @c rqindex and @c bufindex.
 *
 * @param hw Hardware context.
 * @param rqindex Index of the RQ that is being processed.
 * @param bufindex Index into the RQ that is being processed.
 *
 * @return Pointer to the sequence structure, or NULL otherwise.
 */
static ocs_hw_sequence_t *
ocs_hw_rqpair_get(ocs_hw_t *hw, uint16_t rqindex, uint16_t bufindex)
{
	sli4_queue_t *rq_hdr = &hw->rq[rqindex];
	sli4_queue_t *rq_payload = &hw->rq[rqindex+1];
	ocs_hw_sequence_t *seq = NULL;
	hw_rq_t *rq = hw->hw_rq[hw->hw_rq_lookup[rqindex]];

#if defined(ENABLE_DEBUG_RQBUF)
	uint64_t rqbuf_debug_value = 0xdead0000 | ((rq->id & 0xf) << 12) | (bufindex & 0xfff);
#endif

	if (bufindex >= rq_hdr->length) {
		ocs_log_err(hw->os, "RQ index %d bufindex %d exceed ring length %d for id %d\n",
			    rqindex, bufindex, rq_hdr->length, rq_hdr->id);
		return NULL;
	}

	sli_queue_lock(rq_hdr);
	sli_queue_lock(rq_payload);

#if defined(ENABLE_DEBUG_RQBUF)
	/* Put a debug value into the rq, to track which entries are still valid */
	_sli_queue_poke(&hw->sli, rq_hdr, bufindex, (uint8_t *)&rqbuf_debug_value);
	_sli_queue_poke(&hw->sli, rq_payload, bufindex, (uint8_t *)&rqbuf_debug_value);
#endif

	seq = rq->rq_tracker[bufindex];
	rq->rq_tracker[bufindex] = NULL;

	if (seq == NULL ) {
		ocs_log_err(hw->os, "RQ buffer NULL, rqindex %d, bufindex %d, current q index = %d\n",
			    rqindex, bufindex, rq_hdr->index);
	}

	sli_queue_unlock(rq_payload);
	sli_queue_unlock(rq_hdr);
	return seq;
}

/**
 * @brief Posts an RQ buffer to a queue and update the verification structures
 *
 * @param hw		hardware context
 * @param seq Pointer to sequence object.
 *
 * @return Returns 0 on success, or a non-zero value otherwise.
 */
static int32_t
ocs_hw_rqpair_put(ocs_hw_t *hw, ocs_hw_sequence_t *seq)
{
	sli4_queue_t *rq_hdr = &hw->rq[seq->header->rqindex];
	sli4_queue_t *rq_payload = &hw->rq[seq->payload->rqindex];
	uint32_t hw_rq_index = hw->hw_rq_lookup[seq->header->rqindex];
	hw_rq_t *rq = hw->hw_rq[hw_rq_index];
	uint32_t     phys_hdr[2];
	uint32_t     phys_payload[2];
	int32_t      qindex_hdr;
	int32_t      qindex_payload;

	/* Update the RQ verification lookup tables */
	phys_hdr[0] = ocs_addr32_hi(seq->header->dma.phys);
	phys_hdr[1] = ocs_addr32_lo(seq->header->dma.phys);
	phys_payload[0] = ocs_addr32_hi(seq->payload->dma.phys);
	phys_payload[1] = ocs_addr32_lo(seq->payload->dma.phys);

	sli_queue_lock(rq_hdr);
	sli_queue_lock(rq_payload);

	/*
	 * Note: The header must be posted last for buffer pair mode because
	 *       posting on the header queue posts the payload queue as well.
	 *       We do not ring the payload queue independently in RQ pair mode.
	 */
	qindex_payload = _sli_queue_write(&hw->sli, rq_payload, (void *)phys_payload);
	qindex_hdr = _sli_queue_write(&hw->sli, rq_hdr, (void *)phys_hdr);
	if (qindex_hdr < 0 ||
	    qindex_payload < 0) {
		ocs_log_err(hw->os, "RQ_ID=%#x write failed\n", rq_hdr->id);
		sli_queue_unlock(rq_payload);
		sli_queue_unlock(rq_hdr);
		return OCS_HW_RTN_ERROR;
	}

	/* ensure the indexes are the same */
	ocs_hw_assert(qindex_hdr == qindex_payload);

	/* Update the lookup table */
	if (rq->rq_tracker[qindex_hdr] == NULL) {
		rq->rq_tracker[qindex_hdr] = seq;
	} else {
		ocs_log_test(hw->os, "expected rq_tracker[%d][%d] buffer to be NULL\n",
			     hw_rq_index, qindex_hdr);
	}

	sli_queue_unlock(rq_payload);
	sli_queue_unlock(rq_hdr);
	return OCS_HW_RTN_SUCCESS;
}

/**
 * @brief Return RQ buffers (while in RQ pair mode).
 *
 * @par Description
 * The header and payload buffers are returned to the Receive Queue.
 *
 * @param hw Hardware context.
 * @param seq Header/payload sequence buffers.
 *
 * @return Returns OCS_HW_RTN_SUCCESS on success, or an error code value on failure.
 */

ocs_hw_rtn_e
ocs_hw_rqpair_sequence_free(ocs_hw_t *hw, ocs_hw_sequence_t *seq)
{
	ocs_hw_rtn_e   rc = OCS_HW_RTN_SUCCESS;

	/* Check for auto xfer rdy dummy buffers and call the proper release function. */
	if (seq->header->rqindex == OCS_HW_RQ_INDEX_DUMMY_HDR) {
		return ocs_hw_rqpair_auto_xfer_rdy_buffer_sequence_reset(hw, seq);
	}

	/*
	 * Post the data buffer first. Because in RQ pair mode, ringing the
	 * doorbell of the header ring will post the data buffer as well.
	 */
	if (ocs_hw_rqpair_put(hw, seq)) {
		ocs_log_err(hw->os, "error writing buffers\n");
		return OCS_HW_RTN_ERROR;
	}

	return rc;
}

/**
 * @brief Find the RQ index of RQ_ID.
 *
 * @param hw Hardware context.
 * @param rq_id RQ ID to find.
 *
 * @return Returns the RQ index, or -1 if not found
 */
static inline int32_t
ocs_hw_rqpair_find(ocs_hw_t *hw, uint16_t rq_id)
{
	return ocs_hw_queue_hash_find(hw->rq_hash, rq_id);
}

/**
 * @ingroup devInitShutdown
 * @brief Allocate auto xfer rdy buffers.
 *
 * @par Description
 * Allocates the auto xfer rdy buffers and places them on the free list.
 *
 * @param hw Hardware context allocated by the caller.
 * @param num_buffers Number of buffers to allocate.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
ocs_hw_rtn_e
ocs_hw_rqpair_auto_xfer_rdy_buffer_alloc(ocs_hw_t *hw, uint32_t num_buffers)
{
	ocs_hw_auto_xfer_rdy_buffer_t *buf;
	uint32_t i;

	hw->auto_xfer_rdy_buf_pool = ocs_pool_alloc(hw->os, sizeof(ocs_hw_auto_xfer_rdy_buffer_t), num_buffers, FALSE);
	if (hw->auto_xfer_rdy_buf_pool == NULL) {
		ocs_log_err(hw->os, "Failure to allocate auto xfer ready buffer pool\n");
		return OCS_HW_RTN_NO_MEMORY;
	}

	for (i = 0; i < num_buffers; i++) {
		/* allocate the wrapper object */
		buf = ocs_pool_get_instance(hw->auto_xfer_rdy_buf_pool, i);
		ocs_hw_assert(buf != NULL);

		/* allocate the auto xfer ready buffer */
		if (ocs_dma_alloc(hw->os, &buf->payload.dma, hw->config.auto_xfer_rdy_size, OCS_MIN_DMA_ALIGNMENT)) {
			ocs_log_err(hw->os, "DMA allocation failed\n");
			ocs_free(hw->os, buf, sizeof(*buf));
			return OCS_HW_RTN_NO_MEMORY;
		}

		/* build a fake data header in big endian */
		buf->hdr.info = FC_RCTL_INFO_SOL_DATA;
		buf->hdr.r_ctl = FC_RCTL_FC4_DATA;
		buf->hdr.type = FC_TYPE_FCP;
		buf->hdr.f_ctl = fc_htobe24(FC_FCTL_EXCHANGE_RESPONDER |
					    FC_FCTL_FIRST_SEQUENCE |
					    FC_FCTL_LAST_SEQUENCE |
					    FC_FCTL_END_SEQUENCE |
					    FC_FCTL_SEQUENCE_INITIATIVE);

		/* build the fake header DMA object */
		buf->header.rqindex = OCS_HW_RQ_INDEX_DUMMY_HDR;
		buf->header.dma.virt = &buf->hdr;
		buf->header.dma.alloc = buf;
		buf->header.dma.size = sizeof(buf->hdr);
		buf->header.dma.len = sizeof(buf->hdr);

		buf->payload.rqindex = OCS_HW_RQ_INDEX_DUMMY_DATA;
	}
	return OCS_HW_RTN_SUCCESS;
}

/**
 * @ingroup devInitShutdown
 * @brief Post Auto xfer rdy buffers to the XRIs posted with DNRX.
 *
 * @par Description
 * When new buffers are freed, check existing XRIs waiting for buffers.
 *
 * @param hw Hardware context allocated by the caller.
 */
static void
ocs_hw_rqpair_auto_xfer_rdy_dnrx_check(ocs_hw_t *hw)
{
	ocs_hw_io_t *io;
	int32_t rc;

	ocs_lock(&hw->io_lock);

	while (!ocs_list_empty(&hw->io_port_dnrx)) {
		io = ocs_list_remove_head(&hw->io_port_dnrx);
		rc = ocs_hw_reque_xri(hw, io);
		if(rc) {
			break;
		}
	}

	ocs_unlock(&hw->io_lock);
}

/**
 * @brief Called when the POST_SGL_PAGE command completes.
 *
 * @par Description
 * Free the mailbox command buffer.
 *
 * @param hw Hardware context.
 * @param status Status field from the mbox completion.
 * @param mqe Mailbox response structure.
 * @param arg Pointer to a callback function that signals the caller that the command is done.
 *
 * @return Returns 0.
 */
static int32_t
ocs_hw_rqpair_auto_xfer_rdy_move_to_port_cb(ocs_hw_t *hw, int32_t status, uint8_t *mqe, void  *arg)
{
	if (status != 0) {
		ocs_log_debug(hw->os, "Status 0x%x\n", status);
	}

	ocs_free(hw->os, mqe, SLI4_BMBX_SIZE);
	return 0;
}

/**
 * @brief Prepares an XRI to move to the chip.
 *
 * @par Description
 * Puts the data SGL into the SGL list for the IO object and possibly registers
 * an SGL list for the XRI. Since both the POST_XRI and POST_SGL_PAGES commands are
 * mailbox commands, we don't need to wait for completion before preceding.
 *
 * @param hw Hardware context allocated by the caller.
 * @param io Pointer to the IO object.
 *
 * @return Returns OCS_HW_RTN_SUCCESS for success, or an error code value for failure.
 */
ocs_hw_rtn_e
ocs_hw_rqpair_auto_xfer_rdy_move_to_port(ocs_hw_t *hw, ocs_hw_io_t *io)
{
	/* We only need to preregister the SGL if it has not yet been done. */
	if (!sli_get_sgl_preregister(&hw->sli)) {
		uint8_t	*post_sgl;
		ocs_dma_t *psgls = &io->def_sgl;
		ocs_dma_t **sgls = &psgls;

		/* non-local buffer required for mailbox queue */
		post_sgl = ocs_malloc(hw->os, SLI4_BMBX_SIZE, OCS_M_NOWAIT);
		if (post_sgl == NULL) {
			ocs_log_err(hw->os, "no buffer for command\n");
			return OCS_HW_RTN_NO_MEMORY;
		}
		if (sli_cmd_fcoe_post_sgl_pages(&hw->sli, post_sgl, SLI4_BMBX_SIZE,
						io->indicator, 1, sgls, NULL, NULL)) {
			if (ocs_hw_command(hw, post_sgl, OCS_CMD_NOWAIT,
					    ocs_hw_rqpair_auto_xfer_rdy_move_to_port_cb, NULL)) {
				ocs_free(hw->os, post_sgl, SLI4_BMBX_SIZE);
				ocs_log_err(hw->os, "SGL post failed\n");
				return OCS_HW_RTN_ERROR;
			}
		}
	}

	ocs_lock(&hw->io_lock);
	if (ocs_hw_rqpair_auto_xfer_rdy_buffer_post(hw, io, 0) != 0) { /* DNRX set - no buffer */
		ocs_unlock(&hw->io_lock);
		return OCS_HW_RTN_ERROR;
	}
	ocs_unlock(&hw->io_lock);
	return OCS_HW_RTN_SUCCESS;
}

/**
 * @brief Prepares an XRI to move back to the host.
 *
 * @par Description
 * Releases any attached buffer back to the pool.
 *
 * @param hw Hardware context allocated by the caller.
 * @param io Pointer to the IO object.
 */
void
ocs_hw_rqpair_auto_xfer_rdy_move_to_host(ocs_hw_t *hw, ocs_hw_io_t *io)
{
	if (io->axr_buf != NULL) {
		ocs_lock(&hw->io_lock);
			/* check  list and remove if there */
			if (ocs_list_on_list(&io->dnrx_link)) {
				ocs_list_remove(&hw->io_port_dnrx, io);
				io->auto_xfer_rdy_dnrx = 0;

				/* release the count for waiting for a buffer */
				ocs_hw_io_free(hw, io);
			}

			ocs_pool_put(hw->auto_xfer_rdy_buf_pool, io->axr_buf);
			io->axr_buf = NULL;
		ocs_unlock(&hw->io_lock);

		ocs_hw_rqpair_auto_xfer_rdy_dnrx_check(hw);
	}
	return;
}


/**
 * @brief Posts an auto xfer rdy buffer to an IO.
 *
 * @par Description
 * Puts the data SGL into the SGL list for the IO object
 * @n @name
 * @b Note: io_lock must be held.
 *
 * @param hw Hardware context allocated by the caller.
 * @param io Pointer to the IO object.
 *
 * @return Returns the value of DNRX bit in the TRSP and ABORT WQEs.
 */
uint8_t
ocs_hw_rqpair_auto_xfer_rdy_buffer_post(ocs_hw_t *hw, ocs_hw_io_t *io, int reuse_buf)
{
	ocs_hw_auto_xfer_rdy_buffer_t *buf;
	sli4_sge_t	*data;

	if(!reuse_buf) {
		buf = ocs_pool_get(hw->auto_xfer_rdy_buf_pool);
		io->axr_buf = buf;
	}

	data = io->def_sgl.virt;
	data[0].sge_type = SLI4_SGE_TYPE_SKIP;
	data[0].last = 0;

	/*
	 * Note: if we are doing DIF assists, then the SGE[1] must contain the
	 * DI_SEED SGE. The host is responsible for programming:
	 *   SGE Type (Word 2, bits 30:27)
	 *   Replacement App Tag (Word 2 bits 15:0)
	 *   App Tag (Word 3 bits 15:0)
	 *   New Ref Tag (Word 3 bit 23)
	 *   Metadata Enable (Word 3 bit 20)
	 *   Auto-Increment RefTag (Word 3 bit 19)
	 *   Block Size (Word 3 bits 18:16)
	 * The following fields are managed by the SLI Port:
	 *    Ref Tag Compare (Word 0)
	 *    Replacement Ref Tag (Word 1) - In not the LBA
	 *    NA (Word 2 bit 25)
	 *    Opcode RX (Word 3 bits 27:24)
	 *    Checksum Enable (Word 3 bit 22)
	 *    RefTag Enable (Word 3 bit 21)
	 *
	 * The first two SGLs are cleared by ocs_hw_io_init_sges(), so assume eveything is cleared.
	 */
	if (hw->config.auto_xfer_rdy_p_type) {
		sli4_diseed_sge_t *diseed = (sli4_diseed_sge_t*)&data[1];

		diseed->sge_type = SLI4_SGE_TYPE_DISEED;
		diseed->repl_app_tag = hw->config.auto_xfer_rdy_app_tag_value;
		diseed->app_tag_cmp = hw->config.auto_xfer_rdy_app_tag_value;
		diseed->check_app_tag = hw->config.auto_xfer_rdy_app_tag_valid;
		diseed->auto_incr_ref_tag = TRUE; /* Always the LBA */
		diseed->dif_blk_size = hw->config.auto_xfer_rdy_blk_size_chip;
	} else {
		data[1].sge_type = SLI4_SGE_TYPE_SKIP;
		data[1].last = 0;
	}

	data[2].sge_type = SLI4_SGE_TYPE_DATA;
	data[2].buffer_address_high = ocs_addr32_hi(io->axr_buf->payload.dma.phys);
	data[2].buffer_address_low  = ocs_addr32_lo(io->axr_buf->payload.dma.phys);
	data[2].buffer_length = io->axr_buf->payload.dma.size;
	data[2].last = TRUE;
	data[3].sge_type = SLI4_SGE_TYPE_SKIP;

	return 0;
}

/**
 * @brief Return auto xfer ready buffers (while in RQ pair mode).
 *
 * @par Description
 * The header and payload buffers are returned to the auto xfer rdy pool.
 *
 * @param hw Hardware context.
 * @param seq Header/payload sequence buffers.
 *
 * @return Returns OCS_HW_RTN_SUCCESS for success, an error code value for failure.
 */

static ocs_hw_rtn_e
ocs_hw_rqpair_auto_xfer_rdy_buffer_sequence_reset(ocs_hw_t *hw, ocs_hw_sequence_t *seq)
{
	ocs_hw_auto_xfer_rdy_buffer_t *buf = seq->header->dma.alloc;

	buf->data_cqe = 0;
	buf->cmd_cqe = 0;
	buf->fcfi = 0;
	buf->call_axr_cmd = 0;
	buf->call_axr_data = 0;

	/* build a fake data header in big endian */
	buf->hdr.info = FC_RCTL_INFO_SOL_DATA;
	buf->hdr.r_ctl = FC_RCTL_FC4_DATA;
	buf->hdr.type = FC_TYPE_FCP;
	buf->hdr.f_ctl = fc_htobe24(FC_FCTL_EXCHANGE_RESPONDER |
					FC_FCTL_FIRST_SEQUENCE |
					FC_FCTL_LAST_SEQUENCE |
					FC_FCTL_END_SEQUENCE |
					FC_FCTL_SEQUENCE_INITIATIVE);

	/* build the fake header DMA object */
	buf->header.rqindex = OCS_HW_RQ_INDEX_DUMMY_HDR;
	buf->header.dma.virt = &buf->hdr;
	buf->header.dma.alloc = buf;
	buf->header.dma.size = sizeof(buf->hdr);
	buf->header.dma.len = sizeof(buf->hdr);
	buf->payload.rqindex = OCS_HW_RQ_INDEX_DUMMY_DATA;

	ocs_hw_rqpair_auto_xfer_rdy_dnrx_check(hw);

	return OCS_HW_RTN_SUCCESS;
}

/**
 * @ingroup devInitShutdown
 * @brief Free auto xfer rdy buffers.
 *
 * @par Description
 * Frees the auto xfer rdy buffers.
 *
 * @param hw Hardware context allocated by the caller.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
static void
ocs_hw_rqpair_auto_xfer_rdy_buffer_free(ocs_hw_t *hw)
{
	ocs_hw_auto_xfer_rdy_buffer_t *buf;
	uint32_t i;

	if (hw->auto_xfer_rdy_buf_pool != NULL) {
		ocs_lock(&hw->io_lock);
			for (i = 0; i < ocs_pool_get_count(hw->auto_xfer_rdy_buf_pool); i++) {
				buf = ocs_pool_get_instance(hw->auto_xfer_rdy_buf_pool, i);
				if (buf != NULL) {
					ocs_dma_free(hw->os, &buf->payload.dma);
				}
			}
		ocs_unlock(&hw->io_lock);

		ocs_pool_free(hw->auto_xfer_rdy_buf_pool);
		hw->auto_xfer_rdy_buf_pool = NULL;
	}
}

/**
 * @ingroup devInitShutdown
 * @brief Configure the rq_pair function from ocs_hw_init().
 *
 * @par Description
 * Allocates the buffers to auto xfer rdy and posts initial XRIs for this feature.
 *
 * @param hw Hardware context allocated by the caller.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
ocs_hw_rtn_e
ocs_hw_rqpair_init(ocs_hw_t *hw)
{
	ocs_hw_rtn_e	rc;
	uint32_t xris_posted;

	ocs_log_debug(hw->os, "RQ Pair mode\n");

	/*
	 * If we get this far, the auto XFR_RDY feature was enabled successfully, otherwise ocs_hw_init() would
	 * return with an error. So allocate the buffers based on the initial XRI pool required to support this
	 * feature.
	 */
	if (sli_get_auto_xfer_rdy_capable(&hw->sli) &&
	    hw->config.auto_xfer_rdy_size > 0) {
		if (hw->auto_xfer_rdy_buf_pool == NULL) {
			/*
			 * Allocate one more buffer than XRIs so that when all the XRIs are in use, we still have
			 * one to post back for the case where the response phase is started in the context of
			 * the data completion.
			 */
			rc = ocs_hw_rqpair_auto_xfer_rdy_buffer_alloc(hw, hw->config.auto_xfer_rdy_xri_cnt + 1);
			if (rc != OCS_HW_RTN_SUCCESS) {
				return rc;
			}
		} else {
			ocs_pool_reset(hw->auto_xfer_rdy_buf_pool);
		}

		/* Post the auto XFR_RDY XRIs */
		xris_posted = ocs_hw_xri_move_to_port_owned(hw, hw->config.auto_xfer_rdy_xri_cnt);
		if (xris_posted != hw->config.auto_xfer_rdy_xri_cnt) {
			ocs_log_err(hw->os, "post_xri failed, only posted %d XRIs\n", xris_posted);
			return OCS_HW_RTN_ERROR;
		}
	}

	return 0;
}

/**
 * @ingroup devInitShutdown
 * @brief Tear down the rq_pair function from ocs_hw_teardown().
 *
 * @par Description
 * Frees the buffers to auto xfer rdy.
 *
 * @param hw Hardware context allocated by the caller.
 */
void
ocs_hw_rqpair_teardown(ocs_hw_t *hw)
{
	/* We need to free any auto xfer ready buffers */
	ocs_hw_rqpair_auto_xfer_rdy_buffer_free(hw);
}
