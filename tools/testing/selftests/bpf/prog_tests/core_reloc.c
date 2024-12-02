// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <test_progs.h>
#include "progs/core_reloc_types.h"
#include "bpf_testmod/bpf_testmod.h"
#include <linux/limits.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <bpf/btf.h>

static int duration = 0;

#define STRUCT_TO_CHAR_PTR(struct_name) (const char *)&(struct struct_name)

#define MODULES_CASE(name, pg_name, tp_name) {				\
	.case_name = name,						\
	.bpf_obj_file = "test_core_reloc_module.bpf.o",			\
	.btf_src_file = NULL, /* find in kernel module BTFs */		\
	.input = "",							\
	.input_len = 0,							\
	.output = STRUCT_TO_CHAR_PTR(core_reloc_module_output) {	\
		.read_ctx_sz = sizeof(struct bpf_testmod_test_read_ctx),\
		.read_ctx_exists = true,				\
		.buf_exists = true,					\
		.len_exists = true,					\
		.off_exists = true,					\
		.len = 123,						\
		.off = 0,						\
		.comm = "test_progs",					\
		.comm_len = sizeof("test_progs"),			\
	},								\
	.output_len = sizeof(struct core_reloc_module_output),		\
	.prog_name = pg_name,						\
	.raw_tp_name = tp_name,						\
	.trigger = __trigger_module_test_read,				\
	.needs_testmod = true,						\
}

#define FLAVORS_DATA(struct_name) STRUCT_TO_CHAR_PTR(struct_name) {	\
	.a = 42,							\
	.b = 0xc001,							\
	.c = 0xbeef,							\
}

#define FLAVORS_CASE_COMMON(name)					\
	.case_name = #name,						\
	.bpf_obj_file = "test_core_reloc_flavors.bpf.o",		\
	.btf_src_file = "btf__core_reloc_" #name ".bpf.o",		\
	.raw_tp_name = "sys_enter",					\
	.prog_name = "test_core_flavors"				\

#define FLAVORS_CASE(name) {						\
	FLAVORS_CASE_COMMON(name),					\
	.input = FLAVORS_DATA(core_reloc_##name),			\
	.input_len = sizeof(struct core_reloc_##name),			\
	.output = FLAVORS_DATA(core_reloc_flavors),			\
	.output_len = sizeof(struct core_reloc_flavors),		\
}

#define FLAVORS_ERR_CASE(name) {					\
	FLAVORS_CASE_COMMON(name),					\
	.fails = true,							\
}

#define NESTING_DATA(struct_name) STRUCT_TO_CHAR_PTR(struct_name) {	\
	.a = { .a = { .a = 42 } },					\
	.b = { .b = { .b = 0xc001 } },					\
}

#define NESTING_CASE_COMMON(name)					\
	.case_name = #name,						\
	.bpf_obj_file = "test_core_reloc_nesting.bpf.o",		\
	.btf_src_file = "btf__core_reloc_" #name ".bpf.o",		\
	.raw_tp_name = "sys_enter",					\
	.prog_name = "test_core_nesting"				\

#define NESTING_CASE(name) {						\
	NESTING_CASE_COMMON(name),					\
	.input = NESTING_DATA(core_reloc_##name),			\
	.input_len = sizeof(struct core_reloc_##name),			\
	.output = NESTING_DATA(core_reloc_nesting),			\
	.output_len = sizeof(struct core_reloc_nesting)			\
}

#define NESTING_ERR_CASE(name) {					\
	NESTING_CASE_COMMON(name),					\
	.fails = true,							\
	.run_btfgen_fails = true,							\
}

#define ARRAYS_DATA(struct_name) STRUCT_TO_CHAR_PTR(struct_name) {	\
	.a = { [2] = 1 },						\
	.b = { [1] = { [2] = { [3] = 2 } } },				\
	.c = { [1] = { .c =  3 } },					\
	.d = { [0] = { [0] = { .d = 4 } } },				\
}

#define ARRAYS_CASE_COMMON(name)					\
	.case_name = #name,						\
	.bpf_obj_file = "test_core_reloc_arrays.bpf.o",			\
	.btf_src_file = "btf__core_reloc_" #name ".bpf.o",		\
	.raw_tp_name = "sys_enter",					\
	.prog_name = "test_core_arrays"					\

#define ARRAYS_CASE(name) {						\
	ARRAYS_CASE_COMMON(name),					\
	.input = ARRAYS_DATA(core_reloc_##name),			\
	.input_len = sizeof(struct core_reloc_##name),			\
	.output = STRUCT_TO_CHAR_PTR(core_reloc_arrays_output) {	\
		.a2   = 1,						\
		.b123 = 2,						\
		.c1c  = 3,						\
		.d00d = 4,						\
		.f10c = 0,						\
	},								\
	.output_len = sizeof(struct core_reloc_arrays_output)		\
}

#define ARRAYS_ERR_CASE(name) {						\
	ARRAYS_CASE_COMMON(name),					\
	.fails = true,							\
}

#define PRIMITIVES_DATA(struct_name) STRUCT_TO_CHAR_PTR(struct_name) {	\
	.a = 1,								\
	.b = 2,								\
	.c = 3,								\
	.d = (void *)4,							\
	.f = (void *)5,							\
}

#define PRIMITIVES_CASE_COMMON(name)					\
	.case_name = #name,						\
	.bpf_obj_file = "test_core_reloc_primitives.bpf.o",		\
	.btf_src_file = "btf__core_reloc_" #name ".bpf.o",		\
	.raw_tp_name = "sys_enter",					\
	.prog_name = "test_core_primitives"				\

#define PRIMITIVES_CASE(name) {						\
	PRIMITIVES_CASE_COMMON(name),					\
	.input = PRIMITIVES_DATA(core_reloc_##name),			\
	.input_len = sizeof(struct core_reloc_##name),			\
	.output = PRIMITIVES_DATA(core_reloc_primitives),		\
	.output_len = sizeof(struct core_reloc_primitives),		\
}

#define PRIMITIVES_ERR_CASE(name) {					\
	PRIMITIVES_CASE_COMMON(name),					\
	.fails = true,							\
}

