// SPDX-License-Identifier: GPL-2.0
/*
 * A cid-form scheduler checking the cmask cid-form ops.enable() receives: the
 * header, every cid bit against p->cpus_ptr, and that set_cmask() follows with
 * the same mask before set_weight() and before the task first becomes runnable,
 * and never runs before enable().
 *
 * Copyright (c) 2026 Tejun Heo <tj@kernel.org>
 */
#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_ARENA);
	__uint(map_flags, BPF_F_MMAPABLE);
	__uint(max_entries, 1 << 16);
} arena SEC(".maps");

struct task_ctx {
	u64	enable_fp;	/* fingerprint of the mask enable() received */
	bool	enabled;
	bool	pending;	/* enable() ran, the initial set_cmask() hasn't */
};

struct {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct task_ctx);
} task_ctx_stor SEC(".maps");

/* details of a cid bit mismatch, filled by check_mask() */
struct mask_mismatch {
	s32	cid;
	bool	want;
	bool	got;
};

u64 nr_enable, nr_initial_set_cmask, nr_set_cmask, nr_set_weight;

UEI_DEFINE(uei);

static struct task_ctx *lookup_task_ctx(struct task_struct *p)
{
	struct task_ctx *tctx;

	tctx = bpf_task_storage_get(&task_ctx_stor, p, 0, 0);
	if (!tctx)
		scx_bpf_error("task_ctx lookup failed for %s[%d]", p->comm, p->pid);
	return tctx;
}

/*
 * Verify @m's header and every cid bit against @p's cpumask and fingerprint the
 * bits into @fp. Return 0 on success, -EINVAL on a bad header, -ENOENT on a cid
 * without a cpu and -EIO on a bit mismatch with the details in @mm.
 */
static int check_mask(struct task_struct *p, const struct scx_cmask __arena *m, u64 *fp,
		      struct mask_mismatch *mm)
{
	u32 nr_cids = scx_bpf_nr_cids();
	u64 h = 0;
	s32 cid;

	if (m->base || m->nr_cids != nr_cids || m->alloc_words != CMASK_NR_WORDS(nr_cids))
		return -EINVAL;

	bpf_for(cid, 0, nr_cids) {
		bool want, got;
		s32 cpu;

		cpu = scx_bpf_cid_to_cpu(cid);
		if (cpu < 0)
			return -ENOENT;
		want = bpf_cpumask_test_cpu(cpu, p->cpus_ptr);
		got = cmask_test(cid, m);
		if (want != got) {
			mm->cid = cid;
			mm->want = want;
			mm->got = got;
			return -EIO;
		}
		h = h * 31 + got;
	}

	*fp = h;
	return 0;
}

s32 BPF_STRUCT_OPS_SLEEPABLE(enable_cmask_init_task, struct task_struct *p,
			     struct scx_init_task_args *args)
{
	if (!bpf_task_storage_get(&task_ctx_stor, p, 0, BPF_LOCAL_STORAGE_GET_F_CREATE))
		return -ENOMEM;
	return 0;
}

void BPF_STRUCT_OPS(enable_cmask_enable, struct task_struct *p, struct scx_enable_args *args)
{
	struct scx_cmask __arena *m = (struct scx_cmask __arena *)args->cmask_arena_addr;
	struct mask_mismatch mm = {};
	struct task_ctx *tctx;
	int ret;

	asm volatile("" :: "r"(&arena));
	tctx = lookup_task_ctx(p);
	if (!tctx)
		return;

	__sync_fetch_and_add(&nr_enable, 1);
	if (tctx->enabled || tctx->pending) {
		scx_bpf_error("enable: %s[%d] enabled twice", p->comm, p->pid);
		return;
	}

	ret = check_mask(p, m, &tctx->enable_fp, &mm);
	if (ret) {
		scx_bpf_error("enable: %s[%d] cmask check failed %d cid=%d want=%d got=%d",
			      p->comm, p->pid, ret, mm.cid, mm.want, mm.got);
		return;
	}
	tctx->enabled = true;
	tctx->pending = true;
}

void BPF_STRUCT_OPS(enable_cmask_set_cmask, struct task_struct *p,
		    struct scx_cmask __arena *m)
{
	struct mask_mismatch mm = {};
	struct task_ctx *tctx;
	u64 fp;
	int ret;

	asm volatile("" :: "r"(&arena));
	tctx = lookup_task_ctx(p);
	if (!tctx)
		return;

	__sync_fetch_and_add(&nr_set_cmask, 1);
	if (!tctx->enabled) {
		scx_bpf_error("set_cmask: %s[%d] not enabled", p->comm, p->pid);
		return;
	}

	ret = check_mask(p, m, &fp, &mm);
	if (ret) {
		scx_bpf_error("set_cmask: %s[%d] cmask check failed %d cid=%d want=%d got=%d",
			      p->comm, p->pid, ret, mm.cid, mm.want, mm.got);
		return;
	}

	if (tctx->pending) {
		if (fp != tctx->enable_fp) {
			scx_bpf_error("set_cmask: %s[%d] initial mask differs from enable()",
				      p->comm, p->pid);
			return;
		}
		tctx->pending = false;
		__sync_fetch_and_add(&nr_initial_set_cmask, 1);
	}
}

void BPF_STRUCT_OPS(enable_cmask_set_weight, struct task_struct *p, u32 weight)
{
	struct task_ctx *tctx;

	tctx = lookup_task_ctx(p);
	if (!tctx)
		return;

	__sync_fetch_and_add(&nr_set_weight, 1);
	if (tctx->pending)
		scx_bpf_error("set_weight: %s[%d] before the initial set_cmask()", p->comm,
			      p->pid);
}

void BPF_STRUCT_OPS(enable_cmask_runnable, struct task_struct *p, u64 enq_flags)
{
	struct task_ctx *tctx;

	tctx = lookup_task_ctx(p);
	if (!tctx)
		return;

	if (tctx->pending)
		scx_bpf_error("runnable: %s[%d] before the initial set_cmask()", p->comm,
			      p->pid);
}

void BPF_STRUCT_OPS(enable_cmask_disable, struct task_struct *p)
{
	struct task_ctx *tctx;

	tctx = lookup_task_ctx(p);
	if (!tctx)
		return;

	tctx->enabled = false;
	tctx->pending = false;
}

void BPF_STRUCT_OPS(enable_cmask_exit, struct scx_exit_info *ei)
{
	UEI_RECORD(uei, ei);
}

SCX_OPS_CID_DEFINE(enable_cmask_ops,
		   .init_task	= (void *)enable_cmask_init_task,
		   .enable	= (void *)enable_cmask_enable,
		   .set_cmask	= (void *)enable_cmask_set_cmask,
		   .set_weight	= (void *)enable_cmask_set_weight,
		   .runnable	= (void *)enable_cmask_runnable,
		   .disable	= (void *)enable_cmask_disable,
		   .exit	= (void *)enable_cmask_exit,
		   .flags	= SCX_OPS_SWITCH_PARTIAL,
		   .name	= "enable_cmask");
