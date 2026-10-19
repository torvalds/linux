// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"
#include "../test_kmods/bpf_testmod.h"

char _license[] SEC("license") = "GPL";

struct untrusted_val {
	struct bpf_testmod_nullable_args __kptr_untrusted *args;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct untrusted_val);
} untrusted_map SEC(".maps");

/*
 * Through an untrusted pointer the member is probed, which tolerates NULL, so
 * the tag asks for no check there.
 */
SEC("struct_ops/test_nullable")
__success
int BPF_PROG(unchecked_untrusted, struct bpf_testmod_nullable_args *args)
{
	struct bpf_testmod_nullable_args *u;
	struct untrusted_val *v;
	int zero = 0;

	v = bpf_map_lookup_elem(&untrusted_map, &zero);
	u = v ? v->args : NULL;
	if (!u)
		return 0;
	return u->task_or_null->pid;
}

SEC(".struct_ops.link")
struct bpf_testmod_ops3 testmod_nullable_probed = {
	.test_nullable = (void *)unchecked_untrusted,
};