#define MODS_CASE(name) {						\
	.case_name = #name,						\
	.bpf_obj_file = "test_core_reloc_mods.bpf.o",			\
	.btf_src_file = "btf__core_reloc_" #name ".bpf.o",		\
	.input = STRUCT_TO_CHAR_PTR(core_reloc_##name) {		\
		.a = 1,							\
		.b = 2,							\
		.c = (void *)3,						\
		.d = (void *)4,						\
		.e = { [2] = 5 },					\
		.f = { [1] = 6 },					\
		.g = { .x = 7 },					\
		.h = { .y = 8 },					\
	},								\
	.input_len = sizeof(struct core_reloc_##name),			\
	.output = STRUCT_TO_CHAR_PTR(core_reloc_mods_output) {		\
		.a = 1, .b = 2, .c = 3, .d = 4,				\
		.e = 5, .f = 6, .g = 7, .h = 8,				\
	},								\
	.output_len = sizeof(struct core_reloc_mods_output),		\
	.raw_tp_name = "sys_enter",					\
	.prog_name = "test_core_mods",					\
}

#define PTR_AS_ARR_CASE(name) {						\
	.case_name = #name,						\
	.bpf_obj_file = "test_core_reloc_ptr_as_arr.bpf.o",		\
	.btf_src_file = "btf__core_reloc_" #name ".bpf.o",		\
	.input = (const char *)&(struct core_reloc_##name []){		\
		{ .a = 1 },						\
		{ .a = 2 },						\
		{ .a = 3 },						\
	},								\
	.input_len = 3 * sizeof(struct core_reloc_##name),		\
	.output = STRUCT_TO_CHAR_PTR(core_reloc_ptr_as_arr) {		\
		.a = 3,							\
	},								\
	.output_len = sizeof(struct core_reloc_ptr_as_arr),		\
	.raw_tp_name = "sys_enter",					\
	.prog_name = "test_core_ptr_as_arr",				\
}

#define INTS_DATA(struct_name) STRUCT_TO_CHAR_PTR(struct_name) {	\
	.u8_field = 1,							\
	.s8_field = 2,							\
	.u16_field = 3,							\
	.s16_field = 4,							\
	.u32_field = 5,							\
	.s32_field = 6,							\
	.u64_field = 7,							\
	.s64_field = 8,							\
}

#define INTS_CASE_COMMON(name)						\
	.case_name = #name,						\
	.bpf_obj_file = "test_core_reloc_ints.bpf.o",			\
	.btf_src_file = "btf__core_reloc_" #name ".bpf.o",		\
	.raw_tp_name = "sys_enter",					\
	.prog_name = "test_core_ints"

#define INTS_CASE(name) {						\
	INTS_CASE_COMMON(name),						\
	.input = INTS_DATA(core_reloc_##name),				\
	.input_len = sizeof(struct core_reloc_##name),			\
	.output = INTS_DATA(core_reloc_ints),				\
	.output_len = sizeof(struct core_reloc_ints),			\
}

#define INTS_ERR_CASE(name) {						\
	INTS_CASE_COMMON(name),						\
	.fails = true,							\
}

#define FIELD_EXISTS_CASE_COMMON(name)					\
	.case_name = #name,						\
	.bpf_obj_file = "test_core_reloc_existence.bpf.o",		\
	.btf_src_file = "btf__core_reloc_" #name ".bpf.o",		\
	.raw_tp_name = "sys_enter",					\
	.prog_name = "test_core_existence"

#define BITFIELDS_CASE_COMMON(objfile, test_name_prefix,  name)		\
	.case_name = test_name_prefix#name,				\
	.bpf_obj_file = objfile,					\
	.btf_src_file = "btf__core_reloc_" #name ".bpf.o"

