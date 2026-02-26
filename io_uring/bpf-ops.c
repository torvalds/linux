/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/mutex.h>
#include <linux/bpf.h>
#include <linux/bpf_verifier.h>

#include "io_uring.h"
#include "register.h"
#include "loop.h"
#include "memmap.h"
#include "bpf-ops.h"

static DEFINE_MUTEX(io_bpf_ctrl_mutex);
static const struct btf_type *loop_params_type;

__bpf_kfunc_start_defs();

__bpf_kfunc int bpf_io_uring_submit_sqes(struct io_ring_ctx *ctx, u32 nr)
{
	return io_submit_sqes(ctx, nr);
}

__bpf_kfunc
__u8 *bpf_io_uring_get_region(struct io_ring_ctx *ctx, __u32 region_id,
			      const size_t rdwr_buf_size)
{
	struct io_mapped_region *r;

	lockdep_assert_held(&ctx->uring_lock);

	switch (region_id) {
	case IOU_REGION_MEM:
		r = &ctx->param_region;
		break;
	case IOU_REGION_CQ:
		r = &ctx->ring_region;
		break;
	case IOU_REGION_SQ:
		r = &ctx->sq_region;
		break;
	default:
		return NULL;
	}

	if (unlikely(rdwr_buf_size > io_region_size(r)))
		return NULL;
	return io_region_get_ptr(r);
}

__bpf_kfunc_end_defs();

BTF_KFUNCS_START(io_uring_kfunc_set)
BTF_ID_FLAGS(func, bpf_io_uring_submit_sqes, KF_SLEEPABLE);
BTF_ID_FLAGS(func, bpf_io_uring_get_region, KF_RET_NULL);
BTF_KFUNCS_END(io_uring_kfunc_set)

static const struct btf_kfunc_id_set bpf_io_uring_kfunc_set = {
	.owner = THIS_MODULE,
	.set = &io_uring_kfunc_set,
};

static int io_bpf_ops__loop_step(struct io_ring_ctx *ctx,
				 struct iou_loop_params *lp)
{
	return IOU_LOOP_STOP;
}

static struct io_uring_bpf_ops io_bpf_ops_stubs = {
	.loop_step = io_bpf_ops__loop_step,
};

static bool bpf_io_is_valid_access(int off, int size,
				    enum bpf_access_type type,
				    const struct bpf_prog *prog,
				    struct bpf_insn_access_aux *info)
{
	if (type != BPF_READ)
		return false;
	if (off < 0 || off >= sizeof(__u64) * MAX_BPF_FUNC_ARGS)
		return false;
	if (off % size != 0)
		return false;

	return btf_ctx_access(off, size, type, prog, info);
}

static int bpf_io_btf_struct_access(struct bpf_verifier_log *log,
				    const struct bpf_reg_state *reg, int off,
				    int size)
{
	const struct btf_type *t = btf_type_by_id(reg->btf, reg->btf_id);

	if (t == loop_params_type) {
		if (off + size <= offsetofend(struct iou_loop_params, cq_wait_idx))
			return SCALAR_VALUE;
	}

	return -EACCES;
}

static const struct bpf_verifier_ops bpf_io_verifier_ops = {
	.get_func_proto = bpf_base_func_proto,
	.is_valid_access = bpf_io_is_valid_access,
	.btf_struct_access = bpf_io_btf_struct_access,
};

static const struct btf_type *
io_lookup_struct_type(struct btf *btf, const char *name)
{
	s32 type_id;

	type_id = btf_find_by_name_kind(btf, name, BTF_KIND_STRUCT);
	if (type_id < 0)
		return NULL;
	return btf_type_by_id(btf, type_id);
}

static int bpf_io_init(struct btf *btf)
{
	int ret;

	loop_params_type = io_lookup_struct_type(btf, "iou_loop_params");
	if (!loop_params_type) {
		pr_err("io_uring: Failed to locate iou_loop_params\n");
		return -EINVAL;
	}

	ret = register_btf_kfunc_id_set(BPF_PROG_TYPE_STRUCT_OPS,
					&bpf_io_uring_kfunc_set);
	if (ret) {
		pr_err("io_uring: Failed to register kfuncs (%d)\n", ret);
		return ret;
	}
	return 0;
}

