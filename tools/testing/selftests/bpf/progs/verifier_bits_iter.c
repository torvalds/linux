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

u64 bits_array[511] = {};

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
__failure __msg("expected an initialized iter_bits as arg #0")
int BPF_PROG(next_uninit, struct bpf_iter_meta *meta, struct cgroup *cgrp)
{
	struct bpf_iter_bits it = {};

	bpf_iter_bits_next(&it);
	return 0;
}

SEC("iter/cgroup")
__description("uninitialized iter in ->destroy()")
__failure __msg("expected an initialized iter_bits as arg #0")
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
	struct bpf_iter_bits iter;
	int err, nr = 0;
	int *bit;

	err = bpf_iter_bits_new(&iter, NULL, 1);
	bpf_iter_bits_destroy(&iter);
	if (err != -EINVAL)
		return 1;

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
	bpf_for_each(bits, bit, &data[0], ARRAY_SIZE(data))
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
__description("bits too big")
__success __retval(0)
int bits_too_big(void)
{
	u64 data[4];
	int nr = 0;
	int *bit;

	__builtin_memset(&data, 0xff, sizeof(data));
	bpf_for_each(bits, bit, &data[0], 512) /* Be greater than 511 */
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

SEC("syscall")
__description("huge words")
__success __retval(0)
int huge_words(void)
{
	u64 data[8] = {0x1, 0x1, 0x1, 0x1, 0x1, 0x1, 0x1, 0x1};
	int nr = 0;
	int *bit;

	bpf_for_each(bits, bit, &data[0], 67108865)
		nr++;
	return nr;
}

SEC("syscall")
__description("max words")
__success __retval(4)
int max_words(void)
{
	volatile int nr = 0;
	int *bit;

	bits_array[0] = (1ULL << 63) | 1U;
	bits_array[510] = (1ULL << 33) | (1ULL << 32);

	bpf_for_each(bits, bit, bits_array, 511) {
		if (nr == 0 && *bit != 0)
			break;
		if (nr == 2 && *bit != 32672)
			break;
		nr++;
	}
	return nr;
}

SEC("syscall")
__description("bad words")
__success __retval(0)
int bad_words(void)
{
	void *bad_addr = (void *)-4095;
	struct bpf_iter_bits iter;
	volatile int nr;
	int *bit;
	int err;

	err = bpf_iter_bits_new(&iter, bad_addr, 1);
	bpf_iter_bits_destroy(&iter);
	if (err != -EFAULT)
		return 1;

	nr = 0;
	bpf_for_each(bits, bit, bad_addr, 1)
		nr++;
	if (nr != 0)
		return 2;

	err = bpf_iter_bits_new(&iter, bad_addr, 4);
	bpf_iter_bits_destroy(&iter);
	if (err != -EFAULT)
		return 3;

	nr = 0;
	bpf_for_each(bits, bit, bad_addr, 4)
		nr++;
	if (nr != 0)
		return 4;

	return 0;
}
