// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2024 Yafang Shao <laoar.shao@gmail.com> */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#include "bpf_misc.h"
#include "task_kfunc_common.h"

char _license[] SEC("license") = "GPL";

int bpf_iter_bits_new(struct bpf_iter_bits *it, const u64 *unsafe_ptr__ign,
		      u32 nr_bits) __ksym __weak;
int *bpf_iter_bits_next(struct bpf_iter_bits *it) __ksym __weak;
void bpf_iter_bits_destroy(struct bpf_iter_bits *it) __ksym __weak;

SEC("iter.s/cgroup")
__description("bits iter without destroy")
__failure __msg("Unreleased reference")
int BPF_PROG(no_destroy, struct bpf_iter_meta *meta, struct cgroup *cgrp)
{
	struct bpf_iter_bits it;
	u64 data = 1;

	bpf_iter_bits_new(&it, &data, 1);
	bpf_iter_bits_next(&it);
	return 0;
}

SEC("iter/cgroup")
__description("uninitialized iter in ->next()")
__failure __msg("expected an initialized iter_bits as arg #1")
int BPF_PROG(next_uninit, struct bpf_iter_meta *meta, struct cgroup *cgrp)
{
	struct bpf_iter_bits *it = NULL;

	bpf_iter_bits_next(it);
	return 0;
}

SEC("iter/cgroup")
__description("uninitialized iter in ->destroy()")
__failure __msg("expected an initialized iter_bits as arg #1")
int BPF_PROG(destroy_uninit, struct bpf_iter_meta *meta, struct cgroup *cgrp)
{
	struct bpf_iter_bits it = {};

	bpf_iter_bits_destroy(&it);
	return 0;
}

SEC("syscall")
__description("null pointer")
__success __retval(0)
int null_pointer(void)
{
	int nr = 0;
	int *bit;

	bpf_for_each(bits, bit, NULL, 1)
		nr++;
	return nr;
}

SEC("syscall")
__description("bits copy")
__success __retval(10)
int bits_copy(void)
{
	u64 data = 0xf7310UL; /* 4 + 3 + 2 + 1 + 0*/
	int nr = 0;
	int *bit;

	bpf_for_each(bits, bit, &data, 1)
		nr++;
	return nr;
}

SEC("syscall")
__description("bits memalloc")
__success __retval(64)
int bits_memalloc(void)
{
	u64 data[2];
	int nr = 0;
	int *bit;

	__builtin_memset(&data, 0xf0, sizeof(data)); /* 4 * 16 */
	bpf_for_each(bits, bit, &data[0], sizeof(data) / sizeof(u64))
		nr++;
	return nr;
}

SEC("syscall")
__description("bit index")
__success __retval(8)
int bit_index(void)
{
	u64 data = 0x100;
	int bit_idx = 0;
	int *bit;

	bpf_for_each(bits, bit, &data, 1) {
		if (*bit == 0)
			continue;
		bit_idx = *bit;
	}
	return bit_idx;
}

SEC("syscall")
__description("bits nomem")
__success __retval(0)
int bits_nomem(void)
{
	u64 data[4];
	int nr = 0;
	int *bit;

	__builtin_memset(&data, 0xff, sizeof(data));
	bpf_for_each(bits, bit, &data[0], 513) /* Be greater than 512 */
		nr++;
	return nr;
}

SEC("syscall")
__description("fewer words")
__success __retval(1)
int fewer_words(void)
{
	u64 data[2] = {0x1, 0xff};
	int nr = 0;
	int *bit;

	bpf_for_each(bits, bit, &data[0], 1)
		nr++;
	return nr;
}

SEC("syscall")
__description("zero words")
__success __retval(0)
int zero_words(void)
{
	u64 data[2] = {0x1, 0xff};
	int nr = 0;
	int *bit;

	bpf_for_each(bits, bit, &data[0], 0)
		nr++;
	return nr;
}
