/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/mutex.h>
#include <linux/bpf.h>
#include <linux/bpf_verifier.h>

#include "io_uring.h"
#include "register.h"
#include "bpf-ops.h"
#include "loop.h"

static const struct btf_type *loop_params_type;

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
	loop_params_type = io_lookup_struct_type(btf, "iou_loop_params");
	if (!loop_params_type) {
		pr_err("io_uring: Failed to locate iou_loop_params\n");
		return -EINVAL;
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
	return 0;
}

static int bpf_io_reg(void *kdata, struct bpf_link *link)
{
	return -EOPNOTSUPP;
}

static void bpf_io_unreg(void *kdata, struct bpf_link *link)
{
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
