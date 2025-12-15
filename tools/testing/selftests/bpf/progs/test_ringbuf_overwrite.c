// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2025. Huawei Technologies Co., Ltd */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(map_flags, BPF_F_RB_OVERWRITE);
} ringbuf SEC(".maps");

int pid;

const volatile unsigned long LEN1;
const volatile unsigned long LEN2;
const volatile unsigned long LEN3;
const volatile unsigned long LEN4;
const volatile unsigned long LEN5;

long reserve1_fail = 0;
long reserve2_fail = 0;
long reserve3_fail = 0;
long reserve4_fail = 0;
long reserve5_fail = 0;

unsigned long avail_data = 0;
unsigned long ring_size = 0;
unsigned long cons_pos = 0;
unsigned long prod_pos = 0;
unsigned long over_pos = 0;

SEC("fentry/" SYS_PREFIX "sys_getpgid")
int test_overwrite_ringbuf(void *ctx)
{
	char *rec1, *rec2, *rec3, *rec4, *rec5;
	int cur_pid = bpf_get_current_pid_tgid() >> 32;

	if (cur_pid != pid)
		return 0;

	rec1 = bpf_ringbuf_reserve(&ringbuf, LEN1, 0);
	if (!rec1) {
		reserve1_fail = 1;
		return 0;
	}

	rec2 = bpf_ringbuf_reserve(&ringbuf, LEN2, 0);
	if (!rec2) {
		bpf_ringbuf_discard(rec1, 0);
		reserve2_fail = 1;
		return 0;
	}

	rec3 = bpf_ringbuf_reserve(&ringbuf, LEN3, 0);
	/* expect failure */
	if (!rec3) {
		reserve3_fail = 1;
	} else {
		bpf_ringbuf_discard(rec1, 0);
		bpf_ringbuf_discard(rec2, 0);
		bpf_ringbuf_discard(rec3, 0);
		return 0;
	}

	rec4 = bpf_ringbuf_reserve(&ringbuf, LEN4, 0);
	if (!rec4) {
		reserve4_fail = 1;
		bpf_ringbuf_discard(rec1, 0);
		bpf_ringbuf_discard(rec2, 0);
		return 0;
	}

	bpf_ringbuf_submit(rec1, 0);
	bpf_ringbuf_submit(rec2, 0);
	bpf_ringbuf_submit(rec4, 0);

	rec5 = bpf_ringbuf_reserve(&ringbuf, LEN5, 0);
	if (!rec5) {
		reserve5_fail = 1;
		return 0;
	}

	for (int i = 0; i < LEN3; i++)
		rec5[i] = 0xdd;

	bpf_ringbuf_submit(rec5, 0);

	ring_size = bpf_ringbuf_query(&ringbuf, BPF_RB_RING_SIZE);
	avail_data = bpf_ringbuf_query(&ringbuf, BPF_RB_AVAIL_DATA);
	cons_pos = bpf_ringbuf_query(&ringbuf, BPF_RB_CONS_POS);
	prod_pos = bpf_ringbuf_query(&ringbuf, BPF_RB_PROD_POS);
	over_pos = bpf_ringbuf_query(&ringbuf, BPF_RB_OVERWRITE_POS);

	return 0;
}
