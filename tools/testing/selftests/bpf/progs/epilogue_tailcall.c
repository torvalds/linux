// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"
#include "../test_kmods/bpf_testmod.h"
#include "../test_kmods/bpf_testmod_kfunc.h"

char _license[] SEC("license") = "GPL";

static __noinline __used int subprog(struct st_ops_args *args)
{
	args->a += 1;
	return args->a;
}

SEC("struct_ops/test_epilogue_subprog")
int BPF_PROG(test_epilogue_subprog, struct st_ops_args *args)
{
	subprog(args);
	return args->a;
}

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 1);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u32));
	__array(values, void (void));
} epilogue_map SEC(".maps") = {
	.values = {
		[0] = (void *)&test_epilogue_subprog,
	}
};

SEC("struct_ops/test_epilogue_tailcall")
int test_epilogue_tailcall(unsigned long long *ctx)
{
	bpf_tail_call(ctx, &epilogue_map, 0);
	return 0;
}

SEC(".struct_ops.link")
struct bpf_testmod_st_ops epilogue_tailcall = {
	.test_epilogue = (void *)test_epilogue_tailcall,
};

SEC(".struct_ops.link")
struct bpf_testmod_st_ops epilogue_subprog = {
	.test_epilogue = (void *)test_epilogue_subprog,
};

SEC("syscall")
int syscall_epilogue_tailcall(struct st_ops_args *args)
{
	return bpf_kfunc_st_ops_test_epilogue(args);
}