#define BITFIELDS_CASE(name, ...) {					\
	BITFIELDS_CASE_COMMON("test_core_reloc_bitfields_probed.bpf.o",	\
			      "probed:", name),				\
	.input = STRUCT_TO_CHAR_PTR(core_reloc_##name) __VA_ARGS__,	\
	.input_len = sizeof(struct core_reloc_##name),			\
	.output = STRUCT_TO_CHAR_PTR(core_reloc_bitfields_output)	\
		__VA_ARGS__,						\
	.output_len = sizeof(struct core_reloc_bitfields_output),	\
	.raw_tp_name = "sys_enter",					\
	.prog_name = "test_core_bitfields",				\
}, {									\
	BITFIELDS_CASE_COMMON("test_core_reloc_bitfields_direct.bpf.o",	\
			      "direct:", name),				\
	.input = STRUCT_TO_CHAR_PTR(core_reloc_##name) __VA_ARGS__,	\
	.input_len = sizeof(struct core_reloc_##name),			\
	.output = STRUCT_TO_CHAR_PTR(core_reloc_bitfields_output)	\
		__VA_ARGS__,						\
	.output_len = sizeof(struct core_reloc_bitfields_output),	\
	.prog_name = "test_core_bitfields_direct",			\
}


#define BITFIELDS_ERR_CASE(name) {					\
	BITFIELDS_CASE_COMMON("test_core_reloc_bitfields_probed.bpf.o",	\
			      "probed:", name),				\
	.fails = true,							\
	.run_btfgen_fails = true,					\
	.raw_tp_name = "sys_enter",					\
	.prog_name = "test_core_bitfields",				\
}, {									\
	BITFIELDS_CASE_COMMON("test_core_reloc_bitfields_direct.bpf.o",	\
			      "direct:", name),				\
	.fails = true,							\
	.run_btfgen_fails = true,							\
	.prog_name = "test_core_bitfields_direct",			\
}

#define SIZE_CASE_COMMON(name)						\
	.case_name = #name,						\
	.bpf_obj_file = "test_core_reloc_size.bpf.o",			\
	.btf_src_file = "btf__core_reloc_" #name ".bpf.o",		\
	.raw_tp_name = "sys_enter",					\
	.prog_name = "test_core_size"

#define SIZE_OUTPUT_DATA(type)						\
	STRUCT_TO_CHAR_PTR(core_reloc_size_output) {			\
		.int_sz = sizeof(((type *)0)->int_field),		\
		.int_off = offsetof(type, int_field),			\
		.struct_sz = sizeof(((type *)0)->struct_field),		\
		.struct_off = offsetof(type, struct_field),		\
		.union_sz = sizeof(((type *)0)->union_field),		\
		.union_off = offsetof(type, union_field),		\
		.arr_sz = sizeof(((type *)0)->arr_field),		\
		.arr_off = offsetof(type, arr_field),			\
		.arr_elem_sz = sizeof(((type *)0)->arr_field[1]),	\
		.arr_elem_off = offsetof(type, arr_field[1]),		\
		.ptr_sz = 8, /* always 8-byte pointer for BPF */	\
		.ptr_off = offsetof(type, ptr_field),			\
		.enum_sz = sizeof(((type *)0)->enum_field),		\
		.enum_off = offsetof(type, enum_field),			\
		.float_sz = sizeof(((type *)0)->float_field),		\
		.float_off = offsetof(type, float_field),		\
	}

#define SIZE_CASE(name) {						\
	SIZE_CASE_COMMON(name),						\
	.input_len = 0,							\
	.output = SIZE_OUTPUT_DATA(struct core_reloc_##name),		\
	.output_len = sizeof(struct core_reloc_size_output),		\
}

#define SIZE_ERR_CASE(name) {						\
	SIZE_CASE_COMMON(name),						\
	.fails = true,							\
	.run_btfgen_fails = true,					\
}

#define TYPE_BASED_CASE_COMMON(name)					\
	.case_name = #name,						\
	.bpf_obj_file = "test_core_reloc_type_based.bpf.o",		\
	.btf_src_file = "btf__core_reloc_" #name ".bpf.o",		\
	.raw_tp_name = "sys_enter",					\
	.prog_name = "test_core_type_based"

#define TYPE_BASED_CASE(name, ...) {					\
	TYPE_BASED_CASE_COMMON(name),					\
	.output = STRUCT_TO_CHAR_PTR(core_reloc_type_based_output)	\
			__VA_ARGS__,					\
	.output_len = sizeof(struct core_reloc_type_based_output),	\
}

#define TYPE_BASED_ERR_CASE(name) {					\
	TYPE_BASED_CASE_COMMON(name),					\
	.fails = true,							\
}

#define TYPE_ID_CASE_COMMON(name)					\
	.case_name = #name,						\
	.bpf_obj_file = "test_core_reloc_type_id.bpf.o",		\
	.btf_src_file = "btf__core_reloc_" #name ".bpf.o",		\
	.raw_tp_name = "sys_enter",					\
	.prog_name = "test_core_type_id"

#define TYPE_ID_CASE(name, setup_fn) {					\
	TYPE_ID_CASE_COMMON(name),					\
	.output = STRUCT_TO_CHAR_PTR(core_reloc_type_id_output) {},	\
	.output_len = sizeof(struct core_reloc_type_id_output),		\
	.setup = setup_fn,						\
}

#define TYPE_ID_ERR_CASE(name) {					\
	TYPE_ID_CASE_COMMON(name),					\
	.fails = true,							\
}

#define ENUMVAL_CASE_COMMON(name)					\
	.case_name = #name,						\
	.bpf_obj_file = "test_core_reloc_enumval.bpf.o",		\
	.btf_src_file = "btf__core_reloc_" #name ".bpf.o",		\
	.raw_tp_name = "sys_enter",					\
	.prog_name = "test_core_enumval"

#define ENUMVAL_CASE(name, ...) {					\
	ENUMVAL_CASE_COMMON(name),					\
	.output = STRUCT_TO_CHAR_PTR(core_reloc_enumval_output)		\
			__VA_ARGS__,					\
	.output_len = sizeof(struct core_reloc_enumval_output),		\
}

#define ENUMVAL_ERR_CASE(name) {					\
	ENUMVAL_CASE_COMMON(name),					\
	.fails = true,							\
}

#define ENUM64VAL_CASE_COMMON(name)					\
	.case_name = #name,						\
	.bpf_obj_file = "test_core_reloc_enum64val.bpf.o",		\
	.btf_src_file = "btf__core_reloc_" #name ".bpf.o",		\
	.raw_tp_name = "sys_enter",					\
	.prog_name = "test_core_enum64val"

#define ENUM64VAL_CASE(name, ...) {					\
	ENUM64VAL_CASE_COMMON(name),					\
	.output = STRUCT_TO_CHAR_PTR(core_reloc_enum64val_output)	\
			__VA_ARGS__,					\
	.output_len = sizeof(struct core_reloc_enum64val_output),	\
}

#define ENUM64VAL_ERR_CASE(name) {					\
	ENUM64VAL_CASE_COMMON(name),					\
	.fails = true,							\
}

struct core_reloc_test_case;

typedef int (*setup_test_fn)(struct core_reloc_test_case *test);
typedef int (*trigger_test_fn)(const struct core_reloc_test_case *test);

struct core_reloc_test_case {
	const char *case_name;
	const char *bpf_obj_file;
	const char *btf_src_file;
	const char *input;
	int input_len;
	const char *output;
	int output_len;
	bool fails;
	bool run_btfgen_fails;
	bool needs_testmod;
	bool relaxed_core_relocs;
	const char *prog_name;
	const char *raw_tp_name;
	setup_test_fn setup;
	trigger_test_fn trigger;
};

