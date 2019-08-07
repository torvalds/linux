// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "progs/core_reloc_types.h"

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

struct core_reloc_test_case {
	const char *case_name;
	const char *bpf_obj_file;
	const char *btf_src_file;
	const char *input;
	int input_len;
	const char *output;
	int output_len;
	bool fails;
};

static struct core_reloc_test_case test_cases[] = {
	/* validate we can find kernel image and use its BTF for relocs */
	{
		.case_name = "kernel",
		.bpf_obj_file = "test_core_reloc_kernel.o",
		.btf_src_file = NULL, /* load from /lib/modules/$(uname -r) */
		.input = "",
		.input_len = 0,
		.output = "\1", /* true */
		.output_len = 1,
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
};

struct data {
	char in[256];
	char out[256];
};

void test_core_reloc(void)
{
	const char *probe_name = "raw_tracepoint/sys_enter";
	struct bpf_object_load_attr load_attr = {};
	struct core_reloc_test_case *test_case;
	int err, duration = 0, i, equal;
	struct bpf_link *link = NULL;
	struct bpf_map *data_map;
	struct bpf_program *prog;
	struct bpf_object *obj;
	const int zero = 0;
	struct data data;

	for (i = 0; i < ARRAY_SIZE(test_cases); i++) {
		test_case = &test_cases[i];

		if (!test__start_subtest(test_case->case_name))
			continue;

		obj = bpf_object__open(test_case->bpf_obj_file);
		if (CHECK(IS_ERR_OR_NULL(obj), "obj_open",
			  "failed to open '%s': %ld\n",
			  test_case->bpf_obj_file, PTR_ERR(obj)))
			continue;

		prog = bpf_object__find_program_by_title(obj, probe_name);
		if (CHECK(!prog, "find_probe",
			  "prog '%s' not found\n", probe_name))
			goto cleanup;
		bpf_program__set_type(prog, BPF_PROG_TYPE_RAW_TRACEPOINT);

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

		link = bpf_program__attach_raw_tracepoint(prog, "sys_enter");
		if (CHECK(IS_ERR(link), "attach_raw_tp", "err %ld\n",
			  PTR_ERR(link)))
			goto cleanup;

		data_map = bpf_object__find_map_by_name(obj, "test_cor.bss");
		if (CHECK(!data_map, "find_data_map", "data map not found\n"))
			goto cleanup;

		memset(&data, 0, sizeof(data));
		memcpy(data.in, test_case->input, test_case->input_len);

		err = bpf_map_update_elem(bpf_map__fd(data_map),
					  &zero, &data, 0);
		if (CHECK(err, "update_data_map",
			  "failed to update .data map: %d\n", err))
			goto cleanup;

		/* trigger test run */
		usleep(1);

		err = bpf_map_lookup_elem(bpf_map__fd(data_map), &zero, &data);
		if (CHECK(err, "get_result",
			  "failed to get output data: %d\n", err))
			goto cleanup;

		equal = memcmp(data.out, test_case->output,
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
				       j, test_case->output[j], data.out[j]);
			}
			goto cleanup;
		}

cleanup:
		if (!IS_ERR_OR_NULL(link)) {
			bpf_link__destroy(link);
			link = NULL;
		}
		bpf_object__close(obj);
	}
}
