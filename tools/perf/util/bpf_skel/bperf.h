// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
// Copyright (c) 2021 Facebook

#ifndef __BPERF_STAT_H
#define __BPERF_STAT_H

typedef struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(struct bpf_perf_event_value));
	__uint(max_entries, 1);
} reading_map;

#endif /* __BPERF_STAT_H */
