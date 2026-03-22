// SPDX-License-Identifier: GPL-2.0
/*
 * Validate SCX_ENQ_IMMED fast/slow path semantics via the direct insert path.
 *
 * With SCX_OPS_ALWAYS_ENQ_IMMED set, the kernel automatically adds
 * SCX_ENQ_IMMED to every local DSQ dispatch.  When the target CPU's local
 * DSQ already has tasks queued (dsq->nr > 1), the kernel re-enqueues the
 * task through ops.enqueue() with SCX_ENQ_REENQ and SCX_TASK_REENQ_IMMED
 * recorded in p->scx.flags (the "slow path").
 *
 * Worker threads are pinned to CPU 0 via SCX_DSQ_LOCAL_ON to guarantee
 * local DSQ contention.
 */

#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";

UEI_DEFINE(uei);

/* Set by userspace to identify the test process group. */
const volatile u32 test_tgid;

/*
 * SCX_TASK_REENQ_REASON_MASK and SCX_TASK_REENQ_IMMED are exported via
 * vmlinux BTF as part of enum scx_ent_flags.
 */

u64 nr_immed_reenq;

void BPF_STRUCT_OPS(enq_immed_enqueue, struct task_struct *p, u64 enq_flags)
{
	if (enq_flags & SCX_ENQ_REENQ) {
		u32 reason = p->scx.flags & SCX_TASK_REENQ_REASON_MASK;

		if (reason == SCX_TASK_REENQ_IMMED)
			__sync_fetch_and_add(&nr_immed_reenq, 1);
	}

	if (p->tgid == (pid_t)test_tgid)
		scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL_ON | 0, SCX_SLICE_DFL,
				   enq_flags);
	else
		scx_bpf_dsq_insert(p, SCX_DSQ_GLOBAL, SCX_SLICE_DFL,
				   enq_flags);
}

void BPF_STRUCT_OPS(enq_immed_dispatch, s32 cpu, struct task_struct *prev)
{
	scx_bpf_dsq_move_to_local(SCX_DSQ_GLOBAL, 0);
}

void BPF_STRUCT_OPS(enq_immed_exit, struct scx_exit_info *ei)
{
	UEI_RECORD(uei, ei);
}

SCX_OPS_DEFINE(enq_immed_ops,
	       .enqueue		= (void *)enq_immed_enqueue,
	       .dispatch	= (void *)enq_immed_dispatch,
	       .exit		= (void *)enq_immed_exit,
	       .flags		= SCX_OPS_ALWAYS_ENQ_IMMED,
	       .name		= "enq_immed")
