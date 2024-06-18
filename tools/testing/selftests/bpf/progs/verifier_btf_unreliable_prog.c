// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2017 Facebook

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "bpf_misc.h"

struct whatever {};

SEC("kprobe")
__success __log_level(2)
/* context type is wrong, making it impossible to freplace this program */
int btf_unreliable_kprobe(struct whatever *ctx)
{
	return 0;
}

char _license[] SEC("license") = "GPL";
