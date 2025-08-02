// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include <errno.h>
#include <bpf/bpf_helpers.h>

#include "task_local_data.bpf.h"

struct tld_keys {
	tld_key_t value0;
	tld_key_t value1;
	tld_key_t value2;
	tld_key_t value_not_exist;
};

struct test_tld_struct {
	__u64 a;
	__u64 b;
	__u64 c;
	__u64 d;
};

int test_value0;
int test_value1;
struct test_tld_struct test_value2;

SEC("syscall")
int task_main(void *ctx)
{
	struct tld_object tld_obj;
	struct test_tld_struct *struct_p;
	struct task_struct *task;
	int err, *int_p;

	task = bpf_get_current_task_btf();
	err = tld_object_init(task, &tld_obj);
	if (err)
		return 1;

	int_p = tld_get_data(&tld_obj, value0, "value0", sizeof(int));
	if (int_p)
		test_value0 = *int_p;
	else
		return 2;

	int_p = tld_get_data(&tld_obj, value1, "value1", sizeof(int));
	if (int_p)
		test_value1 = *int_p;
	else
		return 3;

	struct_p = tld_get_data(&tld_obj, value2, "value2", sizeof(struct test_tld_struct));
	if (struct_p)
		test_value2 = *struct_p;
	else
		return 4;

	int_p = tld_get_data(&tld_obj, value_not_exist, "value_not_exist", sizeof(int));
	if (int_p)
		return 5;

	return 0;
}

char _license[] SEC("license") = "GPL";
