// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"
#include "../test_kmods/bpf_testmod.h"
#include "../test_kmods/bpf_testmod_kfunc.h"

char _license[] SEC("license") = "GPL";

struct elem {
	struct bpf_timer timer;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct elem);
} array_map SEC(".maps");

#define MAP_MAGIC 1234
int recur;
int test_err;
int timer_ns;
int timer_test_1_ret;
int timer_cb_run;

__noinline static int timer_cb(void *map, int *key, struct bpf_timer *timer)
{
	struct st_ops_args args = {};

	recur++;
	timer_test_1_ret = bpf_kfunc_multi_st_ops_test_1_assoc(&args);
	recur--;

	timer_cb_run++;

	return 0;
}

SEC("struct_ops")
int BPF_PROG(test_1, struct st_ops_args *args)
{
	struct bpf_timer *timer;
	int key = 0;

	if (!recur) {
		timer = bpf_map_lookup_elem(&array_map, &key);
		if (!timer)
			return 0;

		bpf_timer_init(timer, &array_map, 1);
		bpf_timer_set_callback(timer, timer_cb);
		bpf_timer_start(timer, timer_ns, 0);
	}

	return MAP_MAGIC;
}

SEC("syscall")
int syscall_prog(void *ctx)
{
	struct st_ops_args args = {};
	int ret;

	ret = bpf_kfunc_multi_st_ops_test_1_assoc(&args);
	if (ret != MAP_MAGIC)
		test_err++;

	return 0;
}

SEC(".struct_ops.link")
struct bpf_testmod_multi_st_ops st_ops_map = {
	.test_1 = (void *)test_1,
};
