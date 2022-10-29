/* Copyright (c) 2013-2015 PLUMgrid, http://plumgrid.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/version.h>
#include <uapi/linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "trace_common.h"

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, long);
	__type(value, long);
	__uint(max_entries, 1024);
} my_map SEC(".maps");

/* kprobe is NOT a stable ABI. If kernel internals change this bpf+kprobe
 * example will no longer be meaningful
 */
SEC("kprobe/kfree_skb_reason")
int bpf_prog2(struct pt_regs *ctx)
{
	long loc = 0;
	long init_val = 1;
	long *value;

	/* read ip of kfree_skb_reason caller.
	 * non-portable version of __builtin_return_address(0)
	 */
	BPF_KPROBE_READ_RET_IP(loc, ctx);

	value = bpf_map_lookup_elem(&my_map, &loc);
	if (value)
		*value += 1;
	else
		bpf_map_update_elem(&my_map, &loc, &init_val, BPF_ANY);
	return 0;
}

static unsigned int log2(unsigned int v)
{
	unsigned int r;
	unsigned int shift;

	r = (v > 0xFFFF) << 4; v >>= r;
	shift = (v > 0xFF) << 3; v >>= shift; r |= shift;
	shift = (v > 0xF) << 2; v >>= shift; r |= shift;
	shift = (v > 0x3) << 1; v >>= shift; r |= shift;
	r |= (v >> 1);
	return r;
}

static unsigned int log2l(unsigned long v)
{
	unsigned int hi = v >> 32;
	if (hi)
		return log2(hi) + 32;
	else
		return log2(v);
}

struct hist_key {
	char comm[16];
	u64 pid_tgid;
	u64 uid_gid;
	u64 index;
};

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_HASH);
	__uint(key_size, sizeof(struct hist_key));
	__uint(value_size, sizeof(long));
	__uint(max_entries, 1024);
} my_hist_map SEC(".maps");

SEC("kprobe/" SYSCALL(sys_write))
int bpf_prog3(struct pt_regs *ctx)
{
	long write_size = PT_REGS_PARM3(ctx);
	long init_val = 1;
	long *value;
	struct hist_key key;

	key.index = log2l(write_size);
	key.pid_tgid = bpf_get_current_pid_tgid();
	key.uid_gid = bpf_get_current_uid_gid();
	bpf_get_current_comm(&key.comm, sizeof(key.comm));

	value = bpf_map_lookup_elem(&my_hist_map, &key);
	if (value)
		__sync_fetch_and_add(value, 1);
	else
		bpf_map_update_elem(&my_hist_map, &key, &init_val, BPF_ANY);
	return 0;
}
char _license[] SEC("license") = "GPL";
u32 _version SEC("version") = LINUX_VERSION_CODE;
