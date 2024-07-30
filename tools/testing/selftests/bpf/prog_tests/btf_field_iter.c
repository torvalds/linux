// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024, Oracle and/or its affiliates. */

#include <test_progs.h>
#include <bpf/btf.h>
#include "btf_helpers.h"
#include "bpf/libbpf_internal.h"

struct field_data {
	__u32 ids[5];
	const char *strs[5];
} fields[] = {
	{ .ids = {},		.strs = {} },
	{ .ids = {},		.strs = { "int" } },
	{ .ids = {},		.strs = { "int64" } },
	{ .ids = { 1 },		.strs = { "" } },
	{ .ids = { 2, 1 },	.strs = { "" } },
	{ .ids = { 3, 1 },	.strs = { "s1", "f1", "f2" } },
	{ .ids = { 1, 5 },	.strs = { "u1", "f1", "f2" } },
	{ .ids = {},		.strs = { "e1", "v1", "v2" } },
	{ .ids = {},		.strs = { "fw1" } },
	{ .ids = { 1 },		.strs = { "t" } },
	{ .ids = { 2 },		.strs = { "" } },
	{ .ids = { 1 },		.strs = { "" } },
	{ .ids = { 3 },		.strs = { "" } },
	{ .ids = { 1, 1, 3 },	.strs = { "", "p1", "p2" } },
	{ .ids = { 13 },	.strs = { "func" } },
	{ .ids = { 1 },		.strs = { "var1" } },
	{ .ids = { 3 },		.strs = { "var2" } },
	{ .ids = {},		.strs = { "float" } },
	{ .ids = { 11 },	.strs = { "decltag" } },
	{ .ids = { 6 },		.strs = { "typetag" } },
	{ .ids = {},		.strs = { "e64", "eval1", "eval2", "eval3" } },
	{ .ids = { 15, 16 },	.strs = { "datasec1" } }

};

/* Fabricate BTF with various types and check BTF field iteration finds types,
 * strings expected.
 */