static int find_btf_type(const struct btf *btf, const char *name, __u32 kind)
{
	int id;

	id = btf__find_by_name_kind(btf, name, kind);
	if (CHECK(id <= 0, "find_type_id", "failed to find '%s', kind %d: %d\n", name, kind, id))
		return -1;

	return id;
}

static int setup_type_id_case_local(struct core_reloc_test_case *test)
{
	struct core_reloc_type_id_output *exp = (void *)test->output;
	struct btf *local_btf = btf__parse(test->bpf_obj_file, NULL);
	struct btf *targ_btf = btf__parse(test->btf_src_file, NULL);
	const struct btf_type *t;
	const char *name;
	int i;

	if (!ASSERT_OK_PTR(local_btf, "local_btf") || !ASSERT_OK_PTR(targ_btf, "targ_btf")) {
		btf__free(local_btf);
		btf__free(targ_btf);
		return -EINVAL;
	}

	exp->local_anon_struct = -1;
	exp->local_anon_union = -1;
	exp->local_anon_enum = -1;
	exp->local_anon_func_proto_ptr = -1;
	exp->local_anon_void_ptr = -1;
	exp->local_anon_arr = -1;

	for (i = 1; i < btf__type_cnt(local_btf); i++)
	{
		t = btf__type_by_id(local_btf, i);
		/* we are interested only in anonymous types */
		if (t->name_off)
			continue;

		if (btf_is_struct(t) && btf_vlen(t) &&
		    (name = btf__name_by_offset(local_btf, btf_members(t)[0].name_off)) &&
		    strcmp(name, "marker_field") == 0) {
			exp->local_anon_struct = i;
		} else if (btf_is_union(t) && btf_vlen(t) &&
			 (name = btf__name_by_offset(local_btf, btf_members(t)[0].name_off)) &&
			 strcmp(name, "marker_field") == 0) {
			exp->local_anon_union = i;
		} else if (btf_is_enum(t) && btf_vlen(t) &&
			 (name = btf__name_by_offset(local_btf, btf_enum(t)[0].name_off)) &&
			 strcmp(name, "MARKER_ENUM_VAL") == 0) {
			exp->local_anon_enum = i;
		} else if (btf_is_ptr(t) && (t = btf__type_by_id(local_btf, t->type))) {
			if (btf_is_func_proto(t) && (t = btf__type_by_id(local_btf, t->type)) &&
			    btf_is_int(t) && (name = btf__name_by_offset(local_btf, t->name_off)) &&
			    strcmp(name, "_Bool") == 0) {
				/* ptr -> func_proto -> _Bool */
				exp->local_anon_func_proto_ptr = i;
			} else if (btf_is_void(t)) {
				/* ptr -> void */
				exp->local_anon_void_ptr = i;
			}
		} else if (btf_is_array(t) && (t = btf__type_by_id(local_btf, btf_array(t)->type)) &&
			   btf_is_int(t) && (name = btf__name_by_offset(local_btf, t->name_off)) &&
			   strcmp(name, "_Bool") == 0) {
			/* _Bool[] */
			exp->local_anon_arr = i;
		}
	}

	exp->local_struct = find_btf_type(local_btf, "a_struct", BTF_KIND_STRUCT);
	exp->local_union = find_btf_type(local_btf, "a_union", BTF_KIND_UNION);
	exp->local_enum = find_btf_type(local_btf, "an_enum", BTF_KIND_ENUM);
	exp->local_int = find_btf_type(local_btf, "int", BTF_KIND_INT);
	exp->local_struct_typedef = find_btf_type(local_btf, "named_struct_typedef", BTF_KIND_TYPEDEF);
	exp->local_func_proto_typedef = find_btf_type(local_btf, "func_proto_typedef", BTF_KIND_TYPEDEF);
	exp->local_arr_typedef = find_btf_type(local_btf, "arr_typedef", BTF_KIND_TYPEDEF);

	btf__free(local_btf);
	btf__free(targ_btf);
	return 0;
}

static int setup_type_id_case_success(struct core_reloc_test_case *test) {
	struct core_reloc_type_id_output *exp = (void *)test->output;
	struct btf *targ_btf;
	int err;

	err = setup_type_id_case_local(test);
	if (err)
		return err;

	targ_btf = btf__parse(test->btf_src_file, NULL);

	exp->targ_struct = find_btf_type(targ_btf, "a_struct", BTF_KIND_STRUCT);
	exp->targ_union = find_btf_type(targ_btf, "a_union", BTF_KIND_UNION);
	exp->targ_enum = find_btf_type(targ_btf, "an_enum", BTF_KIND_ENUM);
	exp->targ_int = find_btf_type(targ_btf, "int", BTF_KIND_INT);
	exp->targ_struct_typedef = find_btf_type(targ_btf, "named_struct_typedef", BTF_KIND_TYPEDEF);
	exp->targ_func_proto_typedef = find_btf_type(targ_btf, "func_proto_typedef", BTF_KIND_TYPEDEF);
	exp->targ_arr_typedef = find_btf_type(targ_btf, "arr_typedef", BTF_KIND_TYPEDEF);

	btf__free(targ_btf);
	return 0;
}

static int setup_type_id_case_failure(struct core_reloc_test_case *test)
{
	struct core_reloc_type_id_output *exp = (void *)test->output;
	int err;

	err = setup_type_id_case_local(test);
	if (err)
		return err;

	exp->targ_struct = 0;
	exp->targ_union = 0;
	exp->targ_enum = 0;
	exp->targ_int = 0;
	exp->targ_struct_typedef = 0;
	exp->targ_func_proto_typedef = 0;
	exp->targ_arr_typedef = 0;

	return 0;
}

static int __trigger_module_test_read(const struct core_reloc_test_case *test)
{
	struct core_reloc_module_output *exp = (void *)test->output;

	trigger_module_test_read(exp->len);
	return 0;
}

