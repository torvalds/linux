// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#include <linux/btf.h>
#include <linux/btf_ids.h>
#include <linux/error-injection.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/percpu-defs.h>
#include <linux/sysfs.h>
#include <linux/tracepoint.h>
#include "bpf_testmod.h"

#define CREATE_TRACE_POINTS
#include "bpf_testmod-events.h"

typedef int (*func_proto_typedef)(long);
typedef int (*func_proto_typedef_nested1)(func_proto_typedef);
typedef int (*func_proto_typedef_nested2)(func_proto_typedef_nested1);

DEFINE_PER_CPU(int, bpf_testmod_ksym_percpu) = 123;

noinline void
bpf_testmod_test_mod_kfunc(int i)
{
	*(int *)this_cpu_ptr(&bpf_testmod_ksym_percpu) = i;
}

struct bpf_testmod_btf_type_tag_1 {
	int a;
};

struct bpf_testmod_btf_type_tag_2 {
	struct bpf_testmod_btf_type_tag_1 __user *p;
};

struct bpf_testmod_btf_type_tag_3 {
	struct bpf_testmod_btf_type_tag_1 __percpu *p;
};

noinline int
bpf_testmod_test_btf_type_tag_user_1(struct bpf_testmod_btf_type_tag_1 __user *arg) {
	BTF_TYPE_EMIT(func_proto_typedef);
	BTF_TYPE_EMIT(func_proto_typedef_nested1);
	BTF_TYPE_EMIT(func_proto_typedef_nested2);
	return arg->a;
}

noinline int
bpf_testmod_test_btf_type_tag_user_2(struct bpf_testmod_btf_type_tag_2 *arg) {
	return arg->p->a;
}

noinline int
bpf_testmod_test_btf_type_tag_percpu_1(struct bpf_testmod_btf_type_tag_1 __percpu *arg) {
	return arg->a;
}

noinline int
bpf_testmod_test_btf_type_tag_percpu_2(struct bpf_testmod_btf_type_tag_3 *arg) {
	return arg->p->a;
}

noinline int bpf_testmod_loop_test(int n)
{
	int i, sum = 0;

	/* the primary goal of this test is to test LBR. Create a lot of
	 * branches in the function, so we can catch it easily.
	 */
	for (i = 0; i < n; i++)
		sum += i;
	return sum;
}

__weak noinline struct file *bpf_testmod_return_ptr(int arg)
{
	static struct file f = {};

	switch (arg) {
	case 1: return (void *)EINVAL;		/* user addr */
	case 2: return (void *)0xcafe4a11;	/* user addr */
	case 3: return (void *)-EINVAL;		/* canonical, but invalid */
	case 4: return (void *)(1ull << 60);	/* non-canonical and invalid */
	case 5: return (void *)~(1ull << 30);	/* trigger extable */
	case 6: return &f;			/* valid addr */
	case 7: return (void *)((long)&f | 1);	/* kernel tricks */
	default: return NULL;
	}
}

noinline ssize_t
bpf_testmod_test_read(struct file *file, struct kobject *kobj,
		      struct bin_attribute *bin_attr,
		      char *buf, loff_t off, size_t len)
{
	struct bpf_testmod_test_read_ctx ctx = {
		.buf = buf,
		.off = off,
		.len = len,
	};
	int i = 1;

	while (bpf_testmod_return_ptr(i))
		i++;

	/* This is always true. Use the check to make sure the compiler
	 * doesn't remove bpf_testmod_loop_test.
	 */
	if (bpf_testmod_loop_test(101) > 100)
		trace_bpf_testmod_test_read(current, &ctx);

	/* Magic number to enable writable tp */
	if (len == 64) {
		struct bpf_testmod_test_writable_ctx writable = {
			.val = 1024,
		};
		trace_bpf_testmod_test_writable_bare(&writable);
		if (writable.early_ret)
			return snprintf(buf, len, "%d\n", writable.val);
	}

	return -EIO; /* always fail */
}
EXPORT_SYMBOL(bpf_testmod_test_read);
ALLOW_ERROR_INJECTION(bpf_testmod_test_read, ERRNO);

noinline ssize_t
bpf_testmod_test_write(struct file *file, struct kobject *kobj,
		      struct bin_attribute *bin_attr,
		      char *buf, loff_t off, size_t len)
{
	struct bpf_testmod_test_write_ctx ctx = {
		.buf = buf,
		.off = off,
		.len = len,
	};

	trace_bpf_testmod_test_write_bare(current, &ctx);

	return -EIO; /* always fail */
}
EXPORT_SYMBOL(bpf_testmod_test_write);
ALLOW_ERROR_INJECTION(bpf_testmod_test_write, ERRNO);

static struct bin_attribute bin_attr_bpf_testmod_file __ro_after_init = {
	.attr = { .name = "bpf_testmod", .mode = 0666, },
	.read = bpf_testmod_test_read,
	.write = bpf_testmod_test_write,
};

BTF_SET8_START(bpf_testmod_check_kfunc_ids)
BTF_ID_FLAGS(func, bpf_testmod_test_mod_kfunc)
BTF_SET8_END(bpf_testmod_check_kfunc_ids)

static const struct btf_kfunc_id_set bpf_testmod_kfunc_set = {
	.owner = THIS_MODULE,
	.set   = &bpf_testmod_check_kfunc_ids,
};

extern int bpf_fentry_test1(int a);

static int bpf_testmod_init(void)
{
	int ret;

	ret = register_btf_kfunc_id_set(BPF_PROG_TYPE_SCHED_CLS, &bpf_testmod_kfunc_set);
	if (ret < 0)
		return ret;
	if (bpf_fentry_test1(0) < 0)
		return -EINVAL;
	return sysfs_create_bin_file(kernel_kobj, &bin_attr_bpf_testmod_file);
}

static void bpf_testmod_exit(void)
{
	return sysfs_remove_bin_file(kernel_kobj, &bin_attr_bpf_testmod_file);
}

module_init(bpf_testmod_init);
module_exit(bpf_testmod_exit);

MODULE_AUTHOR("Andrii Nakryiko");
MODULE_DESCRIPTION("BPF selftests module");
MODULE_LICENSE("Dual BSD/GPL");
