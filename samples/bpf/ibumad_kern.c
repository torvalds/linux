// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB

/**
 * ibumad BPF sample kernel side
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * Copyright(c) 2018 Ira Weiny, Intel Corporation
 */

#define KBUILD_MODNAME "ibumad_count_pkts_by_class"
#include <uapi/linux/bpf.h>

#include "bpf_helpers.h"


struct bpf_map_def SEC("maps") read_count = {
	.type        = BPF_MAP_TYPE_ARRAY,
	.key_size    = sizeof(u32), /* class; u32 required */
	.value_size  = sizeof(u64), /* count of mads read */
	.max_entries = 256, /* Room for all Classes */
};

struct bpf_map_def SEC("maps") write_count = {
	.type        = BPF_MAP_TYPE_ARRAY,
	.key_size    = sizeof(u32), /* class; u32 required */
	.value_size  = sizeof(u64), /* count of mads written */
	.max_entries = 256, /* Room for all Classes */
};

#undef DEBUG
#ifndef DEBUG
#undef bpf_printk
#define bpf_printk(fmt, ...)
#endif

/* Taken from the current format defined in
 * include/trace/events/ib_umad.h
 * and
 * /sys/kernel/debug/tracing/events/ib_umad/ib_umad_read/format
 * /sys/kernel/debug/tracing/events/ib_umad/ib_umad_write/format
 */
struct ib_umad_rw_args {
	u64 pad;
	u8 port_num;
	u8 sl;
	u8 path_bits;
	u8 grh_present;
	u32 id;
	u32 status;
	u32 timeout_ms;
	u32 retires;
	u32 length;
	u32 qpn;
	u32 qkey;
	u8 gid_index;
	u8 hop_limit;
	u16 lid;
	u16 attr_id;
	u16 pkey_index;
	u8 base_version;
	u8 mgmt_class;
	u8 class_version;
	u8 method;
	u32 flow_label;
	u16 mad_status;
	u16 class_specific;
	u32 attr_mod;
	u64 tid;
	u8 gid[16];
	u32 dev_index;
	u8 traffic_class;
};

SEC("tracepoint/ib_umad/ib_umad_read_recv")
int on_ib_umad_read_recv(struct ib_umad_rw_args *ctx)
{
	u64 zero = 0, *val;
	u8 class = ctx->mgmt_class;

	bpf_printk("ib_umad read recv : class 0x%x\n", class);

	val = bpf_map_lookup_elem(&read_count, &class);
	if (!val) {
		bpf_map_update_elem(&read_count, &class, &zero, BPF_NOEXIST);
		val = bpf_map_lookup_elem(&read_count, &class);
		if (!val)
			return 0;
	}

	(*val) += 1;

	return 0;
}
SEC("tracepoint/ib_umad/ib_umad_read_send")
int on_ib_umad_read_send(struct ib_umad_rw_args *ctx)
{
	u64 zero = 0, *val;
	u8 class = ctx->mgmt_class;

	bpf_printk("ib_umad read send : class 0x%x\n", class);

	val = bpf_map_lookup_elem(&read_count, &class);
	if (!val) {
		bpf_map_update_elem(&read_count, &class, &zero, BPF_NOEXIST);
		val = bpf_map_lookup_elem(&read_count, &class);
		if (!val)
			return 0;
	}

	(*val) += 1;

	return 0;
}
SEC("tracepoint/ib_umad/ib_umad_write")
int on_ib_umad_write(struct ib_umad_rw_args *ctx)
{
	u64 zero = 0, *val;
	u8 class = ctx->mgmt_class;

	bpf_printk("ib_umad write : class 0x%x\n", class);

	val = bpf_map_lookup_elem(&write_count, &class);
	if (!val) {
		bpf_map_update_elem(&write_count, &class, &zero, BPF_NOEXIST);
		val = bpf_map_lookup_elem(&write_count, &class);
		if (!val)
			return 0;
	}

	(*val) += 1;

	return 0;
}

char _license[] SEC("license") = "GPL";
