// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "../test_kmods/bpf_testmod.h"
#include "../test_kmods/bpf_testmod_kfunc.h"

char _license[] SEC("license") = "GPL";

/* an untrusted pointer to the argument struct, never set, for the verifier */
struct untrusted_val {
	struct bpf_testmod_nullable_args __kptr_untrusted *args;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct untrusted_val);
} untrusted_map SEC(".maps");

SEC("struct_ops/test_nullable")
int BPF_PROG(test_nullable_cb, struct bpf_testmod_nullable_args *args)
{
	struct bpf_testmod_nullable_args *u;
	struct untrusted_val *v;
	struct task_struct *t;
	int ret = 0, *i, zero = 0;

	/* trusted, so good for a kfunc as is */
	t = bpf_task_acquire(args->task);
	if (!t)
		return -1;
	bpf_task_release(t);

	/* trusted but __nullable, so a check has to come first */
	if (args->task_or_null) {
		t = bpf_task_acquire(args->task_or_null);
		if (!t)
			return -1;
		bpf_task_release(t);
		ret = 1;
	}
	/* per-CPU and __nullable: a check has to come first, per_cpu_ptr() has none */
	if (args->pcpu_or_null) {
		struct bpf_testmod_pcpu *v = bpf_this_cpu_ptr(args->pcpu_or_null);

		if (v->val != 0x5ca1ab1e)
			return -1;
	}
	/* through a plain pointer the loads are probed, __nullable still asks for a check */
	t = args->self->task_or_null;
	if (t && t->pid != args->task->pid)
		return -1;
	/* a pointer to a scalar walks as untrusted memory, __nullable applies all the same */
	i = args->int_or_null;
	if (i && *i != 7)
		return -1;
	/* through an untrusted pointer the member is probed too, and stays nullable */
	v = bpf_map_lookup_elem(&untrusted_map, &zero);
	u = v ? v->args : NULL;
	if (u) {
		t = u->task_or_null;
		if (t && t->pid != args->task->pid)
			return -1;
	}
	return ret;
}

SEC(".struct_ops.link")
struct bpf_testmod_ops3 testmod_nullable = {
	.test_nullable = (void *)test_nullable_cb,
};

SEC("syscall")
int trigger(void *ctx)
{
	/* one call without task_or_null, one with */
	return bpf_testmod_ops3_call_test_nullable() != 2;
}
