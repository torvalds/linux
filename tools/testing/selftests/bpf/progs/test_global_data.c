// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Isovalent, Inc.

#include <linux/bpf.h>
#include <linux/pkt_cls.h>
#include <string.h>

#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 11);
	__type(key, __u32);
	__type(value, __u64);
} result_number SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 5);
	__type(key, __u32);
	const char (*value)[32];
} result_string SEC(".maps");

struct foo {
	__u8  a;
	__u32 b;
	__u64 c;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 5);
	__type(key, __u32);
	__type(value, struct foo);
} result_struct SEC(".maps");

/* Relocation tests for __u64s. */
static       __u64 num0;
static       __u64 num1 = 42;
static const __u64 num2 = 24;
static       __u64 num3 = 0;
static       __u64 num4 = 0xffeeff;
static const __u64 num5 = 0xabab;
static const __u64 num6 = 0xab;

/* Relocation tests for strings. */
static const char str0[32] = "abcdefghijklmnopqrstuvwxyz";
static       char str1[32] = "abcdefghijklmnopqrstuvwxyz";
static       char str2[32];

/* Relocation tests for structs. */
static const struct foo struct0 = {
	.a = 42,
	.b = 0xfefeefef,
	.c = 0x1111111111111111ULL,
};
static struct foo struct1;
static const struct foo struct2;
static struct foo struct3 = {
	.a = 41,
	.b = 0xeeeeefef,
	.c = 0x2111111111111111ULL,
};

#define test_reloc(map, num, var)					\
	do {								\
		__u32 key = num;					\
		bpf_map_update_elem(&result_##map, &key, var, 0);	\
	} while (0)

SEC("tc")
int load_static_data(struct __sk_buff *skb)
{
	static const __u64 bar = ~0;

	test_reloc(number, 0, &num0);
	test_reloc(number, 1, &num1);
	test_reloc(number, 2, &num2);
	test_reloc(number, 3, &num3);
	test_reloc(number, 4, &num4);
	test_reloc(number, 5, &num5);
	num4 = 1234;
	test_reloc(number, 6, &num4);
	test_reloc(number, 7, &num0);
	test_reloc(number, 8, &num6);

	test_reloc(string, 0, str0);
	test_reloc(string, 1, str1);
	test_reloc(string, 2, str2);
	str1[5] = 'x';
	test_reloc(string, 3, str1);
	__builtin_memcpy(&str2[2], "hello", sizeof("hello"));
	test_reloc(string, 4, str2);

	test_reloc(struct, 0, &struct0);
	test_reloc(struct, 1, &struct1);
	test_reloc(struct, 2, &struct2);
	test_reloc(struct, 3, &struct3);

	test_reloc(number,  9, &struct0.c);
	test_reloc(number, 10, &bar);

	return TC_ACT_OK;
}

char _license[] SEC("license") = "GPL";