static const struct core_reloc_test_case test_cases[] = {
	/* validate we can find kernel image and use its BTF for relocs */
	{
		.case_name = "kernel",
		.bpf_obj_file = "test_core_reloc_kernel.bpf.o",
		.btf_src_file = NULL, /* load from /lib/modules/$(uname -r) */
		.input = "",
		.input_len = 0,
		.output = STRUCT_TO_CHAR_PTR(core_reloc_kernel_output) {
			.valid = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, },
			.comm = "test_progs",
			.comm_len = sizeof("test_progs"),
			.local_task_struct_matches = true,
		},
		.output_len = sizeof(struct core_reloc_kernel_output),
		.raw_tp_name = "sys_enter",
		.prog_name = "test_core_kernel",
	},

	/* validate we can find kernel module BTF types for relocs/attach */
	MODULES_CASE("module_probed", "test_core_module_probed", "bpf_testmod_test_read"),
	MODULES_CASE("module_direct", "test_core_module_direct", NULL),

	/* validate BPF program can use multiple flavors to match against
	 * single target BTF type
	 */
	FLAVORS_CASE(flavors),

	FLAVORS_ERR_CASE(flavors__err_wrong_name),

	/* various struct/enum nesting and resolution scenarios */
	NESTING_CASE(nesting),
	NESTING_CASE(nesting___anon_embed),
	NESTING_CASE(nesting___struct_union_mixup),
	NESTING_CASE(nesting___extra_nesting),
	NESTING_CASE(nesting___dup_compat_types),

	NESTING_ERR_CASE(nesting___err_missing_field),
	NESTING_ERR_CASE(nesting___err_array_field),
	NESTING_ERR_CASE(nesting___err_missing_container),
	NESTING_ERR_CASE(nesting___err_nonstruct_container),
	NESTING_ERR_CASE(nesting___err_array_container),
	NESTING_ERR_CASE(nesting___err_dup_incompat_types),
	NESTING_ERR_CASE(nesting___err_partial_match_dups),
	NESTING_ERR_CASE(nesting___err_too_deep),

	/* various array access relocation scenarios */
	ARRAYS_CASE(arrays),
	ARRAYS_CASE(arrays___diff_arr_dim),
	ARRAYS_CASE(arrays___diff_arr_val_sz),
	ARRAYS_CASE(arrays___equiv_zero_sz_arr),
	ARRAYS_CASE(arrays___fixed_arr),

	ARRAYS_ERR_CASE(arrays___err_too_small),
	ARRAYS_ERR_CASE(arrays___err_too_shallow),
	ARRAYS_ERR_CASE(arrays___err_non_array),
	ARRAYS_ERR_CASE(arrays___err_wrong_val_type),
	ARRAYS_ERR_CASE(arrays___err_bad_zero_sz_arr),

	/* enum/ptr/int handling scenarios */
	PRIMITIVES_CASE(primitives),
	PRIMITIVES_CASE(primitives___diff_enum_def),
	PRIMITIVES_CASE(primitives___diff_func_proto),
	PRIMITIVES_CASE(primitives___diff_ptr_type),

	PRIMITIVES_ERR_CASE(primitives___err_non_enum),
	PRIMITIVES_ERR_CASE(primitives___err_non_int),
	PRIMITIVES_ERR_CASE(primitives___err_non_ptr),

	/* const/volatile/restrict and typedefs scenarios */
	MODS_CASE(mods),
	MODS_CASE(mods___mod_swap),
	MODS_CASE(mods___typedefs),

	/* handling "ptr is an array" semantics */
	PTR_AS_ARR_CASE(ptr_as_arr),
	PTR_AS_ARR_CASE(ptr_as_arr___diff_sz),

	/* int signedness/sizing/bitfield handling */
	INTS_CASE(ints),
	INTS_CASE(ints___bool),
	INTS_CASE(ints___reverse_sign),

	/* validate edge cases of capturing relocations */
	{
		.case_name = "misc",
		.bpf_obj_file = "test_core_reloc_misc.bpf.o",
		.btf_src_file = "btf__core_reloc_misc.bpf.o",
		.input = (const char *)&(struct core_reloc_misc_extensible[]){
			{ .a = 1 },
			{ .a = 2 }, /* not read */
			{ .a = 3 },
		},
		.input_len = 4 * sizeof(int),
		.output = STRUCT_TO_CHAR_PTR(core_reloc_misc_output) {
			.a = 1,
			.b = 1,
			.c = 0, /* BUG in clang, should be 3 */
		},
		.output_len = sizeof(struct core_reloc_misc_output),
		.raw_tp_name = "sys_enter",
		.prog_name = "test_core_misc",
	},

	/* validate field existence checks */
	{
		FIELD_EXISTS_CASE_COMMON(existence),
		.input = STRUCT_TO_CHAR_PTR(core_reloc_existence) {
			.a = 1,
			.b = 2,
			.c = 3,
			.arr = { 4 },
			.s = { .x = 5 },
		},
		.input_len = sizeof(struct core_reloc_existence),
		.output = STRUCT_TO_CHAR_PTR(core_reloc_existence_output) {
			.a_exists = 1,
			.b_exists = 1,
			.c_exists = 1,
			.arr_exists = 1,
			.s_exists = 1,
			.a_value = 1,
			.b_value = 2,
			.c_value = 3,
			.arr_value = 4,
			.s_value = 5,
		},
		.output_len = sizeof(struct core_reloc_existence_output),
	},
	{
		FIELD_EXISTS_CASE_COMMON(existence___minimal),
		.input = STRUCT_TO_CHAR_PTR(core_reloc_existence___minimal) {
			.a = 42,
		},
		.input_len = sizeof(struct core_reloc_existence___minimal),
		.output = STRUCT_TO_CHAR_PTR(core_reloc_existence_output) {
			.a_exists = 1,
			.b_exists = 0,
			.c_exists = 0,
			.arr_exists = 0,
			.s_exists = 0,
			.a_value = 42,
			.b_value = 0xff000002u,
			.c_value = 0xff000003u,
			.arr_value = 0xff000004u,
			.s_value = 0xff000005u,
		},
		.output_len = sizeof(struct core_reloc_existence_output),
	},
	{
		FIELD_EXISTS_CASE_COMMON(existence___wrong_field_defs),
		.input = STRUCT_TO_CHAR_PTR(core_reloc_existence___wrong_field_defs) {
		},
		.input_len = sizeof(struct core_reloc_existence___wrong_field_defs),
		.output = STRUCT_TO_CHAR_PTR(core_reloc_existence_output) {
			.a_exists = 0,
			.b_exists = 0,
			.c_exists = 0,
			.arr_exists = 0,
			.s_exists = 0,
			.a_value = 0xff000001u,
			.b_value = 0xff000002u,
			.c_value = 0xff000003u,
			.arr_value = 0xff000004u,
			.s_value = 0xff000005u,
		},
		.output_len = sizeof(struct core_reloc_existence_output),
	},

	/* bitfield relocation checks */
	BITFIELDS_CASE(bitfields, {
		.ub1 = 1,
		.ub2 = 2,
		.ub7 = 96,
		.sb4 = -7,
		.sb20 = -0x76543,
		.u32 = 0x80000000,
		.s32 = -0x76543210,
	}),
	BITFIELDS_CASE(bitfields___bit_sz_change, {
		.ub1 = 6,
		.ub2 = 0xABCDE,
		.ub7 = 1,
		.sb4 = -1,
		.sb20 = -0x17654321,
		.u32 = 0xBEEF,
		.s32 = -0x3FEDCBA987654321LL,
	}),
	BITFIELDS_CASE(bitfields___bitfield_vs_int, {
		.ub1 = 0xFEDCBA9876543210LL,
		.ub2 = 0xA6,
		.ub7 = -0x7EDCBA987654321LL,
		.sb4 = -0x6123456789ABCDELL,
		.sb20 = 0xD00DLL,
		.u32 = -0x76543,
		.s32 = 0x0ADEADBEEFBADB0BLL,
	}),
	BITFIELDS_CASE(bitfields___just_big_enough, {
		.ub1 = 0xFLL,
		.ub2 = 0x0812345678FEDCBALL,
	}),
	BITFIELDS_ERR_CASE(bitfields___err_too_big_bitfield),

	/* field size and offset relocation checks */
	SIZE_CASE(size),
	SIZE_CASE(size___diff_sz),
	SIZE_CASE(size___diff_offs),
	SIZE_ERR_CASE(size___err_ambiguous),

	/* validate type existence, match, and size relocations */
	TYPE_BASED_CASE(type_based, {
		.struct_exists = 1,
		.complex_struct_exists = 1,
		.union_exists = 1,
		.enum_exists = 1,
		.typedef_named_struct_exists = 1,
		.typedef_anon_struct_exists = 1,
		.typedef_struct_ptr_exists = 1,
		.typedef_int_exists = 1,
		.typedef_enum_exists = 1,
		.typedef_void_ptr_exists = 1,
		.typedef_restrict_ptr_exists = 1,
		.typedef_func_proto_exists = 1,
		.typedef_arr_exists = 1,

		.struct_matches = 1,
		.complex_struct_matches = 1,
		.union_matches = 1,
		.enum_matches = 1,
		.typedef_named_struct_matches = 1,
		.typedef_anon_struct_matches = 1,
		.typedef_struct_ptr_matches = 1,
		.typedef_int_matches = 1,
		.typedef_enum_matches = 1,
		.typedef_void_ptr_matches = 1,
		.typedef_restrict_ptr_matches = 1,
		.typedef_func_proto_matches = 1,
		.typedef_arr_matches = 1,

		.struct_sz = sizeof(struct a_struct),
		.union_sz = sizeof(union a_union),
		.enum_sz = sizeof(enum an_enum),
		.typedef_named_struct_sz = sizeof(named_struct_typedef),
		.typedef_anon_struct_sz = sizeof(anon_struct_typedef),
		.typedef_struct_ptr_sz = sizeof(struct_ptr_typedef),
		.typedef_int_sz = sizeof(int_typedef),
		.typedef_enum_sz = sizeof(enum_typedef),
		.typedef_void_ptr_sz = sizeof(void_ptr_typedef),
		.typedef_func_proto_sz = sizeof(func_proto_typedef),
		.typedef_arr_sz = sizeof(arr_typedef),
	}),
	TYPE_BASED_CASE(type_based___all_missing, {
		/* all zeros */
	}),
	TYPE_BASED_CASE(type_based___diff, {
		.struct_exists = 1,
		.complex_struct_exists = 1,
		.union_exists = 1,
		.enum_exists = 1,
		.typedef_named_struct_exists = 1,
		.typedef_anon_struct_exists = 1,
		.typedef_struct_ptr_exists = 1,
		.typedef_int_exists = 1,
		.typedef_enum_exists = 1,
		.typedef_void_ptr_exists = 1,
		.typedef_func_proto_exists = 1,
		.typedef_arr_exists = 1,

		.struct_matches = 1,
		.complex_struct_matches = 1,
		.union_matches = 1,
		.enum_matches = 1,
		.typedef_named_struct_matches = 1,
		.typedef_anon_struct_matches = 1,
		.typedef_struct_ptr_matches = 1,
		.typedef_int_matches = 0,
		.typedef_enum_matches = 1,
		.typedef_void_ptr_matches = 1,
		.typedef_func_proto_matches = 0,
		.typedef_arr_matches = 0,

		.struct_sz = sizeof(struct a_struct___diff),
		.union_sz = sizeof(union a_union___diff),
		.enum_sz = sizeof(enum an_enum___diff),
		.typedef_named_struct_sz = sizeof(named_struct_typedef___diff),
		.typedef_anon_struct_sz = sizeof(anon_struct_typedef___diff),
		.typedef_struct_ptr_sz = sizeof(struct_ptr_typedef___diff),
		.typedef_int_sz = sizeof(int_typedef___diff),
		.typedef_enum_sz = sizeof(enum_typedef___diff),
		.typedef_void_ptr_sz = sizeof(void_ptr_typedef___diff),
		.typedef_func_proto_sz = sizeof(func_proto_typedef___diff),
		.typedef_arr_sz = sizeof(arr_typedef___diff),
	}),
	TYPE_BASED_CASE(type_based___diff_sz, {
		.struct_exists = 1,
		.union_exists = 1,
		.enum_exists = 1,
		.typedef_named_struct_exists = 1,
		.typedef_anon_struct_exists = 1,
		.typedef_struct_ptr_exists = 1,
		.typedef_int_exists = 1,
		.typedef_enum_exists = 1,
		.typedef_void_ptr_exists = 1,
		.typedef_func_proto_exists = 1,
		.typedef_arr_exists = 1,

		.struct_matches = 0,
		.union_matches = 0,
		.enum_matches = 0,
		.typedef_named_struct_matches = 0,
		.typedef_anon_struct_matches = 0,
		.typedef_struct_ptr_matches = 1,
		.typedef_int_matches = 0,
		.typedef_enum_matches = 0,
		.typedef_void_ptr_matches = 1,
		.typedef_func_proto_matches = 0,
		.typedef_arr_matches = 0,

		.struct_sz = sizeof(struct a_struct___diff_sz),
		.union_sz = sizeof(union a_union___diff_sz),
		.enum_sz = sizeof(enum an_enum___diff_sz),
		.typedef_named_struct_sz = sizeof(named_struct_typedef___diff_sz),
		.typedef_anon_struct_sz = sizeof(anon_struct_typedef___diff_sz),
		.typedef_struct_ptr_sz = sizeof(struct_ptr_typedef___diff_sz),
		.typedef_int_sz = sizeof(int_typedef___diff_sz),
		.typedef_enum_sz = sizeof(enum_typedef___diff_sz),
		.typedef_void_ptr_sz = sizeof(void_ptr_typedef___diff_sz),
		.typedef_func_proto_sz = sizeof(func_proto_typedef___diff_sz),
		.typedef_arr_sz = sizeof(arr_typedef___diff_sz),
	}),
	TYPE_BASED_CASE(type_based___incompat, {
		.enum_exists = 1,
		.enum_matches = 1,
		.enum_sz = sizeof(enum an_enum),
	}),
	TYPE_BASED_CASE(type_based___fn_wrong_args, {
		.struct_exists = 1,
		.struct_matches = 1,
		.struct_sz = sizeof(struct a_struct),
	}),

	/* BTF_TYPE_ID_LOCAL/BTF_TYPE_ID_TARGET tests */
	TYPE_ID_CASE(type_id, setup_type_id_case_success),
	TYPE_ID_CASE(type_id___missing_targets, setup_type_id_case_failure),

	/* Enumerator value existence and value relocations */
	ENUMVAL_CASE(enumval, {
		.named_val1_exists = true,
		.named_val2_exists = true,
		.named_val3_exists = true,
		.anon_val1_exists = true,
		.anon_val2_exists = true,
		.anon_val3_exists = true,
		.named_val1 = 1,
		.named_val2 = 2,
		.anon_val1 = 0x10,
		.anon_val2 = 0x20,
	}),
	ENUMVAL_CASE(enumval___diff, {
		.named_val1_exists = true,
		.named_val2_exists = true,
		.named_val3_exists = true,
		.anon_val1_exists = true,
		.anon_val2_exists = true,
		.anon_val3_exists = true,
		.named_val1 = 101,
		.named_val2 = 202,
		.anon_val1 = 0x11,
		.anon_val2 = 0x22,
	}),
	ENUMVAL_CASE(enumval___val3_missing, {
		.named_val1_exists = true,
		.named_val2_exists = true,
		.named_val3_exists = false,
		.anon_val1_exists = true,
		.anon_val2_exists = true,
		.anon_val3_exists = false,
		.named_val1 = 111,
		.named_val2 = 222,
		.anon_val1 = 0x111,
		.anon_val2 = 0x222,
	}),
	ENUMVAL_ERR_CASE(enumval___err_missing),

	/* 64bit enumerator value existence and value relocations */
	ENUM64VAL_CASE(enum64val, {
		.unsigned_val1_exists = true,
		.unsigned_val2_exists = true,
		.unsigned_val3_exists = true,
		.signed_val1_exists = true,
		.signed_val2_exists = true,
		.signed_val3_exists = true,
		.unsigned_val1 = 0x1ffffffffULL,
		.unsigned_val2 = 0x2,
		.signed_val1 = 0x1ffffffffLL,
		.signed_val2 = -2,
	}),
	ENUM64VAL_CASE(enum64val___diff, {
		.unsigned_val1_exists = true,
		.unsigned_val2_exists = true,
		.unsigned_val3_exists = true,
		.signed_val1_exists = true,
		.signed_val2_exists = true,
		.signed_val3_exists = true,
		.unsigned_val1 = 0x101ffffffffULL,
		.unsigned_val2 = 0x202ffffffffULL,
		.signed_val1 = -101,
		.signed_val2 = -202,
	}),
	ENUM64VAL_CASE(enum64val___val3_missing, {
		.unsigned_val1_exists = true,
		.unsigned_val2_exists = true,
		.unsigned_val3_exists = false,
		.signed_val1_exists = true,
		.signed_val2_exists = true,
		.signed_val3_exists = false,
		.unsigned_val1 = 0x111ffffffffULL,
		.unsigned_val2 = 0x222,
		.signed_val1 = 0x111ffffffffLL,
		.signed_val2 = -222,
	}),
	ENUM64VAL_ERR_CASE(enum64val___err_missing),
};

