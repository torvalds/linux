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

/* some types are shared with test_core_reloc_type_based.c */
struct a_struct {
	int x;
};

union a_union {
	int y;
	int z;
};

enum an_enum {
	AN_ENUM_VAL1 = 1,
	AN_ENUM_VAL2 = 2,
	AN_ENUM_VAL3 = 3,
};

typedef struct a_struct named_struct_typedef;

typedef int (*func_proto_typedef)(long);

typedef char arr_typedef[20];

struct core_reloc_type_id_output {
	int local_anon_struct;
	int local_anon_union;
	int local_anon_enum;
	int local_anon_func_proto_ptr;
	int local_anon_void_ptr;
	int local_anon_arr;

	int local_struct;
	int local_union;
	int local_enum;
	int local_int;
	int local_struct_typedef;
	int local_func_proto_typedef;
	int local_arr_typedef;

	int targ_struct;
	int targ_union;
	int targ_enum;
	int targ_int;
	int targ_struct_typedef;
	int targ_func_proto_typedef;
	int targ_arr_typedef;
};

/* preserve types even if Clang doesn't support built-in */
struct a_struct t1 = {};
union a_union t2 = {};
enum an_enum t3 = 0;
named_struct_typedef t4 = {};
func_proto_typedef t5 = 0;
arr_typedef t6 = {};

SEC("raw_tracepoint/sys_enter")
int test_core_type_id(void *ctx)
{
	/* We use __builtin_btf_type_id() in this tests, but up until the time
	 * __builtin_preserve_type_info() was added it contained a bug that
	 * would make this test fail. The bug was fixed ([0]) with addition of
	 * __builtin_preserve_type_info(), though, so that's what we are using
	 * to detect whether this test has to be executed, however strange
	 * that might look like.
	 *
	 *   [0] https://reviews.llvm.org/D85174
	 */
#if __has_builtin(__builtin_preserve_type_info)
	struct core_reloc_type_id_output *out = (void *)&data.out;

	out->local_anon_struct = bpf_core_type_id_local(struct { int marker_field; });
	out->local_anon_union = bpf_core_type_id_local(union { int marker_field; });
	out->local_anon_enum = bpf_core_type_id_local(enum { MARKER_ENUM_VAL = 123 });
	out->local_anon_func_proto_ptr = bpf_core_type_id_local(_Bool(*)(int));
	out->local_anon_void_ptr = bpf_core_type_id_local(void *);
	out->local_anon_arr = bpf_core_type_id_local(_Bool[47]);

	out->local_struct = bpf_core_type_id_local(struct a_struct);
	out->local_union = bpf_core_type_id_local(union a_union);
	out->local_enum = bpf_core_type_id_local(enum an_enum);
	out->local_int = bpf_core_type_id_local(int);
	out->local_struct_typedef = bpf_core_type_id_local(named_struct_typedef);
	out->local_func_proto_typedef = bpf_core_type_id_local(func_proto_typedef);
	out->local_arr_typedef = bpf_core_type_id_local(arr_typedef);

	out->targ_struct = bpf_core_type_id_kernel(struct a_struct);
	out->targ_union = bpf_core_type_id_kernel(union a_union);
	out->targ_enum = bpf_core_type_id_kernel(enum an_enum);
	out->targ_int = bpf_core_type_id_kernel(int);
	out->targ_struct_typedef = bpf_core_type_id_kernel(named_struct_typedef);
	out->targ_func_proto_typedef = bpf_core_type_id_kernel(func_proto_typedef);
	out->targ_arr_typedef = bpf_core_type_id_kernel(arr_typedef);
#else
	data.skip = true;
#endif

	return 0;
}
