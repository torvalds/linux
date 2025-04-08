// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/module.h>
#include <linux/init.h>

__bpf_kfunc_start_defs();

__bpf_kfunc int bpf_test_modorder_rety(void)
{
	return 'y';
}

__bpf_kfunc_end_defs();

BTF_KFUNCS_START(bpf_test_modorder_kfunc_y_ids)
BTF_ID_FLAGS(func, bpf_test_modorder_rety);
BTF_KFUNCS_END(bpf_test_modorder_kfunc_y_ids)

static const struct btf_kfunc_id_set bpf_test_modorder_y_set = {
	.owner = THIS_MODULE,
	.set = &bpf_test_modorder_kfunc_y_ids,
};

static int __init bpf_test_modorder_y_init(void)
{
	return register_btf_kfunc_id_set(BPF_PROG_TYPE_SCHED_CLS,
					 &bpf_test_modorder_y_set);
}

static void __exit bpf_test_modorder_y_exit(void)
{
}

module_init(bpf_test_modorder_y_init);
module_exit(bpf_test_modorder_y_exit);

MODULE_DESCRIPTION("BPF selftest ordertest module Y");
MODULE_LICENSE("GPL");
