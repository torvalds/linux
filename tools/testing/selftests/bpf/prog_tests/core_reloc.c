// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "progs/core_reloc_types.h"
#include <sys/mman.h>
#include <sys/syscall.h>

#define STRUCT_TO_CHAR_PTR(struct_name) (const char *)&(struct struct_name)

#define FLAVORS_DATA(struct_name) STRUCT_TO_CHAR_PTR(struct_name) {	\
	.a = 42,							\
	.b = 0xc001,							\
	.c = 0xbeef,							\
}

#define FLAVORS_CASE_COMMON(name)					\
	.case_name = #name,						\
	.bpf_obj_file = "test_core_reloc_flavors.o",			\
	.btf_src_file = "btf__core_reloc_" #name ".o"			\

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
	.bpf_obj_file = "test_core_reloc_nesting.o",			\
	.btf_src_file = "btf__core_reloc_" #name ".o"

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
}

#define ARRAYS_DATA(struct_name) STRUCT_TO_CHAR_PTR(struct_name) {	\
	.a = { [2] = 1 },						\
	.b = { [1] = { [2] = { [3] = 2 } } },				\
	.c = { [1] = { .c =  3 } },					\
	.d = { [0] = { [0] = { .d = 4 } } },				\
}

#define ARRAYS_CASE_COMMON(name)					\
	.case_name = #name,						\
	.bpf_obj_file = "test_core_reloc_arrays.o",			\
	.btf_src_file = "btf__core_reloc_" #name ".o"

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
	.bpf_obj_file = "test_core_reloc_primitives.o",			\
	.btf_src_file = "btf__core_reloc_" #name ".o"

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
	.bpf_obj_file = "test_core_reloc_mods.o",			\
	.btf_src_file = "btf__core_reloc_" #name ".o",			\
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
}

#define PTR_AS_ARR_CASE(name) {						\
	.case_name = #name,						\
	.bpf_obj_file = "test_core_reloc_ptr_as_arr.o",			\
	.btf_src_file = "btf__core_reloc_" #name ".o",			\
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
	.bpf_obj_file = "test_core_reloc_ints.o",			\
	.btf_src_file = "btf__core_reloc_" #name ".o"

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

#define EXISTENCE_CASE_COMMON(name)					\
	.case_name = #name,						\
	.bpf_obj_file = "test_core_reloc_existence.o",			\
	.btf_src_file = "btf__core_reloc_" #name ".o",			\
	.relaxed_core_relocs = true

#define EXISTENCE_ERR_CASE(name) {					\
	EXISTENCE_CASE_COMMON(name),					\
	.fails = true,							\
}

#define BITFIELDS_CASE_COMMON(objfile, test_name_prefix,  name)		\
	.case_name = test_name_prefix#name,				\
	.bpf_obj_file = objfile,					\
	.btf_src_file = "btf__core_reloc_" #name ".o"

#define BITFIELDS_CASE(name, ...) {					\
	BITFIELDS_CASE_COMMON("test_core_reloc_bitfields_probed.o",	\
			      "direct:", name),				\
	.input = STRUCT_TO_CHAR_PTR(core_reloc_##name) __VA_ARGS__,	\
	.input_len = sizeof(struct core_reloc_##name),			\
	.output = STRUCT_TO_CHAR_PTR(core_reloc_bitfields_output)	\
		__VA_ARGS__,						\
	.output_len = sizeof(struct core_reloc_bitfields_output),	\
}, {									\
	BITFIELDS_CASE_COMMON("test_core_reloc_bitfields_direct.o",	\
			      "probed:", name),				\
	.input = STRUCT_TO_CHAR_PTR(core_reloc_##name) __VA_ARGS__,	\
	.input_len = sizeof(struct core_reloc_##name),			\
	.output = STRUCT_TO_CHAR_PTR(core_reloc_bitfields_output)	\
		__VA_ARGS__,						\
	.output_len = sizeof(struct core_reloc_bitfields_output),	\
	.direct_raw_tp = true,						\
}


#define BITFIELDS_ERR_CASE(name) {					\
	BITFIELDS_CASE_COMMON("test_core_reloc_bitfields_probed.o",	\
			      "probed:", name),				\
	.fails = true,							\
}, {									\
	BITFIELDS_CASE_COMMON("test_core_reloc_bitfields_direct.o",	\
			      "direct:", name),				\
	.direct_raw_tp = true,						\
	.fails = true,							\
}

#define SIZE_CASE_COMMON(name)						\
	.case_name = #name,						\
	.bpf_obj_file = "test_core_reloc_size.o",			\
	.btf_src_file = "btf__core_reloc_" #name ".o",			\
	.relaxed_core_relocs = true

#define SIZE_OUTPUT_DATA(type)						\
	STRUCT_TO_CHAR_PTR(core_reloc_size_output) {			\
		.int_sz = sizeof(((type *)0)->int_field),		\
		.struct_sz = sizeof(((type *)0)->struct_field),		\
		.union_sz = sizeof(((type *)0)->union_field),		\
		.arr_sz = sizeof(((type *)0)->arr_field),		\
		.arr_elem_sz = sizeof(((type *)0)->arr_field[0]),	\
		.ptr_sz = sizeof(((type *)0)->ptr_field),		\
		.enum_sz = sizeof(((type *)0)->enum_field),	\
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
}

struct core_reloc_test_case {
	const char *case_name;
	const char *bpf_obj_file;
	const char *btf_src_file;
	const char *input;
	int input_len;
	const char *output;
	int output_len;
	bool fails;
	bool relaxed_core_relocs;
	bool direct_raw_tp;
};

static struct core_reloc_test_case test_cases[] = {
	/* validate we can find kernel image and use its BTF for relocs */
	{
		.case_name = "kernel",
		.bpf_obj_file = "test_core_reloc_kernel.o",
		.btf_src_file = NULL, /* load from /lib/modules/$(uname -r) */
		.input = "",
		.input_len = 0,
		.output = STRUCT_TO_CHAR_PTR(core_reloc_kernel_output) {
			.valid = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, },
			.comm = "test_progs",
			.comm_len = sizeof("test_progs"),
		},
		.output_len = sizeof(struct core_reloc_kernel_output),
	},

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
	ARRAYS_ERR_CASE(arrays___err_wrong_val_type1),
	ARRAYS_ERR_CASE(arrays___err_wrong_val_type2),
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
		.bpf_obj_file = "test_core_reloc_misc.o",
		.btf_src_file = "btf__core_reloc_misc.o",
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
	},

	/* validate field existence checks */
	{
		EXISTENCE_CASE_COMMON(existence),
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
		EXISTENCE_CASE_COMMON(existence___minimal),
		.input = STRUCT_TO_CHAR_PTR(core_reloc_existence___minimal) {
			.a = 42,
		},
		.input_len = sizeof(struct core_reloc_existence),
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

	EXISTENCE_ERR_CASE(existence__err_int_sz),
	EXISTENCE_ERR_CASE(existence__err_int_type),
	EXISTENCE_ERR_CASE(existence__err_int_kind),
	EXISTENCE_ERR_CASE(existence__err_arr_kind),
	EXISTENCE_ERR_CASE(existence__err_arr_value_type),
	EXISTENCE_ERR_CASE(existence__err_struct_type),

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
		.s32 = -0x3FEDCBA987654321,
	}),
	BITFIELDS_CASE(bitfields___bitfield_vs_int, {
		.ub1 = 0xFEDCBA9876543210,
		.ub2 = 0xA6,
		.ub7 = -0x7EDCBA987654321,
		.sb4 = -0x6123456789ABCDE,
		.sb20 = 0xD00D,
		.u32 = -0x76543,
		.s32 = 0x0ADEADBEEFBADB0B,
	}),
	BITFIELDS_CASE(bitfields___just_big_enough, {
		.ub1 = 0xF,
		.ub2 = 0x0812345678FEDCBA,
	}),
	BITFIELDS_ERR_CASE(bitfields___err_too_big_bitfield),

	/* size relocation checks */
	SIZE_CASE(size),
	SIZE_CASE(size___diff_sz),
};

