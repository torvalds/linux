// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Facebook

#include <linux/bpf.h>
#include <stdint.h>
#include <stdbool.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>

char _license[] SEC("license") = "GPL";

struct {
	char in[256];
	char out[256];
	bool skip;
} data = {};

struct a_struct {
	int x;
};

struct a_complex_struct {
	union {
		struct a_struct *a;
		void *b;
	} x;
	volatile long y;
};

union a_union {
	int y;
	int z;
};

typedef struct a_struct named_struct_typedef;

typedef struct { int x, y, z; } anon_struct_typedef;

typedef struct {
	int a, b, c;
} *struct_ptr_typedef;

enum an_enum {
	AN_ENUM_VAL1 = 1,
	AN_ENUM_VAL2 = 2,
	AN_ENUM_VAL3 = 3,
};

typedef int int_typedef;

typedef enum { TYPEDEF_ENUM_VAL1, TYPEDEF_ENUM_VAL2 } enum_typedef;

typedef void *void_ptr_typedef;
typedef int *restrict restrict_ptr_typedef;

typedef int (*func_proto_typedef)(long);

typedef char arr_typedef[20];

struct core_reloc_type_based_output {
	bool struct_exists;
	bool complex_struct_exists;
	bool union_exists;
	bool enum_exists;
	bool typedef_named_struct_exists;
	bool typedef_anon_struct_exists;
	bool typedef_struct_ptr_exists;
	bool typedef_int_exists;
	bool typedef_enum_exists;
	bool typedef_void_ptr_exists;
	bool typedef_restrict_ptr_exists;
	bool typedef_func_proto_exists;
	bool typedef_arr_exists;

	bool struct_matches;
	bool complex_struct_matches;
	bool union_matches;
	bool enum_matches;
	bool typedef_named_struct_matches;
	bool typedef_anon_struct_matches;
	bool typedef_struct_ptr_matches;
	bool typedef_int_matches;
	bool typedef_enum_matches;
	bool typedef_void_ptr_matches;
	bool typedef_restrict_ptr_matches;
	bool typedef_func_proto_matches;
	bool typedef_arr_matches;

	int struct_sz;
	int union_sz;
	int enum_sz;
	int typedef_named_struct_sz;
	int typedef_anon_struct_sz;
	int typedef_struct_ptr_sz;
	int typedef_int_sz;
	int typedef_enum_sz;
	int typedef_void_ptr_sz;
	int typedef_func_proto_sz;
	int typedef_arr_sz;
};

SEC("raw_tracepoint/sys_enter")
int test_core_type_based(void *ctx)
{
	/* Support for the BPF_TYPE_MATCHES argument to the
	 * __builtin_preserve_type_info builtin was added at some point during
	 * development of clang 15 and it's what we require for this test. Part of it
	 * could run with merely __builtin_preserve_type_info (which could be checked
	 * separately), but we have to find an upper bound.
	 */
#if __has_builtin(__builtin_preserve_type_info) && __clang_major__ >= 15
	struct core_reloc_type_based_output *out = (void *)&data.out;

	out->struct_exists = bpf_core_type_exists(struct a_struct);
	out->complex_struct_exists = bpf_core_type_exists(struct a_complex_struct);
	out->union_exists = bpf_core_type_exists(union a_union);
	out->enum_exists = bpf_core_type_exists(enum an_enum);
	out->typedef_named_struct_exists = bpf_core_type_exists(named_struct_typedef);
	out->typedef_anon_struct_exists = bpf_core_type_exists(anon_struct_typedef);
	out->typedef_struct_ptr_exists = bpf_core_type_exists(struct_ptr_typedef);
	out->typedef_int_exists = bpf_core_type_exists(int_typedef);
	out->typedef_enum_exists = bpf_core_type_exists(enum_typedef);
	out->typedef_void_ptr_exists = bpf_core_type_exists(void_ptr_typedef);
	out->typedef_restrict_ptr_exists = bpf_core_type_exists(restrict_ptr_typedef);
	out->typedef_func_proto_exists = bpf_core_type_exists(func_proto_typedef);
	out->typedef_arr_exists = bpf_core_type_exists(arr_typedef);

	out->struct_matches = bpf_core_type_matches(struct a_struct);
	out->complex_struct_matches = bpf_core_type_matches(struct a_complex_struct);
	out->union_matches = bpf_core_type_matches(union a_union);
	out->enum_matches = bpf_core_type_matches(enum an_enum);
	out->typedef_named_struct_matches = bpf_core_type_matches(named_struct_typedef);
	out->typedef_anon_struct_matches = bpf_core_type_matches(anon_struct_typedef);
	out->typedef_struct_ptr_matches = bpf_core_type_matches(struct_ptr_typedef);
	out->typedef_int_matches = bpf_core_type_matches(int_typedef);
	out->typedef_enum_matches = bpf_core_type_matches(enum_typedef);
	out->typedef_void_ptr_matches = bpf_core_type_matches(void_ptr_typedef);
	out->typedef_restrict_ptr_matches = bpf_core_type_matches(restrict_ptr_typedef);
	out->typedef_func_proto_matches = bpf_core_type_matches(func_proto_typedef);
	out->typedef_arr_matches = bpf_core_type_matches(arr_typedef);

	out->struct_sz = bpf_core_type_size(struct a_struct);
	out->union_sz = bpf_core_type_size(union a_union);
	out->enum_sz = bpf_core_type_size(enum an_enum);
	out->typedef_named_struct_sz = bpf_core_type_size(named_struct_typedef);
	out->typedef_anon_struct_sz = bpf_core_type_size(anon_struct_typedef);
	out->typedef_struct_ptr_sz = bpf_core_type_size(struct_ptr_typedef);
	out->typedef_int_sz = bpf_core_type_size(int_typedef);
	out->typedef_enum_sz = bpf_core_type_size(enum_typedef);
	out->typedef_void_ptr_sz = bpf_core_type_size(void_ptr_typedef);
	out->typedef_func_proto_sz = bpf_core_type_size(func_proto_typedef);
	out->typedef_arr_sz = bpf_core_type_size(arr_typedef);
#else
	data.skip = true;
#endif
	return 0;
}
