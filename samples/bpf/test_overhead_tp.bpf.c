/* Copyright (c) 2016 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

/* from /sys/kernel/tracing/events/task/task_rename/format */
SEC("tracepoint/task/task_rename")
int prog(struct trace_event_raw_task_rename *ctx)
{
	return 0;
}

/* from /sys/kernel/tracing/events/fib/fib_table_lookup/format */
SEC("tracepoint/fib/fib_table_lookup")
int prog2(struct trace_event_raw_fib_table_lookup *ctx)
{
	return 0;
}
char _license[] SEC("license") = "GPL";
