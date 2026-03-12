// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"
#include "../test_kmods/bpf_testmod.h"
#include "../test_kmods/bpf_testmod_kfunc.h"

char _license[] SEC("license") = "GPL";

#define MAP_A_MAGIC 1234
int test_err_a;
int recur;

/*
 * test_1_a is reused. The kfunc should not be able to get the associated
 * struct_ops and call test_1 recursively as it is ambiguous.
 */
SEC("struct_ops")
int BPF_PROG(test_1_a, struct st_ops_args *args)
{
	int ret;

	if (!recur) {
		recur++;
		ret = bpf_kfunc_multi_st_ops_test_1_assoc(args);
		if (ret != -1)
			test_err_a++;
		recur--;
	}

	return MAP_A_MAGIC;
}

/* Programs associated with st_ops_map_a */

SEC("syscall")
int syscall_prog_a(void *ctx)
{
	struct st_ops_args args = {};
	int ret;

	ret = bpf_kfunc_multi_st_ops_test_1_assoc(&args);
	if (ret != MAP_A_MAGIC)
		test_err_a++;

	return 0;
}

SEC(".struct_ops.link")
struct bpf_testmod_multi_st_ops st_ops_map_a = {
	.test_1 = (void *)test_1_a,
};

/* Programs associated with st_ops_map_b */

int test_err_b;

SEC("syscall")
int syscall_prog_b(void *ctx)
{
	struct st_ops_args args = {};
	int ret;

	ret = bpf_kfunc_multi_st_ops_test_1_assoc(&args);
	if (ret != MAP_A_MAGIC)
		test_err_b++;

	return 0;
}

SEC(".struct_ops.link")
struct bpf_testmod_multi_st_ops st_ops_map_b = {
	.test_1 = (void *)test_1_a,
};