struct data {
	char in[256];
	char out[256];
	bool skip;
	uint64_t my_pid_tgid;
};

static size_t roundup_page(size_t sz)
{
	long page_size = sysconf(_SC_PAGE_SIZE);
	return (sz + page_size - 1) / page_size * page_size;
}

static int run_btfgen(const char *src_btf, const char *dst_btf, const char *objpath)
{
	char command[4096];
	int n;

	n = snprintf(command, sizeof(command),
		     "./bpftool gen min_core_btf %s %s %s",
		     src_btf, dst_btf, objpath);
	if (n < 0 || n >= sizeof(command))
		return -1;

	return system(command);
}

static void run_core_reloc_tests(bool use_btfgen)
{
	const size_t mmap_sz = roundup_page(sizeof(struct data));
	DECLARE_LIBBPF_OPTS(bpf_object_open_opts, open_opts);
	struct core_reloc_test_case *test_case, test_case_copy;
	const char *tp_name, *probe_name;
	int err, i, equal, fd;
	struct bpf_link *link = NULL;
	struct bpf_map *data_map;
	struct bpf_program *prog;
	struct bpf_object *obj;
	uint64_t my_pid_tgid;
	struct data *data;
	void *mmap_data = NULL;

	my_pid_tgid = getpid() | ((uint64_t)syscall(SYS_gettid) << 32);

	for (i = 0; i < ARRAY_SIZE(test_cases); i++) {
		char btf_file[] = "/tmp/core_reloc.btf.XXXXXX";

		test_case_copy = test_cases[i];
		test_case = &test_case_copy;

		if (!test__start_subtest(test_case->case_name))
			continue;

		if (test_case->needs_testmod && !env.has_testmod) {
			test__skip();
			continue;
		}

		/* generate a "minimal" BTF file and use it as source */
		if (use_btfgen) {

			if (!test_case->btf_src_file || test_case->run_btfgen_fails) {
				test__skip();
				continue;
			}

			fd = mkstemp(btf_file);
			if (!ASSERT_GE(fd, 0, "btf_tmp"))
				continue;
			close(fd); /* we only need the path */
			err = run_btfgen(test_case->btf_src_file, btf_file,
					 test_case->bpf_obj_file);
			if (!ASSERT_OK(err, "run_btfgen"))
				continue;

			test_case->btf_src_file = btf_file;
		}

		if (test_case->setup) {
			err = test_case->setup(test_case);
			if (CHECK(err, "test_setup", "test #%d setup failed: %d\n", i, err))
				continue;
		}

		if (test_case->btf_src_file) {
			err = access(test_case->btf_src_file, R_OK);
			if (!ASSERT_OK(err, "btf_src_file"))
				continue;
		}

		open_opts.btf_custom_path = test_case->btf_src_file;
		obj = bpf_object__open_file(test_case->bpf_obj_file, &open_opts);
		if (!ASSERT_OK_PTR(obj, "obj_open"))
			goto cleanup;

		probe_name = test_case->prog_name;
		tp_name = test_case->raw_tp_name; /* NULL for tp_btf */
		prog = bpf_object__find_program_by_name(obj, probe_name);
		if (CHECK(!prog, "find_probe",
			  "prog '%s' not found\n", probe_name))
			goto cleanup;

		err = bpf_object__load(obj);
		if (err) {
			if (!test_case->fails)
				ASSERT_OK(err, "obj_load");
			goto cleanup;
		}

		data_map = bpf_object__find_map_by_name(obj, ".bss");
		if (CHECK(!data_map, "find_data_map", "data map not found\n"))
			goto cleanup;

		mmap_data = mmap(NULL, mmap_sz, PROT_READ | PROT_WRITE,
				 MAP_SHARED, bpf_map__fd(data_map), 0);
		if (CHECK(mmap_data == MAP_FAILED, "mmap",
			  ".bss mmap failed: %d", errno)) {
			mmap_data = NULL;
			goto cleanup;
		}
		data = mmap_data;

		memset(mmap_data, 0, sizeof(*data));
		if (test_case->input_len)
			memcpy(data->in, test_case->input, test_case->input_len);
		data->my_pid_tgid = my_pid_tgid;

		link = bpf_program__attach_raw_tracepoint(prog, tp_name);
		if (!ASSERT_OK_PTR(link, "attach_raw_tp"))
			goto cleanup;

		/* trigger test run */
		if (test_case->trigger) {
			if (!ASSERT_OK(test_case->trigger(test_case), "test_trigger"))
				goto cleanup;
		} else {
			usleep(1);
		}

		if (data->skip) {
			test__skip();
			goto cleanup;
		}

		if (!ASSERT_FALSE(test_case->fails, "obj_load_should_fail"))
			goto cleanup;

		equal = memcmp(data->out, test_case->output,
			       test_case->output_len) == 0;
		if (CHECK(!equal, "check_result",
			  "input/output data don't match\n")) {
			int j;

			for (j = 0; j < test_case->input_len; j++) {
				printf("input byte #%d: 0x%02hhx\n",
				       j, test_case->input[j]);
			}
			for (j = 0; j < test_case->output_len; j++) {
				printf("output byte #%d: EXP 0x%02hhx GOT 0x%02hhx\n",
				       j, test_case->output[j], data->out[j]);
			}
			goto cleanup;
		}

cleanup:
		if (mmap_data) {
			CHECK_FAIL(munmap(mmap_data, mmap_sz));
			mmap_data = NULL;
		}
		if (use_btfgen)
			remove(test_case->btf_src_file);
		bpf_link__destroy(link);
		link = NULL;
		bpf_object__close(obj);
	}
}

void test_core_reloc(void)
{
	run_core_reloc_tests(false);
}

void test_core_reloc_btfgen(void)
{
	run_core_reloc_tests(true);
}
