// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */

#include <linux/bpf.h>
#include <stdint.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>

char _license[] SEC("license") = "GPL";

/* fields of exactly the same size */
struct test_struct___samesize {
	void *ptr;
	unsigned long long val1;
	unsigned int val2;
	unsigned short val3;
	unsigned char val4;
} __attribute((preserve_access_index));

/* unsigned fields that have to be downsized by libbpf */
struct test_struct___downsize {
	void *ptr;
	unsigned long val1;
	unsigned long val2;
	unsigned long val3;
	unsigned long val4;
	/* total sz: 40 */
} __attribute__((preserve_access_index));

/* fields with signed integers of wrong size, should be rejected */
struct test_struct___signed {
	void *ptr;
	long val1;
	long val2;
	long val3;
	long val4;
} __attribute((preserve_access_index));

/* real layout and sizes according to test's (32-bit) BTF */
struct test_struct___real {
	unsigned int ptr; /* can't use `void *`, it is always 8 byte in BPF target */
	unsigned int val2;
	unsigned long long val1;
	unsigned short val3;
	unsigned char val4;
	unsigned char _pad;
	/* total sz: 20 */
};

struct test_struct___real input = {
	.ptr = 0x01020304,
	.val1 = 0x1020304050607080,
	.val2 = 0x0a0b0c0d,
	.val3 = 0xfeed,
	.val4 = 0xb9,
	._pad = 0xff, /* make sure no accidental zeros are present */
};

unsigned long long ptr_samesized = 0;
unsigned long long val1_samesized = 0;
unsigned long long val2_samesized = 0;
unsigned long long val3_samesized = 0;
unsigned long long val4_samesized = 0;
struct test_struct___real output_samesized = {};

unsigned long long ptr_downsized = 0;
unsigned long long val1_downsized = 0;
unsigned long long val2_downsized = 0;
unsigned long long val3_downsized = 0;
unsigned long long val4_downsized = 0;
struct test_struct___real output_downsized = {};

unsigned long long ptr_probed = 0;
unsigned long long val1_probed = 0;
unsigned long long val2_probed = 0;
unsigned long long val3_probed = 0;
unsigned long long val4_probed = 0;

unsigned long long ptr_signed = 0;
unsigned long long val1_signed = 0;
unsigned long long val2_signed = 0;
unsigned long long val3_signed = 0;
unsigned long long val4_signed = 0;
struct test_struct___real output_signed = {};

SEC("raw_tp/sys_exit")
int handle_samesize(void *ctx)
{
	struct test_struct___samesize *in = (void *)&input;
	struct test_struct___samesize *out = (void *)&output_samesized;

	ptr_samesized = (unsigned long long)in->ptr;
	val1_samesized = in->val1;
	val2_samesized = in->val2;
	val3_samesized = in->val3;
	val4_samesized = in->val4;

	out->ptr = in->ptr;
	out->val1 = in->val1;
	out->val2 = in->val2;
	out->val3 = in->val3;
	out->val4 = in->val4;

	return 0;
}

SEC("raw_tp/sys_exit")
int handle_downsize(void *ctx)
{
	struct test_struct___downsize *in = (void *)&input;
	struct test_struct___downsize *out = (void *)&output_downsized;

	ptr_downsized = (unsigned long long)in->ptr;
	val1_downsized = in->val1;
	val2_downsized = in->val2;
	val3_downsized = in->val3;
	val4_downsized = in->val4;

	out->ptr = in->ptr;
	out->val1 = in->val1;
	out->val2 = in->val2;
	out->val3 = in->val3;
	out->val4 = in->val4;

	return 0;
}

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define bpf_core_read_int bpf_core_read
#else
#define bpf_core_read_int(dst, sz, src) ({ \
	/* Prevent "subtraction from stack pointer prohibited" */ \
	volatile long __off = sizeof(*dst) - (sz); \
	bpf_core_read((char *)(dst) + __off, sz, src); \
})
#endif

SEC("raw_tp/sys_enter")
int handle_probed(void *ctx)
{
	struct test_struct___downsize *in = (void *)&input;
	__u64 tmp;

	tmp = 0;
	bpf_core_read_int(&tmp, bpf_core_field_size(in->ptr), &in->ptr);
	ptr_probed = tmp;

	tmp = 0;
	bpf_core_read_int(&tmp, bpf_core_field_size(in->val1), &in->val1);
	val1_probed = tmp;

	tmp = 0;
	bpf_core_read_int(&tmp, bpf_core_field_size(in->val2), &in->val2);
	val2_probed = tmp;

	tmp = 0;
	bpf_core_read_int(&tmp, bpf_core_field_size(in->val3), &in->val3);
	val3_probed = tmp;

	tmp = 0;
	bpf_core_read_int(&tmp, bpf_core_field_size(in->val4), &in->val4);
	val4_probed = tmp;

	return 0;
}

SEC("raw_tp/sys_enter")
int handle_signed(void *ctx)
{
	struct test_struct___signed *in = (void *)&input;
	struct test_struct___signed *out = (void *)&output_signed;

	val2_signed = in->val2;
	val3_signed = in->val3;
	val4_signed = in->val4;

	out->val2= in->val2;
	out->val3= in->val3;
	out->val4= in->val4;

	return 0;
}
