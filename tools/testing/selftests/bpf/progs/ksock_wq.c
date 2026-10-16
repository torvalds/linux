// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Isovalent */

#include "vmlinux.h"
#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>
#include "bpf_experimental.h"
#include "bpf_tracing_net.h"
#include "errno.h"
#include "ksock_common.h"

struct ksock_wq_value {
	struct bpf_wq work;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 2);
	__type(key, u32);
	__type(value, struct ksock_wq_value);
} work_map SEC(".maps");

char send_data[] = "hello from bpf workqueue";

__be16 remote_port;
int connect_send_ret;
int create_ret;
u32 callback_done;

static int ksock_wq_create_cb(void *map, int *key, void *value)
{
	struct bpf_ksock_create_opts opts = {
		.family = AF_INET,
		.type = SOCK_DGRAM,
		.protocol = IPPROTO_UDP,
	};
	struct bpf_ksock *ks;
	int err = 0;

	ks = bpf_ksock_create(&opts, sizeof(opts), &err);
	if (ks)
		bpf_ksock_release(ks);
	create_ret = err;
	__sync_fetch_and_add(&callback_done, 1);
	return 0;
}

static int ksock_wq_connect_send_cb(void *map, int *key, void *value)
{
	union bpf_ksock_addr addr = {
		.sin = {
			.sin_family = AF_INET,
			.sin_port = remote_port,
			.sin_addr.s_addr = bpf_htonl(0x7f000001),
		},
	};
	struct bpf_ksock *ks;
	int err = -ENOENT;

	ks = ksock_ctx_get();
	if (!ks)
		goto out;
	err = bpf_ksock_connect(ks, &addr, sizeof(addr));
	if (!err)
		err = bpf_ksock_send(ks, send_data, sizeof(send_data));
	bpf_ksock_release(ks);
out:
	connect_send_ret = err;
	__sync_fetch_and_add(&callback_done, 1);
	return 0;
}

static int wq_set_callback_start(int (*callback_fn)(void *, int *, void *), u32 key)
{
	struct ksock_wq_value *value;
	int err;

	value = bpf_map_lookup_elem(&work_map, &key);
	if (!value)
		return -ENOENT;

	err = bpf_wq_init(&value->work, &work_map, 0);
	if (err)
		return err;
	err = bpf_wq_set_callback(&value->work, callback_fn, 0);
	if (err)
		return err;
	err = bpf_wq_start(&value->work, 0);
	if (err)
		return err;

	return 0;
}

SEC("syscall")
int ksock_wq_start(void *ctx)
{
	struct bpf_ksock_create_opts opts = {
		.family = AF_INET,
		.type = SOCK_DGRAM,
		.protocol = IPPROTO_UDP,
	};
	struct bpf_ksock *ks;
	int err = 0;

	ks = bpf_ksock_create(&opts, sizeof(opts), &err);
	if (!ks)
		return err;
	err = ksock_ctx_insert(ks);
	if (err)
		return err;

	err = wq_set_callback_start(ksock_wq_connect_send_cb, 0);
	if (err)
		return err;

	err = wq_set_callback_start(ksock_wq_create_cb, 1);
	if (err)
		return err;

	return 0;
}

char __license[] SEC("license") = "GPL";
