// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
} ringbuf SEC(".maps");

/* inputs */
int pid = 0;

/* outputs */
long passed = 0;
long discarded = 0;

SEC("fentry/" SYS_PREFIX "sys_getpgid")
int test_ringbuf_write(void *ctx)
{
	int *foo, cur_pid = bpf_get_current_pid_tgid() >> 32;
	void *sample1, *sample2;

	if (cur_pid != pid)
		return 0;

	sample1 = bpf_ringbuf_reserve(&ringbuf, 0x3000, 0);
	if (!sample1)
		return 0;
	/* first one can pass */
	sample2 = bpf_ringbuf_reserve(&ringbuf, 0x3000, 0);
	if (!sample2) {
		bpf_ringbuf_discard(sample1, 0);
		__sync_fetch_and_add(&discarded, 1);
		return 0;
	}
	/* second one must not */
	__sync_fetch_and_add(&passed, 1);
	foo = sample2 + 4084;
	*foo = 256;
	bpf_ringbuf_discard(sample1, 0);
	bpf_ringbuf_discard(sample2, 0);
	return 0;
}
