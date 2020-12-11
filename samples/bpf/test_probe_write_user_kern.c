/* Copyright (c) 2016 Sargun Dhillon <sargun@sargun.me>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <uapi/linux/bpf.h>
#include <linux/version.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "trace_common.h"

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, struct sockaddr_in);
	__type(value, struct sockaddr_in);
	__uint(max_entries, 256);
} dnat_map SEC(".maps");

/* kprobe is NOT a stable ABI
 * kernel functions can be removed, renamed or completely change semantics.
 * Number of arguments and their positions can change, etc.
 * In such case this bpf+kprobe example will no longer be meaningful
 *
 * This example sits on a syscall, and the syscall ABI is relatively stable
 * of course, across platforms, and over time, the ABI may change.
 */
SEC("kprobe/" SYSCALL(sys_connect))
int bpf_prog1(struct pt_regs *ctx)
{
	struct pt_regs *real_regs = (struct pt_regs *)PT_REGS_PARM1_CORE(ctx);
	void *sockaddr_arg = (void *)PT_REGS_PARM2_CORE(real_regs);
	int sockaddr_len = (int)PT_REGS_PARM3_CORE(real_regs);
	struct sockaddr_in new_addr, orig_addr = {};
	struct sockaddr_in *mapped_addr;

	if (sockaddr_len > sizeof(orig_addr))
		return 0;

	if (bpf_probe_read_user(&orig_addr, sizeof(orig_addr), sockaddr_arg) != 0)
		return 0;

	mapped_addr = bpf_map_lookup_elem(&dnat_map, &orig_addr);
	if (mapped_addr != NULL) {
		memcpy(&new_addr, mapped_addr, sizeof(new_addr));
		bpf_probe_write_user(sockaddr_arg, &new_addr,
				     sizeof(new_addr));
	}
	return 0;
}

char _license[] SEC("license") = "GPL";
u32 _version SEC("version") = LINUX_VERSION_CODE;
