// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_kfuncs.h"

char _license[] SEC("license") = "GPL";

#ifndef SHA256_DIGEST_SIZE
#define SHA256_DIGEST_SIZE      32
#endif

#define SIZEOF_STRUCT_FSVERITY_DIGEST 4  /* sizeof(struct fsverity_digest) */

char expected_digest[SIZEOF_STRUCT_FSVERITY_DIGEST + SHA256_DIGEST_SIZE];
char digest[SIZEOF_STRUCT_FSVERITY_DIGEST + SHA256_DIGEST_SIZE];
__u32 monitored_pid;
__u32 got_fsverity;
__u32 digest_matches;

SEC("lsm.s/file_open")
int BPF_PROG(test_file_open, struct file *f)
{
	struct bpf_dynptr digest_ptr;
	__u32 pid;
	int ret;
	int i;

	pid = bpf_get_current_pid_tgid() >> 32;
	if (pid != monitored_pid)
		return 0;

	bpf_dynptr_from_mem(digest, sizeof(digest), 0, &digest_ptr);
	ret = bpf_get_fsverity_digest(f, &digest_ptr);
	if (ret < 0)
		return 0;
	got_fsverity = 1;

	for (i = 0; i < sizeof(digest); i++) {
		if (digest[i] != expected_digest[i])
			return 0;
	}

	digest_matches = 1;
	return 0;
}
