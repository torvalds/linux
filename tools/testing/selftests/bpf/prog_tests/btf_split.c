// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#include <test_progs.h>
#include <bpf/btf.h>

static char *dump_buf;
static size_t dump_buf_sz;
static FILE *dump_buf_file;

static void btf_dump_printf(void *ctx, const char *fmt, va_list args)
{
	vfprintf(ctx, fmt, args);
}

static void __test_btf_split(bool multi)
{
	struct btf_dump *d = NULL;
	const struct btf_type *t;
	struct btf *btf1, *btf2, *btf3 = NULL;
	int str_off, i, err;

	btf1 = btf__new_empty();
	if (!ASSERT_OK_PTR(btf1, "empty_main_btf"))
		return;

	btf__set_pointer_size(btf1, 8); /* enforce 64-bit arch */

	btf__add_int(btf1, "int", 4, BTF_INT_SIGNED);	/* [1] int */
	btf__add_ptr(btf1, 1);				/* [2] ptr to int */

	btf__add_struct(btf1, "s1", 4);			/* [3] struct s1 { */
	btf__add_field(btf1, "f1", 1, 0, 0);		/*      int f1; */
							/* } */

	btf2 = btf__new_empty_split(btf1);
	if (!ASSERT_OK_PTR(btf2, "empty_split_btf"))
		goto cleanup;

	/* pointer size should be "inherited" from main BTF */
	ASSERT_EQ(btf__pointer_size(btf2), 8, "inherit_ptr_sz");

	str_off = btf__find_str(btf2, "int");
	ASSERT_NEQ(str_off, -ENOENT, "str_int_missing");

	t = btf__type_by_id(btf2, 1);
	if (!ASSERT_OK_PTR(t, "int_type"))
		goto cleanup;
	ASSERT_EQ(btf_is_int(t), true, "int_kind");
	ASSERT_STREQ(btf__str_by_offset(btf2, t->name_off), "int", "int_name");

	btf__add_struct(btf2, "s2", 16);		/* [4] struct s2 {	*/
	btf__add_field(btf2, "f1", 3, 0, 0);		/*      struct s1 f1;	*/
	btf__add_field(btf2, "f2", 1, 32, 0);		/*      int f2;		*/
	btf__add_field(btf2, "f3", 2, 64, 0);		/*      int *f3;	*/
							/* } */

	t = btf__type_by_id(btf1, 4);
	ASSERT_NULL(t, "split_type_in_main");

	t = btf__type_by_id(btf2, 4);
	if (!ASSERT_OK_PTR(t, "split_struct_type"))
		goto cleanup;
	ASSERT_EQ(btf_is_struct(t), true, "split_struct_kind");
	ASSERT_EQ(btf_vlen(t), 3, "split_struct_vlen");
	ASSERT_STREQ(btf__str_by_offset(btf2, t->name_off), "s2", "split_struct_name");

	if (multi) {
		btf3 = btf__new_empty_split(btf2);
		if (!ASSERT_OK_PTR(btf3, "multi_split_btf"))
			goto cleanup;
	} else {
		btf3 = btf2;
	}

	btf__add_union(btf3, "u1", 16);			/* [5] union u1 {	*/
	btf__add_field(btf3, "f1", 4, 0, 0);		/*	struct s2 f1;	*/
	btf__add_field(btf3, "uf2", 1, 0, 0);		/*	int f2;		*/
							/* } */

	if (multi) {
		t = btf__type_by_id(btf2, 5);
		ASSERT_NULL(t, "multisplit_type_in_first_split");
	}

	t = btf__type_by_id(btf3, 5);
	if (!ASSERT_OK_PTR(t, "split_union_type"))
		goto cleanup;
	ASSERT_EQ(btf_is_union(t), true, "split_union_kind");
	ASSERT_EQ(btf_vlen(t), 2, "split_union_vlen");
	ASSERT_STREQ(btf__str_by_offset(btf3, t->name_off), "u1", "split_union_name");
	ASSERT_EQ(btf__type_cnt(btf3), 6, "split_type_cnt");

	t = btf__type_by_id(btf3, 1);
	if (!ASSERT_OK_PTR(t, "split_base_type"))
		goto cleanup;
	ASSERT_EQ(btf_is_int(t), true, "split_base_int");
	ASSERT_STREQ(btf__str_by_offset(btf3, t->name_off), "int", "split_base_type_name");

	/* BTF-to-C dump of split BTF */
	dump_buf_file = open_memstream(&dump_buf, &dump_buf_sz);
	if (!ASSERT_OK_PTR(dump_buf_file, "dump_memstream"))
		return;
	d = btf_dump__new(btf3, btf_dump_printf, dump_buf_file, NULL);
	if (!ASSERT_OK_PTR(d, "btf_dump__new"))
		goto cleanup;
	for (i = 1; i < btf__type_cnt(btf3); i++) {
		err = btf_dump__dump_type(d, i);
		ASSERT_OK(err, "dump_type_ok");
	}
	fflush(dump_buf_file);
	dump_buf[dump_buf_sz] = 0; /* some libc implementations don't do this */
	ASSERT_STREQ(dump_buf,
"struct s1 {\n"
"	int f1;\n"
"};\n\n"
"struct s2 {\n"
"	struct s1 f1;\n"
"	int f2;\n"
"	int *f3;\n"
"};\n\n"
"union u1 {\n"
"	struct s2 f1;\n"
"	int uf2;\n"
"};\n\n", "c_dump");

cleanup:
	if (dump_buf_file)
		fclose(dump_buf_file);
	free(dump_buf);
	btf_dump__free(d);
	btf__free(btf1);
	btf__free(btf2);
	if (btf2 != btf3)
		btf__free(btf3);
}

void test_btf_split(void)
{
	if (test__start_subtest("single_split"))
		__test_btf_split(false);
	if (test__start_subtest("multi_split"))
		__test_btf_split(true);
}
