// SPDX-License-Identifier: GPL-2.0
#include "btf_ptr.h"
#include <bpf/bpf_helpers.h>

__u32 type_id;
/* A buffer we own to render the selected type from, kept in bounds. */
char obj[256];
char out[64];
long ret;

SEC("raw_tp/sys_enter")
int dump_type(void *ctx)
{
	struct btf_ptr ptr = {
		.ptr = obj,
		.type_id = type_id,
		.flags = 0,
	};

	ret = bpf_snprintf_btf(out, sizeof(out), &ptr, sizeof(ptr), 0);
	return 0;
}

char _license[] SEC("license") = "GPL";
