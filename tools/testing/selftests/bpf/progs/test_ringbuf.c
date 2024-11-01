// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Facebook

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

/* inputs */
int pid = 0;
long value = 0;
long flags = 0;

/* outputs */
long total = 0;
long discarded = 0;
long dropped = 0;

long avail_data = 0;
long ring_size = 0;
long cons_pos = 0;
long prod_pos = 0;

/* inner state */
long seq = 0;

SEC("fentry/" SYS_PREFIX "sys_getpgid")
int test_ringbuf(void *ctx)
{
	int cur_pid = bpf_get_current_pid_tgid() >> 32;
	struct sample *sample;
	int zero = 0;

	if (cur_pid != pid)
		return 0;

	sample = bpf_ringbuf_reserve(&ringbuf, sizeof(*sample), 0);
	if (!sample) {
		__sync_fetch_and_add(&dropped, 1);
		return 0;
	}

	sample->pid = pid;
	bpf_get_current_comm(sample->comm, sizeof(sample->comm));
	sample->value = value;

	sample->seq = seq++;
	__sync_fetch_and_add(&total, 1);

	if (sample->seq & 1) {
		/* copy from reserved sample to a new one... */
		bpf_ringbuf_output(&ringbuf, sample, sizeof(*sample), flags);
		/* ...and then discard reserved sample */
		bpf_ringbuf_discard(sample, flags);
		__sync_fetch_and_add(&discarded, 1);
	} else {
		bpf_ringbuf_submit(sample, flags);
	}

	avail_data = bpf_ringbuf_query(&ringbuf, BPF_RB_AVAIL_DATA);
	ring_size = bpf_ringbuf_query(&ringbuf, BPF_RB_RING_SIZE);
	cons_pos = bpf_ringbuf_query(&ringbuf, BPF_RB_CONS_POS);
	prod_pos = bpf_ringbuf_query(&ringbuf, BPF_RB_PROD_POS);

	return 0;
}
