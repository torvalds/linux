/* Copyright (c) 2016 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <linux/ptrace.h>
#include <uapi/linux/bpf.h>
#include <uapi/linux/bpf_perf_event.h>
#include <uapi/linux/perf_event.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

struct key_t {
	char comm[TASK_COMM_LEN];
	u32 kernstack;
	u32 userstack;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, struct key_t);
	__type(value, u64);
	__uint(max_entries, 10000);
} counts SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_STACK_TRACE);
	__uint(key_size, sizeof(u32));
	__uint(value_size, PERF_MAX_STACK_DEPTH * sizeof(u64));
	__uint(max_entries, 10000);
} stackmap SEC(".maps");

#define KERN_STACKID_FLAGS (0 | BPF_F_FAST_STACK_CMP)
#define USER_STACKID_FLAGS (0 | BPF_F_FAST_STACK_CMP | BPF_F_USER_STACK)

SEC("perf_event")
int bpf_prog1(struct bpf_perf_event_data *ctx)
{
	char time_fmt1[] = "Time Enabled: %llu, Time Running: %llu";
	char time_fmt2[] = "Get Time Failed, ErrCode: %d";
	char addr_fmt[] = "Address recorded on event: %llx";
	char fmt[] = "CPU-%d period %lld ip %llx";
	u32 cpu = bpf_get_smp_processor_id();
	struct bpf_perf_event_value value_buf;
	struct key_t key;
	u64 *val, one = 1;
	int ret;

	if (ctx->sample_period < 10000)
		/* ignore warmup */
		return 0;
	bpf_get_current_comm(&key.comm, sizeof(key.comm));
	key.kernstack = bpf_get_stackid(ctx, &stackmap, KERN_STACKID_FLAGS);
	key.userstack = bpf_get_stackid(ctx, &stackmap, USER_STACKID_FLAGS);
	if ((int)key.kernstack < 0 && (int)key.userstack < 0) {
		bpf_trace_printk(fmt, sizeof(fmt), cpu, ctx->sample_period,
				 PT_REGS_IP(&ctx->regs));
		return 0;
	}

	ret = bpf_perf_prog_read_value(ctx, (void *)&value_buf, sizeof(struct bpf_perf_event_value));
	if (!ret)
	  bpf_trace_printk(time_fmt1, sizeof(time_fmt1), value_buf.enabled, value_buf.running);
	else
	  bpf_trace_printk(time_fmt2, sizeof(time_fmt2), ret);

	if (ctx->addr != 0)
	  bpf_trace_printk(addr_fmt, sizeof(addr_fmt), ctx->addr);

	val = bpf_map_lookup_elem(&counts, &key);
	if (val)
		(*val)++;
	else
		bpf_map_update_elem(&counts, &key, &one, BPF_NOEXIST);
	return 0;
}

char _license[] SEC("license") = "GPL";