void test_btf_field_iter(void)
{
	struct btf *btf = NULL;
	int id;

	btf = btf__new_empty();
	if (!ASSERT_OK_PTR(btf, "empty_btf"))
		return;

	btf__add_int(btf, "int", 4, BTF_INT_SIGNED);	/* [1] int */
	btf__add_int(btf, "int64", 8, BTF_INT_SIGNED);	/* [2] int64 */
	btf__add_ptr(btf, 1);				/* [3] int * */
	btf__add_array(btf, 1, 2, 3);			/* [4] int64[3] */
	btf__add_struct(btf, "s1", 12);			/* [5] struct s1 { */
	btf__add_field(btf, "f1", 3, 0, 0);		/*      int *f1; */
	btf__add_field(btf, "f2", 1, 0, 0);		/*	int f2; */
							/* } */
	btf__add_union(btf, "u1", 12);			/* [6] union u1 { */
	btf__add_field(btf, "f1", 1, 0, 0);		/*	int f1; */
	btf__add_field(btf, "f2", 5, 0, 0);		/*	struct s1 f2; */
							/* } */
	btf__add_enum(btf, "e1", 4);			/* [7] enum e1 { */
	btf__add_enum_value(btf, "v1", 1);		/*	v1 = 1; */
	btf__add_enum_value(btf, "v2", 2);		/*	v2 = 2; */
							/* } */

	btf__add_fwd(btf, "fw1", BTF_FWD_STRUCT);	/* [8] struct fw1; */
	btf__add_typedef(btf, "t", 1);			/* [9] typedef int t; */
	btf__add_volatile(btf, 2);			/* [10] volatile int64; */
	btf__add_const(btf, 1);				/* [11] const int; */
	btf__add_restrict(btf, 3);			/* [12] restrict int *; */
	btf__add_func_proto(btf, 1);			/* [13] int (*)(int p1, int *p2); */
	btf__add_func_param(btf, "p1", 1);
	btf__add_func_param(btf, "p2", 3);

	btf__add_func(btf, "func", BTF_FUNC_GLOBAL, 13);/* [14] int func(int p1, int *p2); */
	btf__add_var(btf, "var1", BTF_VAR_STATIC, 1);	/* [15] static int var1; */
	btf__add_var(btf, "var2", BTF_VAR_STATIC, 3);	/* [16] static int *var2; */
	btf__add_float(btf, "float", 4);		/* [17] float; */
	btf__add_decl_tag(btf, "decltag", 11, -1);	/* [18] decltag const int; */
	btf__add_type_tag(btf, "typetag", 6);		/* [19] typetag union u1; */
	btf__add_enum64(btf, "e64", 8, true);		/* [20] enum { */
	btf__add_enum64_value(btf, "eval1", 1000);	/*	 eval1 = 1000, */
	btf__add_enum64_value(btf, "eval2", 2000);	/*	 eval2 = 2000, */
	btf__add_enum64_value(btf, "eval3", 3000);	/*	 eval3 = 3000 */
							/* } */
	btf__add_datasec(btf, "datasec1", 12);		/* [21] datasec datasec1 */
	btf__add_datasec_var_info(btf, 15, 0, 4);
	btf__add_datasec_var_info(btf, 16, 4, 8);

	VALIDATE_RAW_BTF(
		btf,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[2] INT 'int64' size=8 bits_offset=0 nr_bits=64 encoding=SIGNED",
		"[3] PTR '(anon)' type_id=1",
		"[4] ARRAY '(anon)' type_id=2 index_type_id=1 nr_elems=3",
		"[5] STRUCT 's1' size=12 vlen=2\n"
		"\t'f1' type_id=3 bits_offset=0\n"
		"\t'f2' type_id=1 bits_offset=0",
		"[6] UNION 'u1' size=12 vlen=2\n"
		"\t'f1' type_id=1 bits_offset=0\n"
		"\t'f2' type_id=5 bits_offset=0",
		"[7] ENUM 'e1' encoding=UNSIGNED size=4 vlen=2\n"
		"\t'v1' val=1\n"
		"\t'v2' val=2",
		"[8] FWD 'fw1' fwd_kind=struct",
		"[9] TYPEDEF 't' type_id=1",
		"[10] VOLATILE '(anon)' type_id=2",
		"[11] CONST '(anon)' type_id=1",
		"[12] RESTRICT '(anon)' type_id=3",
		"[13] FUNC_PROTO '(anon)' ret_type_id=1 vlen=2\n"
		"\t'p1' type_id=1\n"
		"\t'p2' type_id=3",
		"[14] FUNC 'func' type_id=13 linkage=global",
		"[15] VAR 'var1' type_id=1, linkage=static",
		"[16] VAR 'var2' type_id=3, linkage=static",
		"[17] FLOAT 'float' size=4",
		"[18] DECL_TAG 'decltag' type_id=11 component_idx=-1",
		"[19] TYPE_TAG 'typetag' type_id=6",
		"[20] ENUM64 'e64' encoding=SIGNED size=8 vlen=3\n"
		"\t'eval1' val=1000\n"
		"\t'eval2' val=2000\n"
		"\t'eval3' val=3000",
		"[21] DATASEC 'datasec1' size=12 vlen=2\n"
		"\ttype_id=15 offset=0 size=4\n"
		"\ttype_id=16 offset=4 size=8");

	for (id = 1; id < btf__type_cnt(btf); id++) {
		struct btf_type *t = btf_type_by_id(btf, id);
		struct btf_field_iter it_strs, it_ids;
		int str_idx = 0, id_idx = 0;
		__u32 *next_str, *next_id;

		if (!ASSERT_OK_PTR(t, "btf_type_by_id"))
			break;
		if (!ASSERT_OK(btf_field_iter_init(&it_strs, t, BTF_FIELD_ITER_STRS),
			       "iter_init_strs"))
			break;
		if (!ASSERT_OK(btf_field_iter_init(&it_ids, t, BTF_FIELD_ITER_IDS),
			       "iter_init_ids"))
			break;
		while ((next_str = btf_field_iter_next(&it_strs))) {
			const char *str = btf__str_by_offset(btf, *next_str);

			if (!ASSERT_OK(strcmp(fields[id].strs[str_idx], str), "field_str_match"))
				break;
			str_idx++;
		}
		/* ensure no more strings are expected */
		ASSERT_EQ(fields[id].strs[str_idx], NULL, "field_str_cnt");

		while ((next_id = btf_field_iter_next(&it_ids))) {
			if (!ASSERT_EQ(*next_id, fields[id].ids[id_idx], "field_id_match"))
				break;
			id_idx++;
		}
		/* ensure no more ids are expected */
		ASSERT_EQ(fields[id].ids[id_idx], 0, "field_id_cnt");
	}
	btf__free(btf);
}
