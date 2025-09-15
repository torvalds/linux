// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (C) 2022 Huawei Technologies Duesseldorf GmbH
 *
 * Author: Roberto Sassu <roberto.sassu@huawei.com>
 */

#include "vmlinux.h"
#include <errno.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

__u32 monitored_pid;
__s32 key_serial;
__u32 key_id;
__u64 flags;

extern struct bpf_key *bpf_lookup_user_key(__s32 serial, __u64 flags) __ksym;
extern struct bpf_key *bpf_lookup_system_key(__u64 id) __ksym;
extern void bpf_key_put(struct bpf_key *key) __ksym;

SEC("lsm.s/bpf")
int BPF_PROG(bpf, int cmd, union bpf_attr *attr, unsigned int size, bool kernel)
{
	struct bpf_key *bkey;
	__u32 pid;

	pid = bpf_get_current_pid_tgid() >> 32;
	if (pid != monitored_pid)
		return 0;

	if (key_serial)
		bkey = bpf_lookup_user_key(key_serial, flags);
	else
		bkey = bpf_lookup_system_key(key_id);

	if (!bkey)
		return -ENOENT;

	bpf_key_put(bkey);

	return 0;
}
