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

extern struct bpf_key *bpf_lookup_system_key(__u64 id) __ksym;
extern void bpf_key_put(struct bpf_key *key) __ksym;
extern int bpf_verify_pkcs7_signature(struct bpf_dynptr *data_ptr,
				      struct bpf_dynptr *sig_ptr,
				      struct bpf_key *trusted_keyring) __ksym;

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
} ringbuf SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u32);
} array_map SEC(".maps");

int err, pid;

char _license[] SEC("license") = "GPL";

SEC("?lsm.s/bpf")
int BPF_PROG(dynptr_type_not_supp, int cmd, union bpf_attr *attr,
	     unsigned int size)
{
	char write_data[64] = "hello there, world!!";
	struct bpf_dynptr ptr;

	bpf_ringbuf_reserve_dynptr(&ringbuf, sizeof(write_data), 0, &ptr);

	return bpf_verify_pkcs7_signature(&ptr, &ptr, NULL);
}

SEC("?lsm.s/bpf")
int BPF_PROG(not_valid_dynptr, int cmd, union bpf_attr *attr, unsigned int size)
{
	unsigned long val;

	return bpf_verify_pkcs7_signature((struct bpf_dynptr *)&val,
					  (struct bpf_dynptr *)&val, NULL);
}

SEC("?lsm.s/bpf")
int BPF_PROG(not_ptr_to_stack, int cmd, union bpf_attr *attr, unsigned int size)
{
	unsigned long val;

	return bpf_verify_pkcs7_signature((struct bpf_dynptr *)val,
					  (struct bpf_dynptr *)val, NULL);
}

SEC("lsm.s/bpf")
int BPF_PROG(dynptr_data_null, int cmd, union bpf_attr *attr, unsigned int size)
{
	struct bpf_key *trusted_keyring;
	struct bpf_dynptr ptr;
	__u32 *value;
	int ret, zero = 0;

	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 0;

	value = bpf_map_lookup_elem(&array_map, &zero);
	if (!value)
		return 0;

	/* Pass invalid flags. */
	ret = bpf_dynptr_from_mem(value, sizeof(*value), ((__u64)~0ULL), &ptr);
	if (ret != -EINVAL)
		return 0;

	trusted_keyring = bpf_lookup_system_key(0);
	if (!trusted_keyring)
		return 0;

	err = bpf_verify_pkcs7_signature(&ptr, &ptr, trusted_keyring);

	bpf_key_put(trusted_keyring);

	return 0;
}
