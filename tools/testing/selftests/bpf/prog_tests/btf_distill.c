// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024, Oracle and/or its affiliates. */

#include <test_progs.h>
#include <bpf/btf.h>
#include "btf_helpers.h"

/* Fabricate base, split BTF with references to base types needed; then create
 * split BTF with distilled base BTF and ensure expectations are met:
 *  - only referenced base types from split BTF are present
 *  - struct/union/enum are represented as empty unless anonymous, when they
 *    are represented in full in split BTF
 */
static void test_distilled_base(void)
{
	struct btf *btf1 = NULL, *btf2 = NULL, *btf3 = NULL, *btf4 = NULL;

	btf1 = btf__new_empty();
	if (!ASSERT_OK_PTR(btf1, "empty_main_btf"))
		return;

	btf__add_int(btf1, "int", 4, BTF_INT_SIGNED);	/* [1] int */
	btf__add_ptr(btf1, 1);				/* [2] ptr to int */
	btf__add_struct(btf1, "s1", 8);			/* [3] struct s1 { */
	btf__add_field(btf1, "f1", 2, 0, 0);		/*      int *f1; */
							/* } */
	btf__add_struct(btf1, "", 12);			/* [4] struct { */
	btf__add_field(btf1, "f1", 1, 0, 0);		/*	int f1; */
	btf__add_field(btf1, "f2", 3, 32, 0);		/*	struct s1 f2; */
							/* } */
	btf__add_int(btf1, "unsigned int", 4, 0);	/* [5] unsigned int */
	btf__add_union(btf1, "u1", 12);			/* [6] union u1 { */
	btf__add_field(btf1, "f1", 1, 0, 0);		/*	int f1; */
	btf__add_field(btf1, "f2", 2, 0, 0);		/*	int *f2; */
							/* } */
	btf__add_union(btf1, "", 4);			/* [7] union { */
	btf__add_field(btf1, "f1", 1, 0, 0);		/*	int f1; */
							/* } */
	btf__add_enum(btf1, "e1", 4);			/* [8] enum e1 { */
	btf__add_enum_value(btf1, "v1", 1);		/*	v1 = 1; */
							/* } */
	btf__add_enum(btf1, "", 4);			/* [9] enum { */
	btf__add_enum_value(btf1, "av1", 2);		/*	av1 = 2; */
							/* } */
	btf__add_enum64(btf1, "e641", 8, true);		/* [10] enum64 { */
	btf__add_enum64_value(btf1, "v1", 1024);	/*	v1 = 1024; */
							/* } */
	btf__add_enum64(btf1, "", 8, true);		/* [11] enum64 { */
	btf__add_enum64_value(btf1, "v1", 1025);	/*	v1 = 1025; */
							/* } */
	btf__add_struct(btf1, "unneeded", 4);		/* [12] struct unneeded { */
	btf__add_field(btf1, "f1", 1, 0, 0);		/*	int f1; */
							/* } */
	btf__add_struct(btf1, "embedded", 4);		/* [13] struct embedded { */
	btf__add_field(btf1, "f1", 1, 0, 0);		/*	int f1; */
							/* } */
	btf__add_func_proto(btf1, 1);			/* [14] int (*)(int *p1); */
	btf__add_func_param(btf1, "p1", 1);

	btf__add_array(btf1, 1, 1, 3);			/* [15] int [3]; */

	btf__add_struct(btf1, "from_proto", 4);		/* [16] struct from_proto { */
	btf__add_field(btf1, "f1", 1, 0, 0);		/*	int f1; */
							/* } */
	btf__add_union(btf1, "u1", 4);			/* [17] union u1 { */
	btf__add_field(btf1, "f1", 1, 0, 0);		/*	 int f1; */
							/* } */
	VALIDATE_RAW_BTF(
		btf1,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[2] PTR '(anon)' type_id=1",
		"[3] STRUCT 's1' size=8 vlen=1\n"
		"\t'f1' type_id=2 bits_offset=0",
		"[4] STRUCT '(anon)' size=12 vlen=2\n"
		"\t'f1' type_id=1 bits_offset=0\n"
		"\t'f2' type_id=3 bits_offset=32",
		"[5] INT 'unsigned int' size=4 bits_offset=0 nr_bits=32 encoding=(none)",
		"[6] UNION 'u1' size=12 vlen=2\n"
		"\t'f1' type_id=1 bits_offset=0\n"
		"\t'f2' type_id=2 bits_offset=0",
		"[7] UNION '(anon)' size=4 vlen=1\n"
		"\t'f1' type_id=1 bits_offset=0",
		"[8] ENUM 'e1' encoding=UNSIGNED size=4 vlen=1\n"
		"\t'v1' val=1",
		"[9] ENUM '(anon)' encoding=UNSIGNED size=4 vlen=1\n"
		"\t'av1' val=2",
		"[10] ENUM64 'e641' encoding=SIGNED size=8 vlen=1\n"
		"\t'v1' val=1024",
		"[11] ENUM64 '(anon)' encoding=SIGNED size=8 vlen=1\n"
		"\t'v1' val=1025",
		"[12] STRUCT 'unneeded' size=4 vlen=1\n"
		"\t'f1' type_id=1 bits_offset=0",
		"[13] STRUCT 'embedded' size=4 vlen=1\n"
		"\t'f1' type_id=1 bits_offset=0",
		"[14] FUNC_PROTO '(anon)' ret_type_id=1 vlen=1\n"
		"\t'p1' type_id=1",
		"[15] ARRAY '(anon)' type_id=1 index_type_id=1 nr_elems=3",
		"[16] STRUCT 'from_proto' size=4 vlen=1\n"
		"\t'f1' type_id=1 bits_offset=0",
		"[17] UNION 'u1' size=4 vlen=1\n"
		"\t'f1' type_id=1 bits_offset=0");

	btf2 = btf__new_empty_split(btf1);
	if (!ASSERT_OK_PTR(btf2, "empty_split_btf"))
		goto cleanup;

	btf__add_ptr(btf2, 3);				/* [18] ptr to struct s1 */
	/* add ptr to struct anon */
	btf__add_ptr(btf2, 4);				/* [19] ptr to struct (anon) */
	btf__add_const(btf2, 6);			/* [20] const union u1 */
	btf__add_restrict(btf2, 7);			/* [21] restrict union (anon) */
	btf__add_volatile(btf2, 8);			/* [22] volatile enum e1 */
	btf__add_typedef(btf2, "et", 9);		/* [23] typedef enum (anon) */
	btf__add_const(btf2, 10);			/* [24] const enum64 e641 */
	btf__add_ptr(btf2, 11);				/* [25] restrict enum64 (anon) */
	btf__add_struct(btf2, "with_embedded", 4);	/* [26] struct with_embedded { */
	btf__add_field(btf2, "f1", 13, 0, 0);		/*	struct embedded f1; */
							/* } */
	btf__add_func(btf2, "fn", BTF_FUNC_STATIC, 14);	/* [27] int fn(int p1); */
	btf__add_typedef(btf2, "arraytype", 15);	/* [28] typedef int[3] foo; */
	btf__add_func_proto(btf2, 1);			/* [29] int (*)(struct from proto p1); */
	btf__add_func_param(btf2, "p1", 16);

	VALIDATE_RAW_BTF(
		btf2,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[2] PTR '(anon)' type_id=1",
		"[3] STRUCT 's1' size=8 vlen=1\n"
		"\t'f1' type_id=2 bits_offset=0",
		"[4] STRUCT '(anon)' size=12 vlen=2\n"
		"\t'f1' type_id=1 bits_offset=0\n"
		"\t'f2' type_id=3 bits_offset=32",
		"[5] INT 'unsigned int' size=4 bits_offset=0 nr_bits=32 encoding=(none)",
		"[6] UNION 'u1' size=12 vlen=2\n"
		"\t'f1' type_id=1 bits_offset=0\n"
		"\t'f2' type_id=2 bits_offset=0",
		"[7] UNION '(anon)' size=4 vlen=1\n"
		"\t'f1' type_id=1 bits_offset=0",
		"[8] ENUM 'e1' encoding=UNSIGNED size=4 vlen=1\n"
		"\t'v1' val=1",
		"[9] ENUM '(anon)' encoding=UNSIGNED size=4 vlen=1\n"
		"\t'av1' val=2",
		"[10] ENUM64 'e641' encoding=SIGNED size=8 vlen=1\n"
		"\t'v1' val=1024",
		"[11] ENUM64 '(anon)' encoding=SIGNED size=8 vlen=1\n"
		"\t'v1' val=1025",
		"[12] STRUCT 'unneeded' size=4 vlen=1\n"
		"\t'f1' type_id=1 bits_offset=0",
		"[13] STRUCT 'embedded' size=4 vlen=1\n"
		"\t'f1' type_id=1 bits_offset=0",
		"[14] FUNC_PROTO '(anon)' ret_type_id=1 vlen=1\n"
		"\t'p1' type_id=1",
		"[15] ARRAY '(anon)' type_id=1 index_type_id=1 nr_elems=3",
		"[16] STRUCT 'from_proto' size=4 vlen=1\n"
		"\t'f1' type_id=1 bits_offset=0",
		"[17] UNION 'u1' size=4 vlen=1\n"
		"\t'f1' type_id=1 bits_offset=0",
		"[18] PTR '(anon)' type_id=3",
		"[19] PTR '(anon)' type_id=4",
		"[20] CONST '(anon)' type_id=6",
		"[21] RESTRICT '(anon)' type_id=7",
		"[22] VOLATILE '(anon)' type_id=8",
		"[23] TYPEDEF 'et' type_id=9",
		"[24] CONST '(anon)' type_id=10",
		"[25] PTR '(anon)' type_id=11",
		"[26] STRUCT 'with_embedded' size=4 vlen=1\n"
		"\t'f1' type_id=13 bits_offset=0",
		"[27] FUNC 'fn' type_id=14 linkage=static",
		"[28] TYPEDEF 'arraytype' type_id=15",
		"[29] FUNC_PROTO '(anon)' ret_type_id=1 vlen=1\n"
		"\t'p1' type_id=16");

	if (!ASSERT_EQ(0, btf__distill_base(btf2, &btf3, &btf4),
		       "distilled_base") ||
	    !ASSERT_OK_PTR(btf3, "distilled_base") ||
	    !ASSERT_OK_PTR(btf4, "distilled_split") ||
	    !ASSERT_EQ(8, btf__type_cnt(btf3), "distilled_base_type_cnt"))
		goto cleanup;

	VALIDATE_RAW_BTF(
		btf4,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[2] STRUCT 's1' size=8 vlen=0",
		"[3] UNION 'u1' size=12 vlen=0",
		"[4] ENUM 'e1' encoding=UNSIGNED size=4 vlen=0",
		"[5] ENUM 'e641' encoding=UNSIGNED size=8 vlen=0",
		"[6] STRUCT 'embedded' size=4 vlen=0",
		"[7] STRUCT 'from_proto' size=4 vlen=0",
		/* split BTF; these types should match split BTF above from 17-28, with
		 * updated type id references
		 */
		"[8] PTR '(anon)' type_id=2",
		"[9] PTR '(anon)' type_id=20",
		"[10] CONST '(anon)' type_id=3",
		"[11] RESTRICT '(anon)' type_id=21",
		"[12] VOLATILE '(anon)' type_id=4",
		"[13] TYPEDEF 'et' type_id=22",
		"[14] CONST '(anon)' type_id=5",
		"[15] PTR '(anon)' type_id=23",
		"[16] STRUCT 'with_embedded' size=4 vlen=1\n"
		"\t'f1' type_id=6 bits_offset=0",
		"[17] FUNC 'fn' type_id=24 linkage=static",
		"[18] TYPEDEF 'arraytype' type_id=25",
		"[19] FUNC_PROTO '(anon)' ret_type_id=1 vlen=1\n"
		"\t'p1' type_id=7",
		/* split BTF types added from original base BTF below */
		"[20] STRUCT '(anon)' size=12 vlen=2\n"
		"\t'f1' type_id=1 bits_offset=0\n"
		"\t'f2' type_id=2 bits_offset=32",
		"[21] UNION '(anon)' size=4 vlen=1\n"
		"\t'f1' type_id=1 bits_offset=0",
		"[22] ENUM '(anon)' encoding=UNSIGNED size=4 vlen=1\n"
		"\t'av1' val=2",
		"[23] ENUM64 '(anon)' encoding=SIGNED size=8 vlen=1\n"
		"\t'v1' val=1025",
		"[24] FUNC_PROTO '(anon)' ret_type_id=1 vlen=1\n"
		"\t'p1' type_id=1",
		"[25] ARRAY '(anon)' type_id=1 index_type_id=1 nr_elems=3");

	if (!ASSERT_EQ(btf__relocate(btf4, btf1), 0, "relocate_split"))
		goto cleanup;

	VALIDATE_RAW_BTF(
		btf4,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[2] PTR '(anon)' type_id=1",
		"[3] STRUCT 's1' size=8 vlen=1\n"
		"\t'f1' type_id=2 bits_offset=0",
		"[4] STRUCT '(anon)' size=12 vlen=2\n"
		"\t'f1' type_id=1 bits_offset=0\n"
		"\t'f2' type_id=3 bits_offset=32",
		"[5] INT 'unsigned int' size=4 bits_offset=0 nr_bits=32 encoding=(none)",
		"[6] UNION 'u1' size=12 vlen=2\n"
		"\t'f1' type_id=1 bits_offset=0\n"
		"\t'f2' type_id=2 bits_offset=0",
		"[7] UNION '(anon)' size=4 vlen=1\n"
		"\t'f1' type_id=1 bits_offset=0",
		"[8] ENUM 'e1' encoding=UNSIGNED size=4 vlen=1\n"
		"\t'v1' val=1",
		"[9] ENUM '(anon)' encoding=UNSIGNED size=4 vlen=1\n"
		"\t'av1' val=2",
		"[10] ENUM64 'e641' encoding=SIGNED size=8 vlen=1\n"
		"\t'v1' val=1024",
		"[11] ENUM64 '(anon)' encoding=SIGNED size=8 vlen=1\n"
		"\t'v1' val=1025",
		"[12] STRUCT 'unneeded' size=4 vlen=1\n"
		"\t'f1' type_id=1 bits_offset=0",
		"[13] STRUCT 'embedded' size=4 vlen=1\n"
		"\t'f1' type_id=1 bits_offset=0",
		"[14] FUNC_PROTO '(anon)' ret_type_id=1 vlen=1\n"
		"\t'p1' type_id=1",
		"[15] ARRAY '(anon)' type_id=1 index_type_id=1 nr_elems=3",
		"[16] STRUCT 'from_proto' size=4 vlen=1\n"
		"\t'f1' type_id=1 bits_offset=0",
		"[17] UNION 'u1' size=4 vlen=1\n"
		"\t'f1' type_id=1 bits_offset=0",
		"[18] PTR '(anon)' type_id=3",
		"[19] PTR '(anon)' type_id=30",
		"[20] CONST '(anon)' type_id=6",
		"[21] RESTRICT '(anon)' type_id=31",
		"[22] VOLATILE '(anon)' type_id=8",
		"[23] TYPEDEF 'et' type_id=32",
		"[24] CONST '(anon)' type_id=10",
		"[25] PTR '(anon)' type_id=33",
		"[26] STRUCT 'with_embedded' size=4 vlen=1\n"
		"\t'f1' type_id=13 bits_offset=0",
		"[27] FUNC 'fn' type_id=34 linkage=static",
		"[28] TYPEDEF 'arraytype' type_id=35",
		"[29] FUNC_PROTO '(anon)' ret_type_id=1 vlen=1\n"
		"\t'p1' type_id=16",
		/* below here are (duplicate) anon base types added by distill
		 * process to split BTF.
		 */
		"[30] STRUCT '(anon)' size=12 vlen=2\n"
		"\t'f1' type_id=1 bits_offset=0\n"
		"\t'f2' type_id=3 bits_offset=32",
		"[31] UNION '(anon)' size=4 vlen=1\n"
		"\t'f1' type_id=1 bits_offset=0",
		"[32] ENUM '(anon)' encoding=UNSIGNED size=4 vlen=1\n"
		"\t'av1' val=2",
		"[33] ENUM64 '(anon)' encoding=SIGNED size=8 vlen=1\n"
		"\t'v1' val=1025",
		"[34] FUNC_PROTO '(anon)' ret_type_id=1 vlen=1\n"
		"\t'p1' type_id=1",
		"[35] ARRAY '(anon)' type_id=1 index_type_id=1 nr_elems=3");

cleanup:
	btf__free(btf4);
	btf__free(btf3);
	btf__free(btf2);
	btf__free(btf1);
}

