// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include "bpf_misc.h"

SEC("syscall")
__failure __msg("Possibly NULL pointer passed")
int stream_vprintk_null_arg(void *ctx)
{
	bpf_stream_vprintk(BPF_STDOUT, "", NULL, 0, NULL);
	return 0;
}

SEC("syscall")
__failure __msg("R3 type=scalar expected=")
int stream_vprintk_scalar_arg(void *ctx)
{
	bpf_stream_vprintk(BPF_STDOUT, "", (void *)46, 0, NULL);
	return 0;
}

SEC("syscall")
__failure __msg("arg#1 doesn't point to a const string")
int stream_vprintk_string_arg(void *ctx)
{
	bpf_stream_vprintk(BPF_STDOUT, ctx, NULL, 0, NULL);
	return 0;
}

char _license[] SEC("license") = "GPL";
