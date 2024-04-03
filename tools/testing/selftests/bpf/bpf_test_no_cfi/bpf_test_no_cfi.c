// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */
#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/init.h>
#include <linux/module.h>

struct bpf_test_no_cfi_ops {
	void (*fn_1)(void);
	void (*fn_2)(void);
};

static int dummy_init(struct btf *btf)
{
	return 0;
}

static int dummy_init_member(const struct btf_type *t,
			     const struct btf_member *member,
			     void *kdata, const void *udata)
{
	return 0;
}

static int dummy_reg(void *kdata)
{
	return 0;
}

static void dummy_unreg(void *kdata)
{
}

static const struct bpf_verifier_ops dummy_verifier_ops;

static void bpf_test_no_cfi_ops__fn_1(void)
{
}

static void bpf_test_no_cfi_ops__fn_2(void)
{
}

static struct bpf_test_no_cfi_ops __test_no_cif_ops = {
	.fn_1 = bpf_test_no_cfi_ops__fn_1,
	.fn_2 = bpf_test_no_cfi_ops__fn_2,
};

static struct bpf_struct_ops test_no_cif_ops = {
	.verifier_ops = &dummy_verifier_ops,
	.init = dummy_init,
	.init_member = dummy_init_member,
	.reg = dummy_reg,
	.unreg = dummy_unreg,
	.name = "bpf_test_no_cfi_ops",
	.owner = THIS_MODULE,
};

static int bpf_test_no_cfi_init(void)
{
	int ret;

	ret = register_bpf_struct_ops(&test_no_cif_ops,
				      bpf_test_no_cfi_ops);
	if (!ret)
		return -EINVAL;

	test_no_cif_ops.cfi_stubs = &__test_no_cif_ops;
	ret = register_bpf_struct_ops(&test_no_cif_ops,
				      bpf_test_no_cfi_ops);
	return ret;
}

static void bpf_test_no_cfi_exit(void)
{
}

module_init(bpf_test_no_cfi_init);
module_exit(bpf_test_no_cfi_exit);

MODULE_AUTHOR("Kuifeng Lee");
MODULE_DESCRIPTION("BPF no cfi_stubs test module");
MODULE_LICENSE("Dual BSD/GPL");

