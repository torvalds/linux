// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021. Huawei Technologies Co., Ltd
 */
#include <linux/kernel.h>
#include <linux/bpf_verifier.h>
#include <linux/bpf.h>
#include <linux/btf.h>

extern struct bpf_struct_ops bpf_bpf_dummy_ops;

/* A common type for test_N with return value in bpf_dummy_ops */
typedef int (*dummy_ops_test_ret_fn)(struct bpf_dummy_ops_state *state, ...);

struct bpf_dummy_ops_test_args {
	u64 args[MAX_BPF_FUNC_ARGS];
	struct bpf_dummy_ops_state state;
};

static struct bpf_dummy_ops_test_args *
dummy_ops_init_args(const union bpf_attr *kattr, unsigned int nr)
{
	__u32 size_in;
	struct bpf_dummy_ops_test_args *args;
	void __user *ctx_in;
	void __user *u_state;

	size_in = kattr->test.ctx_size_in;
	if (size_in != sizeof(u64) * nr)
		return ERR_PTR(-EINVAL);

	args = kzalloc(sizeof(*args), GFP_KERNEL);
	if (!args)
		return ERR_PTR(-ENOMEM);

	ctx_in = u64_to_user_ptr(kattr->test.ctx_in);
	if (copy_from_user(args->args, ctx_in, size_in))
		goto out;

	/* args[0] is 0 means state argument of test_N will be NULL */
	u_state = u64_to_user_ptr(args->args[0]);
	if (u_state && copy_from_user(&args->state, u_state,
				      sizeof(args->state)))
		goto out;

	return args;
out:
	kfree(args);
	return ERR_PTR(-EFAULT);
}

static int dummy_ops_copy_args(struct bpf_dummy_ops_test_args *args)
{
	void __user *u_state;

	u_state = u64_to_user_ptr(args->args[0]);
	if (u_state && copy_to_user(u_state, &args->state, sizeof(args->state)))
		return -EFAULT;

	return 0;
}

static int dummy_ops_call_op(void *image, struct bpf_dummy_ops_test_args *args)
{
	dummy_ops_test_ret_fn test = (void *)image;
	struct bpf_dummy_ops_state *state = NULL;

	/* state needs to be NULL if args[0] is 0 */
	if (args->args[0])
		state = &args->state;
	return test(state, args->args[1], args->args[2],
		    args->args[3], args->args[4]);
}

int bpf_struct_ops_test_run(struct bpf_prog *prog, const union bpf_attr *kattr,
			    union bpf_attr __user *uattr)
{
	const struct bpf_struct_ops *st_ops = &bpf_bpf_dummy_ops;
	const struct btf_type *func_proto;
	struct bpf_dummy_ops_test_args *args;
	struct bpf_tramp_progs *tprogs;
	void *image = NULL;
	unsigned int op_idx;
	int prog_ret;
	int err;

	if (prog->aux->attach_btf_id != st_ops->type_id)
		return -EOPNOTSUPP;

	func_proto = prog->aux->attach_func_proto;
	args = dummy_ops_init_args(kattr, btf_type_vlen(func_proto));
	if (IS_ERR(args))
		return PTR_ERR(args);

	tprogs = kcalloc(BPF_TRAMP_MAX, sizeof(*tprogs), GFP_KERNEL);
	if (!tprogs) {
		err = -ENOMEM;
		goto out;
	}

	image = bpf_jit_alloc_exec(PAGE_SIZE);
	if (!image) {
		err = -ENOMEM;
		goto out;
	}
	set_vm_flush_reset_perms(image);

	op_idx = prog->expected_attach_type;
	err = bpf_struct_ops_prepare_trampoline(tprogs, prog,
						&st_ops->func_models[op_idx],
						image, image + PAGE_SIZE);
	if (err < 0)
		goto out;

	set_memory_ro((long)image, 1);
	set_memory_x((long)image, 1);
	prog_ret = dummy_ops_call_op(image, args);

	err = dummy_ops_copy_args(args);
	if (err)
		goto out;
	if (put_user(prog_ret, &uattr->test.retval))
		err = -EFAULT;
out:
	kfree(args);
	bpf_jit_free_exec(image);
	kfree(tprogs);
	return err;
}

static int bpf_dummy_init(struct btf *btf)
{
	return 0;
}

static bool bpf_dummy_ops_is_valid_access(int off, int size,
					  enum bpf_access_type type,
					  const struct bpf_prog *prog,
					  struct bpf_insn_access_aux *info)
{
	return bpf_tracing_btf_ctx_access(off, size, type, prog, info);
}

static int bpf_dummy_ops_btf_struct_access(struct bpf_verifier_log *log,
					   const struct btf *btf,
					   const struct btf_type *t, int off,
					   int size, enum bpf_access_type atype,
					   u32 *next_btf_id,
					   enum bpf_type_flag *flag)
{
	const struct btf_type *state;
	s32 type_id;
	int err;

	type_id = btf_find_by_name_kind(btf, "bpf_dummy_ops_state",
					BTF_KIND_STRUCT);
	if (type_id < 0)
		return -EINVAL;

	state = btf_type_by_id(btf, type_id);
	if (t != state) {
		bpf_log(log, "only access to bpf_dummy_ops_state is supported\n");
		return -EACCES;
	}

	err = btf_struct_access(log, btf, t, off, size, atype, next_btf_id,
				flag);
	if (err < 0)
		return err;

	return atype == BPF_READ ? err : NOT_INIT;
}

static const struct bpf_verifier_ops bpf_dummy_verifier_ops = {
	.is_valid_access = bpf_dummy_ops_is_valid_access,
	.btf_struct_access = bpf_dummy_ops_btf_struct_access,
};

static int bpf_dummy_init_member(const struct btf_type *t,
				 const struct btf_member *member,
				 void *kdata, const void *udata)
{
	return -EOPNOTSUPP;
}

static int bpf_dummy_reg(void *kdata)
{
	return -EOPNOTSUPP;
}

static void bpf_dummy_unreg(void *kdata)
{
}

struct bpf_struct_ops bpf_bpf_dummy_ops = {
	.verifier_ops = &bpf_dummy_verifier_ops,
	.init = bpf_dummy_init,
	.init_member = bpf_dummy_init_member,
	.reg = bpf_dummy_reg,
	.unreg = bpf_dummy_unreg,
	.name = "bpf_dummy_ops",
};
