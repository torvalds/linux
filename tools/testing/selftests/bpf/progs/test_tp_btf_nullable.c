// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "../test_kmods/bpf_testmod.h"
#include "bpf_misc.h"

SEC("tp_btf/bpf_testmod_test_nullable_bare")
__failure __msg("R1 invalid mem access 'trusted_ptr_or_null_'")
int BPF_PROG(handle_tp_btf_nullable_bare1, struct bpf_testmod_test_read_ctx *nullable_ctx)
{
	return nullable_ctx->len;
}

SEC("tp_btf/bpf_testmod_test_nullable_bare")
int BPF_PROG(handle_tp_btf_nullable_bare2, struct bpf_testmod_test_read_ctx *nullable_ctx)
{
	if (nullable_ctx)
		return nullable_ctx->len;
	return 0;
}

char _license[] SEC("license") = "GPL";