struct data {
	char in[256];
	char out[256];
	uint64_t my_pid_tgid;
};

static size_t roundup_page(size_t sz)
{
	long page_size = sysconf(_SC_PAGE_SIZE);
	return (sz + page_size - 1) / page_size * page_size;
}

void test_core_reloc(void)
{
	const size_t mmap_sz = roundup_page(sizeof(struct data));
	struct bpf_object_load_attr load_attr = {};
	struct core_reloc_test_case *test_case;
	const char *tp_name, *probe_name;
	int err, duration = 0, i, equal;
	struct bpf_link *link = NULL;
	struct bpf_map *data_map;
	struct bpf_program *prog;
	struct bpf_object *obj;
	uint64_t my_pid_tgid;
	struct data *data;
	void *mmap_data = NULL;

	my_pid_tgid = getpid() | ((uint64_t)syscall(SYS_gettid) << 32);

	for (i = 0; i < ARRAY_SIZE(test_cases); i++) {
		test_case = &test_cases[i];
		if (!test__start_subtest(test_case->case_name))
			continue;

		DECLARE_LIBBPF_OPTS(bpf_object_open_opts, opts,
			.relaxed_core_relocs = test_case->relaxed_core_relocs,
		);

		obj = bpf_object__open_file(test_case->bpf_obj_file, &opts);
		if (CHECK(IS_ERR(obj), "obj_open", "failed to open '%s': %ld\n",
			  test_case->bpf_obj_file, PTR_ERR(obj)))
			continue;

		/* for typed raw tracepoints, NULL should be specified */
		if (test_case->direct_raw_tp) {
			probe_name = "tp_btf/sys_enter";
			tp_name = NULL;
		} else {
			probe_name = "raw_tracepoint/sys_enter";
			tp_name = "sys_enter";
		}

		prog = bpf_object__find_program_by_title(obj, probe_name);
		if (CHECK(!prog, "find_probe",
			  "prog '%s' not found\n", probe_name))
			goto cleanup;

		load_attr.obj = obj;
		load_attr.log_level = 0;
		load_attr.target_btf_path = test_case->btf_src_file;
		err = bpf_object__load_xattr(&load_attr);
		if (test_case->fails) {
			CHECK(!err, "obj_load_fail",
			      "should fail to load prog '%s'\n", probe_name);
			goto cleanup;
		} else {
			if (CHECK(err, "obj_load",
				  "failed to load prog '%s': %d\n",
				  probe_name, err))
				goto cleanup;
		}

		data_map = bpf_object__find_map_by_name(obj, "test_cor.bss");
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
		memcpy(data->in, test_case->input, test_case->input_len);
		data->my_pid_tgid = my_pid_tgid;

		link = bpf_program__attach_raw_tracepoint(prog, tp_name);
		if (CHECK(IS_ERR(link), "attach_raw_tp", "err %ld\n",
			  PTR_ERR(link)))
			goto cleanup;

		/* trigger test run */
		usleep(1);

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
		if (!IS_ERR_OR_NULL(link)) {
			bpf_link__destroy(link);
			link = NULL;
		}
		bpf_object__close(obj);
	}
}
