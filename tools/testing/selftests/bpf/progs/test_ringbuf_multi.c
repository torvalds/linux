// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Facebook

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

struct sample {
	int pid;
	int seq;
	long value;
	char comm[16];
};

struct ringbuf_map {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
} ringbuf1 SEC(".maps"),
  ringbuf2 SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY_OF_MAPS);
	__uint(max_entries, 4);
	__type(key, int);
	__array(values, struct ringbuf_map);
} ringbuf_arr SEC(".maps") = {
	.values = {
		[0] = &ringbuf1,
		[2] = &ringbuf2,
	},
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH_OF_MAPS);
	__uint(max_entries, 1);
	__type(key, int);
	__array(values, struct ringbuf_map);
} ringbuf_hash SEC(".maps") = {
	.values = {
		[0] = &ringbuf1,
	},
};

/* inputs */
int pid = 0;
int target_ring = 0;
long value = 0;

/* outputs */
long total = 0;
long dropped = 0;
long skipped = 0;

SEC("tp/syscalls/sys_enter_getpgid")
int test_ringbuf(void *ctx)
{
	int cur_pid = bpf_get_current_pid_tgid() >> 32;
	struct sample *sample;
	void *rb;
	int zero = 0;

	if (cur_pid != pid)
		return 0;

	rb = bpf_map_lookup_elem(&ringbuf_arr, &target_ring);
	if (!rb) {
		skipped += 1;
		return 1;
	}

	sample = bpf_ringbuf_reserve(rb, sizeof(*sample), 0);
	if (!sample) {
		dropped += 1;
		return 1;
	}

	sample->pid = pid;
	bpf_get_current_comm(sample->comm, sizeof(sample->comm));
	sample->value = value;

	sample->seq = total;
	total += 1;

	bpf_ringbuf_submit(sample, 0);

	return 0;
}