/* ensure we can cope with multiple types with the same name in
 * distilled base BTF.  In this case because sizes are different,
 * we can still disambiguate them.
 */
static void test_distilled_base_multi(void)
{
	struct btf *btf1 = NULL, *btf2 = NULL, *btf3 = NULL, *btf4 = NULL;

	btf1 = btf__new_empty();
	if (!ASSERT_OK_PTR(btf1, "empty_main_btf"))
		return;
	btf__add_int(btf1, "int", 4, BTF_INT_SIGNED);   /* [1] int */
	btf__add_int(btf1, "int", 8, BTF_INT_SIGNED);	/* [2] int */
	VALIDATE_RAW_BTF(
		btf1,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[2] INT 'int' size=8 bits_offset=0 nr_bits=64 encoding=SIGNED");
	btf2 = btf__new_empty_split(btf1);
	if (!ASSERT_OK_PTR(btf2, "empty_split_btf"))
		goto cleanup;
	btf__add_ptr(btf2, 1);
	btf__add_const(btf2, 2);
	VALIDATE_RAW_BTF(
		btf2,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[2] INT 'int' size=8 bits_offset=0 nr_bits=64 encoding=SIGNED",
		"[3] PTR '(anon)' type_id=1",
		"[4] CONST '(anon)' type_id=2");
	if (!ASSERT_EQ(0, btf__distill_base(btf2, &btf3, &btf4),
		       "distilled_base") ||
	    !ASSERT_OK_PTR(btf3, "distilled_base") ||
	    !ASSERT_OK_PTR(btf4, "distilled_split") ||
	    !ASSERT_EQ(3, btf__type_cnt(btf3), "distilled_base_type_cnt"))
		goto cleanup;
	VALIDATE_RAW_BTF(
		btf3,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[2] INT 'int' size=8 bits_offset=0 nr_bits=64 encoding=SIGNED");
	if (!ASSERT_EQ(btf__relocate(btf4, btf1), 0, "relocate_split"))
		goto cleanup;

	VALIDATE_RAW_BTF(
		btf4,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[2] INT 'int' size=8 bits_offset=0 nr_bits=64 encoding=SIGNED",
		"[3] PTR '(anon)' type_id=1",
		"[4] CONST '(anon)' type_id=2");

cleanup:
	btf__free(btf4);
	btf__free(btf3);
	btf__free(btf2);
	btf__free(btf1);
}

/* If a needed type is not present in the base BTF we wish to relocate
 * with, btf__relocate() should error our.
 */
static void test_distilled_base_missing_err(void)
{
	struct btf *btf1 = NULL, *btf2 = NULL, *btf3 = NULL, *btf4 = NULL, *btf5 = NULL;

	btf1 = btf__new_empty();
	if (!ASSERT_OK_PTR(btf1, "empty_main_btf"))
		return;
	btf__add_int(btf1, "int", 4, BTF_INT_SIGNED);   /* [1] int */
	btf__add_int(btf1, "int", 8, BTF_INT_SIGNED);   /* [2] int */
	VALIDATE_RAW_BTF(
		btf1,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[2] INT 'int' size=8 bits_offset=0 nr_bits=64 encoding=SIGNED");
	btf2 = btf__new_empty_split(btf1);
	if (!ASSERT_OK_PTR(btf2, "empty_split_btf"))
		goto cleanup;
	btf__add_ptr(btf2, 1);
	btf__add_const(btf2, 2);
	VALIDATE_RAW_BTF(
		btf2,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[2] INT 'int' size=8 bits_offset=0 nr_bits=64 encoding=SIGNED",
		"[3] PTR '(anon)' type_id=1",
		"[4] CONST '(anon)' type_id=2");
	if (!ASSERT_EQ(0, btf__distill_base(btf2, &btf3, &btf4),
		       "distilled_base") ||
	    !ASSERT_OK_PTR(btf3, "distilled_base") ||
	    !ASSERT_OK_PTR(btf4, "distilled_split") ||
	    !ASSERT_EQ(3, btf__type_cnt(btf3), "distilled_base_type_cnt"))
		goto cleanup;
	VALIDATE_RAW_BTF(
		btf3,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[2] INT 'int' size=8 bits_offset=0 nr_bits=64 encoding=SIGNED");
	btf5 = btf__new_empty();
	if (!ASSERT_OK_PTR(btf5, "empty_reloc_btf"))
		goto cleanup;
	btf__add_int(btf5, "int", 4, BTF_INT_SIGNED);   /* [1] int */
	VALIDATE_RAW_BTF(
		btf5,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED");
	ASSERT_EQ(btf__relocate(btf4, btf5), -EINVAL, "relocate_split");

cleanup:
	btf__free(btf5);
	btf__free(btf4);
	btf__free(btf3);
	btf__free(btf2);
	btf__free(btf1);
}

/* With 2 types of same size in distilled base BTF, relocation should
 * fail as we have no means to choose between them.
 */
static void test_distilled_base_multi_err(void)
{
	struct btf *btf1 = NULL, *btf2 = NULL, *btf3 = NULL, *btf4 = NULL;

	btf1 = btf__new_empty();
	if (!ASSERT_OK_PTR(btf1, "empty_main_btf"))
		return;
	btf__add_int(btf1, "int", 4, BTF_INT_SIGNED);   /* [1] int */
	btf__add_int(btf1, "int", 4, BTF_INT_SIGNED);   /* [2] int */
	VALIDATE_RAW_BTF(
		btf1,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[2] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED");
	btf2 = btf__new_empty_split(btf1);
	if (!ASSERT_OK_PTR(btf2, "empty_split_btf"))
		goto cleanup;
	btf__add_ptr(btf2, 1);
	btf__add_const(btf2, 2);
	VALIDATE_RAW_BTF(
		btf2,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[2] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[3] PTR '(anon)' type_id=1",
		"[4] CONST '(anon)' type_id=2");
	if (!ASSERT_EQ(0, btf__distill_base(btf2, &btf3, &btf4),
		       "distilled_base") ||
	    !ASSERT_OK_PTR(btf3, "distilled_base") ||
	    !ASSERT_OK_PTR(btf4, "distilled_split") ||
	    !ASSERT_EQ(3, btf__type_cnt(btf3), "distilled_base_type_cnt"))
		goto cleanup;
	VALIDATE_RAW_BTF(
		btf3,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[2] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED");
	ASSERT_EQ(btf__relocate(btf4, btf1), -EINVAL, "relocate_split");
cleanup:
	btf__free(btf4);
	btf__free(btf3);
	btf__free(btf2);
	btf__free(btf1);
}

/* With 2 types of same size in base BTF, relocation should
 * fail as we have no means to choose between them.
 */
static void test_distilled_base_multi_err2(void)
{
	struct btf *btf1 = NULL, *btf2 = NULL, *btf3 = NULL, *btf4 = NULL, *btf5 = NULL;

	btf1 = btf__new_empty();
	if (!ASSERT_OK_PTR(btf1, "empty_main_btf"))
		return;
	btf__add_int(btf1, "int", 4, BTF_INT_SIGNED);   /* [1] int */
	VALIDATE_RAW_BTF(
		btf1,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED");
	btf2 = btf__new_empty_split(btf1);
	if (!ASSERT_OK_PTR(btf2, "empty_split_btf"))
		goto cleanup;
	btf__add_ptr(btf2, 1);
	VALIDATE_RAW_BTF(
		btf2,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[2] PTR '(anon)' type_id=1");
	if (!ASSERT_EQ(0, btf__distill_base(btf2, &btf3, &btf4),
		       "distilled_base") ||
	    !ASSERT_OK_PTR(btf3, "distilled_base") ||
	    !ASSERT_OK_PTR(btf4, "distilled_split") ||
	    !ASSERT_EQ(2, btf__type_cnt(btf3), "distilled_base_type_cnt"))
		goto cleanup;
	VALIDATE_RAW_BTF(
		btf3,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED");
	btf5 = btf__new_empty();
	if (!ASSERT_OK_PTR(btf5, "empty_reloc_btf"))
		goto cleanup;
	btf__add_int(btf5, "int", 4, BTF_INT_SIGNED);   /* [1] int */
	btf__add_int(btf5, "int", 4, BTF_INT_SIGNED);   /* [2] int */
	VALIDATE_RAW_BTF(
		btf5,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[2] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED");
	ASSERT_EQ(btf__relocate(btf4, btf5), -EINVAL, "relocate_split");
cleanup:
	btf__free(btf5);
	btf__free(btf4);
	btf__free(btf3);
	btf__free(btf2);
	btf__free(btf1);
}

/* create split reference BTF from vmlinux + split BTF with a few type references;
 * ensure the resultant split reference BTF is as expected, containing only types
 * needed to disambiguate references from split BTF.
 */
static void test_distilled_base_vmlinux(void)
{
	struct btf *split_btf = NULL, *vmlinux_btf = btf__load_vmlinux_btf();
	struct btf *split_dist = NULL, *base_dist = NULL;
	__s32 int_id, myint_id;

	if (!ASSERT_OK_PTR(vmlinux_btf, "load_vmlinux"))
		return;
	int_id = btf__find_by_name_kind(vmlinux_btf, "int", BTF_KIND_INT);
	if (!ASSERT_GT(int_id, 0, "find_int"))
		goto cleanup;
	split_btf = btf__new_empty_split(vmlinux_btf);
	if (!ASSERT_OK_PTR(split_btf, "new_split"))
		goto cleanup;
	myint_id = btf__add_typedef(split_btf, "myint", int_id);
	btf__add_ptr(split_btf, myint_id);

	if (!ASSERT_EQ(btf__distill_base(split_btf, &base_dist, &split_dist), 0,
		       "distill_vmlinux_base"))
		goto cleanup;

	if (!ASSERT_OK_PTR(split_dist, "split_distilled") ||
	    !ASSERT_OK_PTR(base_dist, "base_dist"))
		goto cleanup;
	VALIDATE_RAW_BTF(
		split_dist,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[2] TYPEDEF 'myint' type_id=1",
		"[3] PTR '(anon)' type_id=2");

cleanup:
	btf__free(split_dist);
	btf__free(base_dist);
	btf__free(split_btf);
	btf__free(vmlinux_btf);
}

/* Split and new base BTFs should inherit endianness from source BTF. */
static void test_distilled_endianness(void)
{
	struct btf *base = NULL, *split = NULL, *new_base = NULL, *new_split = NULL;
	struct btf *new_base1 = NULL, *new_split1 = NULL;
	enum btf_endianness inverse_endianness;
	const void *raw_data;
	__u32 size;

	base = btf__new_empty();
	if (!ASSERT_OK_PTR(base, "empty_main_btf"))
		return;
	inverse_endianness = btf__endianness(base) == BTF_LITTLE_ENDIAN ? BTF_BIG_ENDIAN
									: BTF_LITTLE_ENDIAN;
	btf__set_endianness(base, inverse_endianness);
	btf__add_int(base, "int", 4, BTF_INT_SIGNED);   /* [1] int */
	VALIDATE_RAW_BTF(
		base,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED");
	split = btf__new_empty_split(base);
	if (!ASSERT_OK_PTR(split, "empty_split_btf"))
		goto cleanup;
	btf__add_ptr(split, 1);
	VALIDATE_RAW_BTF(
		split,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[2] PTR '(anon)' type_id=1");
	if (!ASSERT_EQ(0, btf__distill_base(split, &new_base, &new_split),
		       "distilled_base") ||
	    !ASSERT_OK_PTR(new_base, "distilled_base") ||
	    !ASSERT_OK_PTR(new_split, "distilled_split") ||
	    !ASSERT_EQ(2, btf__type_cnt(new_base), "distilled_base_type_cnt"))
		goto cleanup;
	VALIDATE_RAW_BTF(
		new_split,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[2] PTR '(anon)' type_id=1");

	raw_data = btf__raw_data(new_base, &size);
	if (!ASSERT_OK_PTR(raw_data, "btf__raw_data #1"))
		goto cleanup;
	new_base1 = btf__new(raw_data, size);
	if (!ASSERT_OK_PTR(new_base1, "new_base1 = btf__new()"))
		goto cleanup;
	raw_data = btf__raw_data(new_split, &size);
	if (!ASSERT_OK_PTR(raw_data, "btf__raw_data #2"))
		goto cleanup;
	new_split1 = btf__new_split(raw_data, size, new_base1);
	if (!ASSERT_OK_PTR(new_split1, "new_split1 = btf__new()"))
		goto cleanup;

	ASSERT_EQ(btf__endianness(new_base1), inverse_endianness, "new_base1 endianness");
	ASSERT_EQ(btf__endianness(new_split1), inverse_endianness, "new_split1 endianness");
	VALIDATE_RAW_BTF(
		new_split1,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[2] PTR '(anon)' type_id=1");
cleanup:
	btf__free(new_split1);
	btf__free(new_base1);
	btf__free(new_split);
	btf__free(new_base);
	btf__free(split);
	btf__free(base);
}

/* If a needed composite type, which is the member of composite type
 * in the split BTF, has a different size in the base BTF we wish to
 * relocate with, btf__relocate() should error out.
 */
static void test_distilled_base_embedded_err(void)
{
	struct btf *btf1 = NULL, *btf2 = NULL, *btf3 = NULL, *btf4 = NULL, *btf5 = NULL;

	btf1 = btf__new_empty();
	if (!ASSERT_OK_PTR(btf1, "empty_main_btf"))
		return;

	btf__add_int(btf1, "int", 4, BTF_INT_SIGNED);   /* [1] int */
	btf__add_struct(btf1, "s1", 4);                 /* [2] struct s1 { */
	btf__add_field(btf1, "f1", 1, 0, 0);            /*      int f1; */
							/* } */
	VALIDATE_RAW_BTF(
		btf1,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[2] STRUCT 's1' size=4 vlen=1\n"
		"\t'f1' type_id=1 bits_offset=0");

	btf2 = btf__new_empty_split(btf1);
	if (!ASSERT_OK_PTR(btf2, "empty_split_btf"))
		goto cleanup;

	btf__add_struct(btf2, "with_embedded", 8);      /* [3] struct with_embedded { */
	btf__add_field(btf2, "e1", 2, 0, 0);		/*      struct s1 e1; */
							/* } */

	VALIDATE_RAW_BTF(
		btf2,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[2] STRUCT 's1' size=4 vlen=1\n"
		"\t'f1' type_id=1 bits_offset=0",
		"[3] STRUCT 'with_embedded' size=8 vlen=1\n"
		"\t'e1' type_id=2 bits_offset=0");

	if (!ASSERT_EQ(0, btf__distill_base(btf2, &btf3, &btf4),
		       "distilled_base") ||
	    !ASSERT_OK_PTR(btf3, "distilled_base") ||
	    !ASSERT_OK_PTR(btf4, "distilled_split") ||
	    !ASSERT_EQ(2, btf__type_cnt(btf3), "distilled_base_type_cnt"))
		goto cleanup;

	VALIDATE_RAW_BTF(
		btf4,
		"[1] STRUCT 's1' size=4 vlen=0",
		"[2] STRUCT 'with_embedded' size=8 vlen=1\n"
		"\t'e1' type_id=1 bits_offset=0");

	btf5 = btf__new_empty();
	if (!ASSERT_OK_PTR(btf5, "empty_reloc_btf"))
		goto cleanup;

	btf__add_int(btf5, "int", 4, BTF_INT_SIGNED);   /* [1] int */
	/* struct with the same name but different size */
	btf__add_struct(btf5, "s1", 8);                 /* [2] struct s1 { */
	btf__add_field(btf5, "f1", 1, 0, 0);            /*      int f1; */
							/* } */

	ASSERT_EQ(btf__relocate(btf4, btf5), -EINVAL, "relocate_split");
cleanup:
	btf__free(btf5);
	btf__free(btf4);
	btf__free(btf3);
	btf__free(btf2);
	btf__free(btf1);
}

void test_btf_distill(void)
{
	if (test__start_subtest("distilled_base"))
		test_distilled_base();
	if (test__start_subtest("distilled_base_multi"))
		test_distilled_base_multi();
	if (test__start_subtest("distilled_base_missing_err"))
		test_distilled_base_missing_err();
	if (test__start_subtest("distilled_base_multi_err"))
		test_distilled_base_multi_err();
	if (test__start_subtest("distilled_base_multi_err2"))
		test_distilled_base_multi_err2();
	if (test__start_subtest("distilled_base_embedded_err"))
		test_distilled_base_embedded_err();
	if (test__start_subtest("distilled_base_vmlinux"))
		test_distilled_base_vmlinux();
	if (test__start_subtest("distilled_endianness"))
		test_distilled_endianness();
}
