/*-
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0
 *
 * Copyright (c) 2017 Mellanox Technologies Ltd.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <linux/module.h>
#include <linux/err.h>
#include <linux/slab.h>

#include <rdma/ib_verbs.h>

#define	IB_CQ_POLL_MAX	16
/* maximum number of completions per poll loop */
#define	IB_CQ_POLL_BUDGET 65536
#define	IB_CQ_POLL_FLAGS (IB_CQ_NEXT_COMP | IB_CQ_REPORT_MISSED_EVENTS)

static void
ib_cq_poll_work(struct work_struct *work)
{
	struct ib_wc ib_wc[IB_CQ_POLL_MAX];
	struct ib_cq *cq = container_of(work, struct ib_cq, work);
	int total = 0;
	int i;
	int n;

	while (1) {
		n = ib_poll_cq(cq, IB_CQ_POLL_MAX, ib_wc);
		for (i = 0; i < n; i++) {
			struct ib_wc *wc = ib_wc + i;

			if (wc->wr_cqe != NULL)
				wc->wr_cqe->done(cq, wc);
		}

		if (n != IB_CQ_POLL_MAX) {
			if (ib_req_notify_cq(cq, IB_CQ_POLL_FLAGS) > 0)
				break;
			else
				return;
		}
		total += n;
		if (total >= IB_CQ_POLL_BUDGET)
			break;
	}

	/* give other work structs a chance */
	queue_work(ib_comp_wq, &cq->work);
}

static void
ib_cq_completion_workqueue(struct ib_cq *cq, void *private)
{
	queue_work(ib_comp_wq, &cq->work);
}

struct ib_cq *
ib_alloc_cq(struct ib_device *dev, void *private,
    int nr_cqe, int comp_vector, enum ib_poll_context poll_ctx)
{
	struct ib_cq_init_attr cq_attr = {
		.cqe = nr_cqe,
		.comp_vector = comp_vector,
	};
	struct ib_cq *cq;

	/*
	 * Check for invalid parameters early on to avoid
	 * extra error handling code:
	 */
	switch (poll_ctx) {
	case IB_POLL_DIRECT:
	case IB_POLL_SOFTIRQ:
	case IB_POLL_WORKQUEUE:
		break;
	default:
		return (ERR_PTR(-EINVAL));
	}

	cq = dev->create_cq(dev, &cq_attr, NULL, NULL);
	if (IS_ERR(cq))
		return (cq);

	cq->device = dev;
	cq->uobject = NULL;
	cq->event_handler = NULL;
	cq->cq_context = private;
	cq->poll_ctx = poll_ctx;
	atomic_set(&cq->usecnt, 0);

	switch (poll_ctx) {
	case IB_POLL_DIRECT:
		cq->comp_handler = NULL;	/* no hardware completions */
		break;
	case IB_POLL_SOFTIRQ:
	case IB_POLL_WORKQUEUE:
		cq->comp_handler = ib_cq_completion_workqueue;
		INIT_WORK(&cq->work, ib_cq_poll_work);
		ib_req_notify_cq(cq, IB_CQ_NEXT_COMP);
		break;
	default:
		break;
	}
	return (cq);
}
EXPORT_SYMBOL(ib_alloc_cq);

void
ib_free_cq(struct ib_cq *cq)
{

	if (WARN_ON_ONCE(atomic_read(&cq->usecnt) != 0))
		return;

	switch (cq->poll_ctx) {
	case IB_POLL_DIRECT:
		break;
	case IB_POLL_SOFTIRQ:
	case IB_POLL_WORKQUEUE:
		flush_work(&cq->work);
		break;
	default:
		break;
	}

	(void)cq->device->destroy_cq(cq);
}
EXPORT_SYMBOL(ib_free_cq);
