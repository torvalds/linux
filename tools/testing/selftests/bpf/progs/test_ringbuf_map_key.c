// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

struct sample {
	int pid;
	int seq;
	long value;
	char comm[16];
};

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
} ringbuf SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1000);
	__type(key, struct sample);
	__type(value, int);
} hash_map SEC(".maps");

/* inputs */
int pid = 0;

/* inner state */
long seq = 0;

SEC("fentry/" SYS_PREFIX "sys_getpgid")
int test_ringbuf_mem_map_key(void *ctx)
{
	int cur_pid = bpf_get_current_pid_tgid() >> 32;
	struct sample *sample, sample_copy;
	int *lookup_val;

	if (cur_pid != pid)
		return 0;

	sample = bpf_ringbuf_reserve(&ringbuf, sizeof(*sample), 0);
	if (!sample)
		return 0;

	sample->pid = pid;
	bpf_get_current_comm(sample->comm, sizeof(sample->comm));
	sample->seq = ++seq;
	sample->value = 42;

	/* test using 'sample' (PTR_TO_MEM | MEM_ALLOC) as map key arg
	 */
	lookup_val = (int *)bpf_map_lookup_elem(&hash_map, sample);

	/* workaround - memcpy is necessary so that verifier doesn't
	 * complain with:
	 *   verifier internal error: more than one arg with ref_obj_id R3
	 * when trying to do bpf_map_update_elem(&hash_map, sample, &sample->seq, BPF_ANY);
	 *
	 * Since bpf_map_lookup_elem above uses 'sample' as key, test using
	 * sample field as value below
	 */
	__builtin_memcpy(&sample_copy, sample, sizeof(struct sample));
	bpf_map_update_elem(&hash_map, &sample_copy, &sample->seq, BPF_ANY);

	bpf_ringbuf_submit(sample, 0);
	return 0;
}
