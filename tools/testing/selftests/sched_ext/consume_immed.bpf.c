// SPDX-License-Identifier: GPL-2.0
/*
 * Validate SCX_ENQ_IMMED semantics through the consume path.
 *
 * This is the orthogonal counterpart to enq_immed:
 *
 *   enq_immed:      SCX_ENQ_IMMED via scx_bpf_dsq_insert() to local DSQ
 *                   with SCX_OPS_ALWAYS_ENQ_IMMED
 *
 *   consume_immed:  SCX_ENQ_IMMED via scx_bpf_dsq_move_to_local() with
 *                   explicit SCX_ENQ_IMMED in enq_flags (requires v2 kfunc)
 *
 * Worker threads belonging to test_tgid are inserted into USER_DSQ.
 * ops.dispatch() on CPU 0 consumes from USER_DSQ with SCX_ENQ_IMMED.
 * With multiple workers competing for CPU 0, dsq->nr > 1 triggers the
 * IMMED slow path (reenqueue with SCX_TASK_REENQ_IMMED).
 *
 * Requires scx_bpf_dsq_move_to_local___v2() (v7.1+) for enq_flags support.
 */

#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";

UEI_DEFINE(uei);

#define USER_DSQ	0

/* Set by userspace to identify the test process group. */
const volatile u32 test_tgid;

/*
 * SCX_TASK_REENQ_REASON_MASK and SCX_TASK_REENQ_IMMED are exported via
 * vmlinux BTF as part of enum scx_ent_flags.
 */

u64 nr_consume_immed_reenq;

void BPF_STRUCT_OPS(consume_immed_enqueue, struct task_struct *p,
		    u64 enq_flags)
{
	if (enq_flags & SCX_ENQ_REENQ) {
		u32 reason = p->scx.flags & SCX_TASK_REENQ_REASON_MASK;

		if (reason == SCX_TASK_REENQ_IMMED)
			__sync_fetch_and_add(&nr_consume_immed_reenq, 1);
	}

	if (p->tgid == (pid_t)test_tgid)
		scx_bpf_dsq_insert(p, USER_DSQ, SCX_SLICE_DFL, enq_flags);
	else
		scx_bpf_dsq_insert(p, SCX_DSQ_GLOBAL, SCX_SLICE_DFL,
				   enq_flags);
}

void BPF_STRUCT_OPS(consume_immed_dispatch, s32 cpu, struct task_struct *prev)
{
	if (cpu == 0)
		scx_bpf_dsq_move_to_local(USER_DSQ, SCX_ENQ_IMMED);
	else
		scx_bpf_dsq_move_to_local(SCX_DSQ_GLOBAL, 0);
}

s32 BPF_STRUCT_OPS_SLEEPABLE(consume_immed_init)
{
	/*
	 * scx_bpf_dsq_move_to_local___v2() adds the enq_flags parameter.
	 * On older kernels the consume path cannot pass SCX_ENQ_IMMED.
	 */
	if (!bpf_ksym_exists(scx_bpf_dsq_move_to_local___v2)) {
		scx_bpf_error("scx_bpf_dsq_move_to_local v2 not available");
		return -EOPNOTSUPP;
	}

	return scx_bpf_create_dsq(USER_DSQ, -1);
}

void BPF_STRUCT_OPS(consume_immed_exit, struct scx_exit_info *ei)
{
	UEI_RECORD(uei, ei);
}

SCX_OPS_DEFINE(consume_immed_ops,
	       .enqueue		= (void *)consume_immed_enqueue,
	       .dispatch	= (void *)consume_immed_dispatch,
	       .init		= (void *)consume_immed_init,
	       .exit		= (void *)consume_immed_exit,
	       .name		= "consume_immed")
