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
#include "bpf_kfuncs.h"
#include "err.h"

#define MAX_DATA_SIZE (1024 * 1024)
#define MAX_SIG_SIZE 1024

__u32 monitored_pid;
__u32 user_keyring_serial;
__u64 system_keyring_id;

struct data {
	__u8 data[MAX_DATA_SIZE];
	__u32 data_len;
	__u8 sig[MAX_SIG_SIZE];
	__u32 sig_len;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct data);
} data_input SEC(".maps");

char _license[] SEC("license") = "GPL";

SEC("lsm.s/bpf")
int BPF_PROG(bpf, int cmd, union bpf_attr *attr, unsigned int size)
{
	struct bpf_dynptr data_ptr, sig_ptr;
	struct data *data_val;
	struct bpf_key *trusted_keyring;
	__u32 pid;
	__u64 value;
	int ret, zero = 0;

	pid = bpf_get_current_pid_tgid() >> 32;
	if (pid != monitored_pid)
		return 0;

	data_val = bpf_map_lookup_elem(&data_input, &zero);
	if (!data_val)
		return 0;

	ret = bpf_probe_read_kernel(&value, sizeof(value), &attr->value);
	if (ret)
		goto out;

	ret = bpf_copy_from_user(data_val, sizeof(struct data),
				 (void *)(unsigned long)value);
	if (ret)
		goto out;

	if (data_val->data_len > sizeof(data_val->data))
		return -EINVAL;

	bpf_dynptr_from_mem(data_val->data, data_val->data_len, 0, &data_ptr);

	if (data_val->sig_len > sizeof(data_val->sig))
		return -EINVAL;

	bpf_dynptr_from_mem(data_val->sig, data_val->sig_len, 0, &sig_ptr);

	if (user_keyring_serial)
		trusted_keyring = bpf_lookup_user_key(user_keyring_serial, 0);
	else
		trusted_keyring = bpf_lookup_system_key(system_keyring_id);

	if (!trusted_keyring)
		return -ENOENT;

	ret = bpf_verify_pkcs7_signature(&data_ptr, &sig_ptr, trusted_keyring);

	bpf_key_put(trusted_keyring);

out:
	set_if_not_errno_or_zero(ret, -EFAULT);

	return ret;
}
