// SPDX-License-Identifier: GPL-2.0
/*
 * Validate scx_bpf_dsq_reenq() semantics on user DSQs.
 *
 * A BPF timer periodically calls scx_bpf_dsq_reenq() on a user DSQ,
 * causing tasks to be re-enqueued through ops.enqueue() with SCX_ENQ_REENQ
 * set and SCX_TASK_REENQ_KFUNC recorded in p->scx.flags.
 *
 * The test verifies:
 *  - scx_bpf_dsq_reenq() triggers ops.enqueue() with SCX_ENQ_REENQ
 *  - The reenqueue reason is SCX_TASK_REENQ_KFUNC (bit 12 set)
 *  - Tasks are correctly re-dispatched after reenqueue
 */

#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";

UEI_DEFINE(uei);

#define USER_DSQ	0

/*
 * SCX_TASK_REENQ_REASON_MASK and SCX_TASK_REENQ_KFUNC are exported via
 * vmlinux BTF as part of enum scx_ent_flags.
 */

/* 5ms timer interval */
#define REENQ_TIMER_NS		(5 * 1000 * 1000ULL)

/*
 * Number of times ops.enqueue() was called with SCX_ENQ_REENQ set and
 * SCX_TASK_REENQ_KFUNC recorded in p->scx.flags.
 */
u64 nr_reenq_kfunc;

struct reenq_timer_val {
	struct bpf_timer timer;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, u32);
	__type(value, struct reenq_timer_val);
} reenq_timer SEC(".maps");

/*
 * Timer callback: reenqueue all tasks currently sitting on USER_DSQ back
 * through ops.enqueue() with SCX_ENQ_REENQ | SCX_TASK_REENQ_KFUNC.
 */
static int reenq_timerfn(void *map, int *key, struct bpf_timer *timer)
{
	scx_bpf_dsq_reenq(USER_DSQ, 0);
	bpf_timer_start(timer, REENQ_TIMER_NS, 0);
	return 0;
}

void BPF_STRUCT_OPS(dsq_reenq_enqueue, struct task_struct *p, u64 enq_flags)
{
	/*
	 * If this is a kfunc-triggered reenqueue, verify that
	 * SCX_TASK_REENQ_KFUNC is recorded in p->scx.flags.
	 */
	if (enq_flags & SCX_ENQ_REENQ) {
		u32 reason = p->scx.flags & SCX_TASK_REENQ_REASON_MASK;

		if (reason == SCX_TASK_REENQ_KFUNC)
			__sync_fetch_and_add(&nr_reenq_kfunc, 1);
	}

	/*
	 * Always dispatch to USER_DSQ so the timer can reenqueue tasks again
	 * on the next tick.
	 */
	scx_bpf_dsq_insert(p, USER_DSQ, SCX_SLICE_DFL, enq_flags);
}

void BPF_STRUCT_OPS(dsq_reenq_dispatch, s32 cpu, struct task_struct *prev)
{
	scx_bpf_dsq_move_to_local(USER_DSQ, 0);
}

s32 BPF_STRUCT_OPS_SLEEPABLE(dsq_reenq_init)
{
	struct reenq_timer_val *tval;
	u32 key = 0;
	s32 ret;

	ret = scx_bpf_create_dsq(USER_DSQ, -1);
	if (ret)
		return ret;

	if (!__COMPAT_has_generic_reenq()) {
		scx_bpf_error("scx_bpf_dsq_reenq() not available");
		return -EOPNOTSUPP;
	}

	tval = bpf_map_lookup_elem(&reenq_timer, &key);
	if (!tval)
		return -ESRCH;

	bpf_timer_init(&tval->timer, &reenq_timer, CLOCK_MONOTONIC);
	bpf_timer_set_callback(&tval->timer, reenq_timerfn);

	return bpf_timer_start(&tval->timer, REENQ_TIMER_NS, 0);
}

void BPF_STRUCT_OPS(dsq_reenq_exit, struct scx_exit_info *ei)
{
	UEI_RECORD(uei, ei);
}

SCX_OPS_DEFINE(dsq_reenq_ops,
	       .enqueue		= (void *)dsq_reenq_enqueue,
	       .dispatch	= (void *)dsq_reenq_dispatch,
	       .init		= (void *)dsq_reenq_init,
	       .exit		= (void *)dsq_reenq_exit,
	       .timeout_ms	= 10000,
	       .name		= "dsq_reenq")