static int bpf_io_check_member(const struct btf_type *t,
				const struct btf_member *member,
				const struct bpf_prog *prog)
{
	return 0;
}

static int bpf_io_init_member(const struct btf_type *t,
			       const struct btf_member *member,
			       void *kdata, const void *udata)
{
	u32 moff = __btf_member_bit_offset(t, member) / 8;
	const struct io_uring_bpf_ops *uops = udata;
	struct io_uring_bpf_ops *ops = kdata;

	switch (moff) {
	case offsetof(struct io_uring_bpf_ops, ring_fd):
		ops->ring_fd = uops->ring_fd;
		return 1;
	}
	return 0;
}

static int io_install_bpf(struct io_ring_ctx *ctx, struct io_uring_bpf_ops *ops)
{
	if (ctx->flags & (IORING_SETUP_SQPOLL | IORING_SETUP_IOPOLL))
		return -EOPNOTSUPP;
	if (!(ctx->flags & IORING_SETUP_DEFER_TASKRUN))
		return -EOPNOTSUPP;

	if (ctx->bpf_ops)
		return -EBUSY;
	if (WARN_ON_ONCE(!ops->loop_step))
		return -EINVAL;

	ops->priv = ctx;
	ctx->bpf_ops = ops;
	ctx->loop_step = ops->loop_step;
	return 0;
}

static int bpf_io_reg(void *kdata, struct bpf_link *link)
{
	struct io_uring_bpf_ops *ops = kdata;
	struct io_ring_ctx *ctx;
	struct file *file;
	int ret = -EBUSY;

	file = io_uring_register_get_file(ops->ring_fd, false);
	if (IS_ERR(file))
		return PTR_ERR(file);
	ctx = file->private_data;

	scoped_guard(mutex, &io_bpf_ctrl_mutex) {
		guard(mutex)(&ctx->uring_lock);
		ret = io_install_bpf(ctx, ops);
	}

	fput(file);
	return ret;
}

static void io_eject_bpf(struct io_ring_ctx *ctx)
{
	struct io_uring_bpf_ops *ops = ctx->bpf_ops;

	if (WARN_ON_ONCE(!ops))
		return;
	if (WARN_ON_ONCE(ops->priv != ctx))
		return;

	ops->priv = NULL;
	ctx->bpf_ops = NULL;
	ctx->loop_step = NULL;
}

static void bpf_io_unreg(void *kdata, struct bpf_link *link)
{
	struct io_uring_bpf_ops *ops = kdata;
	struct io_ring_ctx *ctx;

	guard(mutex)(&io_bpf_ctrl_mutex);
	ctx = ops->priv;
	if (ctx) {
		guard(mutex)(&ctx->uring_lock);
		if (WARN_ON_ONCE(ctx->bpf_ops != ops))
			return;

		io_eject_bpf(ctx);
	}
}

void io_unregister_bpf_ops(struct io_ring_ctx *ctx)
{
	/*
	 * ->bpf_ops is write protected by io_bpf_ctrl_mutex and uring_lock,
	 * and read protected by either. Try to avoid taking the global lock
	 * for rings that never had any bpf installed.
	 */
	scoped_guard(mutex, &ctx->uring_lock) {
		if (!ctx->bpf_ops)
			return;
	}

	guard(mutex)(&io_bpf_ctrl_mutex);
	guard(mutex)(&ctx->uring_lock);
	if (ctx->bpf_ops)
		io_eject_bpf(ctx);
}

static struct bpf_struct_ops bpf_ring_ops = {
	.verifier_ops = &bpf_io_verifier_ops,
	.reg = bpf_io_reg,
	.unreg = bpf_io_unreg,
	.check_member = bpf_io_check_member,
	.init_member = bpf_io_init_member,
	.init = bpf_io_init,
	.cfi_stubs = &io_bpf_ops_stubs,
	.name = "io_uring_bpf_ops",
	.owner = THIS_MODULE,
};

static int __init io_uring_bpf_init(void)
{
	int ret;

	ret = register_bpf_struct_ops(&bpf_ring_ops, io_uring_bpf_ops);
	if (ret) {
		pr_err("io_uring: Failed to register struct_ops (%d)\n", ret);
		return ret;
	}

	return 0;
}
__initcall(io_uring_bpf_init);
