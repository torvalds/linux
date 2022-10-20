// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 */

#include "walt.h"

static inline void __window_data(u32 *dst, u32 *src)
{
	if (src)
		memcpy(dst, src, nr_cpu_ids * sizeof(u32));
	else
		memset(dst, 0, nr_cpu_ids * sizeof(u32));
}

struct trace_seq;
const char *__window_print(struct trace_seq *p, const u32 *buf, int buf_len)
{
	int i;
	const char *ret = p->buffer + seq_buf_used(&p->seq);

	for (i = 0; i < buf_len; i++)
		trace_seq_printf(p, "%u ", buf[i]);

	trace_seq_putc(p, 0);

	return ret;
}

static inline s64 __rq_update_sum(struct rq *rq, bool curr, bool new)
{
	struct walt_rq *wrq = &per_cpu(walt_rq, cpu_of(rq));

	if (curr)
		if (new)
			return wrq->nt_curr_runnable_sum;
		else
			return wrq->curr_runnable_sum;
	else
		if (new)
			return wrq->nt_prev_runnable_sum;
		else
			return wrq->prev_runnable_sum;
}

static inline s64 __grp_update_sum(struct rq *rq, bool curr, bool new)
{
	struct walt_rq *wrq = &per_cpu(walt_rq, cpu_of(rq));

	if (curr)
		if (new)
			return wrq->grp_time.nt_curr_runnable_sum;
		else
			return wrq->grp_time.curr_runnable_sum;
	else
		if (new)
			return wrq->grp_time.nt_prev_runnable_sum;
		else
			return wrq->grp_time.prev_runnable_sum;
}

static inline s64
__get_update_sum(struct rq *rq, enum migrate_types migrate_type,
		 bool src, bool new, bool curr)
{
	switch (migrate_type) {
	case RQ_TO_GROUP:
		if (src)
			return __rq_update_sum(rq, curr, new);
		else
			return __grp_update_sum(rq, curr, new);
	case GROUP_TO_RQ:
		if (src)
			return __grp_update_sum(rq, curr, new);
		else
			return __rq_update_sum(rq, curr, new);
	default:
		WARN_ON_ONCE(1);
		return -EINVAL;
	}
}

#define CREATE_TRACE_POINTS
#include "trace.h"
