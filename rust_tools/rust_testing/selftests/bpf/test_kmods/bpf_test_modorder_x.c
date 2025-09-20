// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/module.h>
#include <linux/init.h>

__bpf_kfunc_start_defs();

__bpf_kfunc int bpf_test_modorder_retx(void)
{
	return 'x';
}

__bpf_kfunc_end_defs();

BTF_KFUNCS_START(bpf_test_modorder_kfunc_x_ids)
BTF_ID_FLAGS(func, bpf_test_modorder_retx);
BTF_KFUNCS_END(bpf_test_modorder_kfunc_x_ids)

static const struct btf_kfunc_id_set bpf_test_modorder_x_set = {
	.owner = THIS_MODULE,
	.set = &bpf_test_modorder_kfunc_x_ids,
};

static int __init bpf_test_modorder_x_init(void)
{
	return register_btf_kfunc_id_set(BPF_PROG_TYPE_SCHED_CLS,
					 &bpf_test_modorder_x_set);
}

static void __exit bpf_test_modorder_x_exit(void)
{
}

module_init(bpf_test_modorder_x_init);
module_exit(bpf_test_modorder_x_exit);

MODULE_DESCRIPTION("BPF selftest ordertest module X");
MODULE_LICENSE("GPL");
