// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

typedef int (*func_proto_typedef)(long);
typedef int (*func_proto_typedef_nested1)(func_proto_typedef);
typedef int (*func_proto_typedef_nested2)(func_proto_typedef_nested1);

int proto_out;

SEC("raw_tracepoint/sys_enter")
int core_relo_proto(void *ctx)
{
	proto_out = bpf_core_type_exists(func_proto_typedef_nested2);

	return 0;
}

char LICENSE[] SEC("license") = "GPL";
