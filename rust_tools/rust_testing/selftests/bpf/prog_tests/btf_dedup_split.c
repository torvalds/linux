// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#include <test_progs.h>
#include <bpf/btf.h>
#include "btf_helpers.h"

static void test_split_simple() {
	const struct btf_type *t;
	struct btf *btf1, *btf2;
	int str_off, err;

	btf1 = btf__new_empty();
	if (!ASSERT_OK_PTR(btf1, "empty_main_btf"))
		return;

	btf__set_pointer_size(btf1, 8); /* enforce 64-bit arch */

	btf__add_int(btf1, "int", 4, BTF_INT_SIGNED);	/* [1] int */
	btf__add_ptr(btf1, 1);				/* [2] ptr to int */
	btf__add_struct(btf1, "s1", 4);			/* [3] struct s1 { */
	btf__add_field(btf1, "f1", 1, 0, 0);		/*      int f1; */
							/* } */

	VALIDATE_RAW_BTF(
		btf1,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[2] PTR '(anon)' type_id=1",
		"[3] STRUCT 's1' size=4 vlen=1\n"
		"\t'f1' type_id=1 bits_offset=0");

	ASSERT_STREQ(btf_type_c_dump(btf1), "\
struct s1 {\n\
	int f1;\n\
};\n\n", "c_dump");

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
	btf__add_field(btf2, "f1", 6, 0, 0);		/*      struct s1 f1;	*/
	btf__add_field(btf2, "f2", 5, 32, 0);		/*      int f2;		*/
	btf__add_field(btf2, "f3", 2, 64, 0);		/*      int *f3;	*/
							/* } */

	/* duplicated int */
	btf__add_int(btf2, "int", 4, BTF_INT_SIGNED);	/* [5] int */

	/* duplicated struct s1 */
	btf__add_struct(btf2, "s1", 4);			/* [6] struct s1 { */
	btf__add_field(btf2, "f1", 5, 0, 0);		/*      int f1; */
							/* } */

	VALIDATE_RAW_BTF(
		btf2,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[2] PTR '(anon)' type_id=1",
		"[3] STRUCT 's1' size=4 vlen=1\n"
		"\t'f1' type_id=1 bits_offset=0",
		"[4] STRUCT 's2' size=16 vlen=3\n"
		"\t'f1' type_id=6 bits_offset=0\n"
		"\t'f2' type_id=5 bits_offset=32\n"
		"\t'f3' type_id=2 bits_offset=64",
		"[5] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[6] STRUCT 's1' size=4 vlen=1\n"
		"\t'f1' type_id=5 bits_offset=0");

	ASSERT_STREQ(btf_type_c_dump(btf2), "\
struct s1 {\n\
	int f1;\n\
};\n\
\n\
struct s1___2 {\n\
	int f1;\n\
};\n\
\n\
struct s2 {\n\
	struct s1___2 f1;\n\
	int f2;\n\
	int *f3;\n\
};\n\n", "c_dump");

	err = btf__dedup(btf2, NULL);
	if (!ASSERT_OK(err, "btf_dedup"))
		goto cleanup;

	VALIDATE_RAW_BTF(
		btf2,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[2] PTR '(anon)' type_id=1",
		"[3] STRUCT 's1' size=4 vlen=1\n"
		"\t'f1' type_id=1 bits_offset=0",
		"[4] STRUCT 's2' size=16 vlen=3\n"
		"\t'f1' type_id=3 bits_offset=0\n"
		"\t'f2' type_id=1 bits_offset=32\n"
		"\t'f3' type_id=2 bits_offset=64");

	ASSERT_STREQ(btf_type_c_dump(btf2), "\
struct s1 {\n\
	int f1;\n\
};\n\
\n\
struct s2 {\n\
	struct s1 f1;\n\
	int f2;\n\
	int *f3;\n\
};\n\n", "c_dump");

cleanup:
	btf__free(btf2);
	btf__free(btf1);
}

static void test_split_fwd_resolve() {
	struct btf *btf1, *btf2;
	int err;

	btf1 = btf__new_empty();
	if (!ASSERT_OK_PTR(btf1, "empty_main_btf"))
		return;

	btf__set_pointer_size(btf1, 8); /* enforce 64-bit arch */

	btf__add_int(btf1, "int", 4, BTF_INT_SIGNED);	/* [1] int */
	btf__add_ptr(btf1, 4);				/* [2] ptr to struct s1 */
	btf__add_ptr(btf1, 5);				/* [3] ptr to struct s2 */
	btf__add_struct(btf1, "s1", 16);		/* [4] struct s1 { */
	btf__add_field(btf1, "f1", 2, 0, 0);		/*      struct s1 *f1; */
	btf__add_field(btf1, "f2", 3, 64, 0);		/*      struct s2 *f2; */
							/* } */
	btf__add_struct(btf1, "s2", 4);			/* [5] struct s2 { */
	btf__add_field(btf1, "f1", 1, 0, 0);		/*      int f1; */
							/* } */
	/* keep this not a part of type the graph to test btf_dedup_resolve_fwds */
	btf__add_struct(btf1, "s3", 4);                 /* [6] struct s3 { */
	btf__add_field(btf1, "f1", 1, 0, 0);		/*      int f1; */
							/* } */

	VALIDATE_RAW_BTF(
		btf1,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[2] PTR '(anon)' type_id=4",
		"[3] PTR '(anon)' type_id=5",
		"[4] STRUCT 's1' size=16 vlen=2\n"
		"\t'f1' type_id=2 bits_offset=0\n"
		"\t'f2' type_id=3 bits_offset=64",
		"[5] STRUCT 's2' size=4 vlen=1\n"
		"\t'f1' type_id=1 bits_offset=0",
		"[6] STRUCT 's3' size=4 vlen=1\n"
		"\t'f1' type_id=1 bits_offset=0");

	btf2 = btf__new_empty_split(btf1);
	if (!ASSERT_OK_PTR(btf2, "empty_split_btf"))
		goto cleanup;

	btf__add_int(btf2, "int", 4, BTF_INT_SIGNED);	/* [7] int */
	btf__add_ptr(btf2, 11);				/* [8] ptr to struct s1 */
	btf__add_fwd(btf2, "s2", BTF_FWD_STRUCT);	/* [9] fwd for struct s2 */
	btf__add_ptr(btf2, 9);				/* [10] ptr to fwd struct s2 */
	btf__add_struct(btf2, "s1", 16);		/* [11] struct s1 { */
	btf__add_field(btf2, "f1", 8, 0, 0);		/*      struct s1 *f1; */
	btf__add_field(btf2, "f2", 10, 64, 0);		/*      struct s2 *f2; */
							/* } */
	btf__add_fwd(btf2, "s3", BTF_FWD_STRUCT);	/* [12] fwd for struct s3 */
	btf__add_ptr(btf2, 12);				/* [13] ptr to struct s1 */

	VALIDATE_RAW_BTF(
		btf2,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[2] PTR '(anon)' type_id=4",
		"[3] PTR '(anon)' type_id=5",
		"[4] STRUCT 's1' size=16 vlen=2\n"
		"\t'f1' type_id=2 bits_offset=0\n"
		"\t'f2' type_id=3 bits_offset=64",
		"[5] STRUCT 's2' size=4 vlen=1\n"
		"\t'f1' type_id=1 bits_offset=0",
		"[6] STRUCT 's3' size=4 vlen=1\n"
		"\t'f1' type_id=1 bits_offset=0",
		"[7] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[8] PTR '(anon)' type_id=11",
		"[9] FWD 's2' fwd_kind=struct",
		"[10] PTR '(anon)' type_id=9",
		"[11] STRUCT 's1' size=16 vlen=2\n"
		"\t'f1' type_id=8 bits_offset=0\n"
		"\t'f2' type_id=10 bits_offset=64",
		"[12] FWD 's3' fwd_kind=struct",
		"[13] PTR '(anon)' type_id=12");

	err = btf__dedup(btf2, NULL);
	if (!ASSERT_OK(err, "btf_dedup"))
		goto cleanup;

	VALIDATE_RAW_BTF(
		btf2,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[2] PTR '(anon)' type_id=4",
		"[3] PTR '(anon)' type_id=5",
		"[4] STRUCT 's1' size=16 vlen=2\n"
		"\t'f1' type_id=2 bits_offset=0\n"
		"\t'f2' type_id=3 bits_offset=64",
		"[5] STRUCT 's2' size=4 vlen=1\n"
		"\t'f1' type_id=1 bits_offset=0",
		"[6] STRUCT 's3' size=4 vlen=1\n"
		"\t'f1' type_id=1 bits_offset=0",
		"[7] PTR '(anon)' type_id=6");

cleanup:
	btf__free(btf2);
	btf__free(btf1);
}

static void test_split_struct_duped() {
	struct btf *btf1, *btf2;
	int err;

	btf1 = btf__new_empty();
	if (!ASSERT_OK_PTR(btf1, "empty_main_btf"))
		return;

	btf__set_pointer_size(btf1, 8); /* enforce 64-bit arch */

	btf__add_int(btf1, "int", 4, BTF_INT_SIGNED);	/* [1] int */
	btf__add_ptr(btf1, 5);				/* [2] ptr to struct s1 */
	btf__add_fwd(btf1, "s2", BTF_FWD_STRUCT);	/* [3] fwd for struct s2 */
	btf__add_ptr(btf1, 3);				/* [4] ptr to fwd struct s2 */
	btf__add_struct(btf1, "s1", 16);		/* [5] struct s1 { */
	btf__add_field(btf1, "f1", 2, 0, 0);		/*      struct s1 *f1; */
	btf__add_field(btf1, "f2", 4, 64, 0);		/*      struct s2 *f2; */
							/* } */

	VALIDATE_RAW_BTF(
		btf1,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[2] PTR '(anon)' type_id=5",
		"[3] FWD 's2' fwd_kind=struct",
		"[4] PTR '(anon)' type_id=3",
		"[5] STRUCT 's1' size=16 vlen=2\n"
		"\t'f1' type_id=2 bits_offset=0\n"
		"\t'f2' type_id=4 bits_offset=64");

	btf2 = btf__new_empty_split(btf1);
	if (!ASSERT_OK_PTR(btf2, "empty_split_btf"))
		goto cleanup;

	btf__add_int(btf2, "int", 4, BTF_INT_SIGNED);	/* [6] int */
	btf__add_ptr(btf2, 10);				/* [7] ptr to struct s1 */
	btf__add_fwd(btf2, "s2", BTF_FWD_STRUCT);	/* [8] fwd for struct s2 */
	btf__add_ptr(btf2, 11);				/* [9] ptr to struct s2 */
	btf__add_struct(btf2, "s1", 16);		/* [10] struct s1 { */
	btf__add_field(btf2, "f1", 7, 0, 0);		/*      struct s1 *f1; */
	btf__add_field(btf2, "f2", 9, 64, 0);		/*      struct s2 *f2; */
							/* } */
	btf__add_struct(btf2, "s2", 40);		/* [11] struct s2 {	*/
	btf__add_field(btf2, "f1", 7, 0, 0);		/*      struct s1 *f1;	*/
	btf__add_field(btf2, "f2", 9, 64, 0);		/*      struct s2 *f2;	*/
	btf__add_field(btf2, "f3", 6, 128, 0);		/*      int f3;		*/
	btf__add_field(btf2, "f4", 10, 192, 0);		/*      struct s1 f4;	*/
							/* } */
	btf__add_ptr(btf2, 8);				/* [12] ptr to fwd struct s2 */
	btf__add_struct(btf2, "s3", 8);			/* [13] struct s3 { */
	btf__add_field(btf2, "f1", 12, 0, 0);		/*      struct s2 *f1; (fwd) */
							/* } */

	VALIDATE_RAW_BTF(
		btf2,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[2] PTR '(anon)' type_id=5",
		"[3] FWD 's2' fwd_kind=struct",
		"[4] PTR '(anon)' type_id=3",
		"[5] STRUCT 's1' size=16 vlen=2\n"
		"\t'f1' type_id=2 bits_offset=0\n"
		"\t'f2' type_id=4 bits_offset=64",
		"[6] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[7] PTR '(anon)' type_id=10",
		"[8] FWD 's2' fwd_kind=struct",
		"[9] PTR '(anon)' type_id=11",
		"[10] STRUCT 's1' size=16 vlen=2\n"
		"\t'f1' type_id=7 bits_offset=0\n"
		"\t'f2' type_id=9 bits_offset=64",
		"[11] STRUCT 's2' size=40 vlen=4\n"
		"\t'f1' type_id=7 bits_offset=0\n"
		"\t'f2' type_id=9 bits_offset=64\n"
		"\t'f3' type_id=6 bits_offset=128\n"
		"\t'f4' type_id=10 bits_offset=192",
		"[12] PTR '(anon)' type_id=8",
		"[13] STRUCT 's3' size=8 vlen=1\n"
		"\t'f1' type_id=12 bits_offset=0");

	err = btf__dedup(btf2, NULL);
	if (!ASSERT_OK(err, "btf_dedup"))
		goto cleanup;

	VALIDATE_RAW_BTF(
		btf2,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[2] PTR '(anon)' type_id=5",
		"[3] FWD 's2' fwd_kind=struct",
		"[4] PTR '(anon)' type_id=3",
		"[5] STRUCT 's1' size=16 vlen=2\n"
		"\t'f1' type_id=2 bits_offset=0\n"
		"\t'f2' type_id=4 bits_offset=64",
		"[6] PTR '(anon)' type_id=8",
		"[7] PTR '(anon)' type_id=9",
		"[8] STRUCT 's1' size=16 vlen=2\n"
		"\t'f1' type_id=6 bits_offset=0\n"
		"\t'f2' type_id=7 bits_offset=64",
		"[9] STRUCT 's2' size=40 vlen=4\n"
		"\t'f1' type_id=6 bits_offset=0\n"
		"\t'f2' type_id=7 bits_offset=64\n"
		"\t'f3' type_id=1 bits_offset=128\n"
		"\t'f4' type_id=8 bits_offset=192",
		"[10] STRUCT 's3' size=8 vlen=1\n"
		"\t'f1' type_id=7 bits_offset=0");

cleanup:
	btf__free(btf2);
	btf__free(btf1);
}

static void btf_add_dup_struct_in_cu(struct btf *btf, int start_id)
{
#define ID(n) (start_id + n)
	btf__set_pointer_size(btf, 8); /* enforce 64-bit arch */

	btf__add_int(btf, "int", 4, BTF_INT_SIGNED);    /* [1] int */

	btf__add_struct(btf, "s", 8);                   /* [2] struct s { */
	btf__add_field(btf, "a", ID(3), 0, 0);          /*      struct anon a; */
	btf__add_field(btf, "b", ID(4), 0, 0);          /*      struct anon b; */
							/* } */

	btf__add_struct(btf, "(anon)", 8);              /* [3] struct anon { */
	btf__add_field(btf, "f1", ID(1), 0, 0);         /*      int f1; */
	btf__add_field(btf, "f2", ID(1), 32, 0);        /*      int f2; */
							/* } */

	btf__add_struct(btf, "(anon)", 8);              /* [4] struct anon { */
	btf__add_field(btf, "f1", ID(1), 0, 0);         /*      int f1; */
	btf__add_field(btf, "f2", ID(1), 32, 0);        /*      int f2; */
							/* } */
#undef ID
}

static void test_split_dup_struct_in_cu()
{
	struct btf *btf1, *btf2 = NULL;
	int err;

	/* generate the base data.. */
	btf1 = btf__new_empty();
	if (!ASSERT_OK_PTR(btf1, "empty_main_btf"))
		return;

	btf_add_dup_struct_in_cu(btf1, 0);

	VALIDATE_RAW_BTF(
			btf1,
			"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
			"[2] STRUCT 's' size=8 vlen=2\n"
			"\t'a' type_id=3 bits_offset=0\n"
			"\t'b' type_id=4 bits_offset=0",
			"[3] STRUCT '(anon)' size=8 vlen=2\n"
			"\t'f1' type_id=1 bits_offset=0\n"
			"\t'f2' type_id=1 bits_offset=32",
			"[4] STRUCT '(anon)' size=8 vlen=2\n"
			"\t'f1' type_id=1 bits_offset=0\n"
			"\t'f2' type_id=1 bits_offset=32");

	/* ..dedup them... */
	err = btf__dedup(btf1, NULL);
	if (!ASSERT_OK(err, "btf_dedup"))
		goto cleanup;

	VALIDATE_RAW_BTF(
			btf1,
			"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
			"[2] STRUCT 's' size=8 vlen=2\n"
			"\t'a' type_id=3 bits_offset=0\n"
			"\t'b' type_id=3 bits_offset=0",
			"[3] STRUCT '(anon)' size=8 vlen=2\n"
			"\t'f1' type_id=1 bits_offset=0\n"
			"\t'f2' type_id=1 bits_offset=32");

	/* and add the same data on top of it */
	btf2 = btf__new_empty_split(btf1);
	if (!ASSERT_OK_PTR(btf2, "empty_split_btf"))
		goto cleanup;

	btf_add_dup_struct_in_cu(btf2, 3);

	VALIDATE_RAW_BTF(
			btf2,
			"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
			"[2] STRUCT 's' size=8 vlen=2\n"
			"\t'a' type_id=3 bits_offset=0\n"
			"\t'b' type_id=3 bits_offset=0",
			"[3] STRUCT '(anon)' size=8 vlen=2\n"
			"\t'f1' type_id=1 bits_offset=0\n"
			"\t'f2' type_id=1 bits_offset=32",
			"[4] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
			"[5] STRUCT 's' size=8 vlen=2\n"
			"\t'a' type_id=6 bits_offset=0\n"
			"\t'b' type_id=7 bits_offset=0",
			"[6] STRUCT '(anon)' size=8 vlen=2\n"
			"\t'f1' type_id=4 bits_offset=0\n"
			"\t'f2' type_id=4 bits_offset=32",
			"[7] STRUCT '(anon)' size=8 vlen=2\n"
			"\t'f1' type_id=4 bits_offset=0\n"
			"\t'f2' type_id=4 bits_offset=32");

	err = btf__dedup(btf2, NULL);
	if (!ASSERT_OK(err, "btf_dedup"))
		goto cleanup;

	/* after dedup it should match the original data */
	VALIDATE_RAW_BTF(
			btf2,
			"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
			"[2] STRUCT 's' size=8 vlen=2\n"
			"\t'a' type_id=3 bits_offset=0\n"
			"\t'b' type_id=3 bits_offset=0",
			"[3] STRUCT '(anon)' size=8 vlen=2\n"
			"\t'f1' type_id=1 bits_offset=0\n"
			"\t'f2' type_id=1 bits_offset=32");

cleanup:
	btf__free(btf2);
	btf__free(btf1);
}

/* Ensure module split BTF dedup worked correctly; when dedup fails badly
 * core kernel types are in split BTF also, so ensure that references to
 * such types point at base - not split - BTF.
 *
 * bpf_testmod_test_write() has multiple core kernel type parameters;
 *
 * ssize_t
 * bpf_testmod_test_write(struct file *file, struct kobject *kobj,
 *                        struct bin_attribute *bin_attr,
 *                        char *buf, loff_t off, size_t len);
 *
 * Ensure each of the FUNC_PROTO params is a core kernel type.
 *
 * Do the same for
 *
 * __bpf_kfunc struct sock *bpf_kfunc_call_test3(struct sock *sk);
 *
 * ...and
 *
 * __bpf_kfunc void bpf_kfunc_call_test_pass_ctx(struct __sk_buff *skb);
 *
 */
const char *mod_funcs[] = {
	"bpf_testmod_test_write",
	"bpf_kfunc_call_test3",
	"bpf_kfunc_call_test_pass_ctx"
};

static void test_split_module(void)
{
	struct btf *vmlinux_btf, *btf1 = NULL;
	int i, nr_base_types;

	vmlinux_btf = btf__load_vmlinux_btf();
	if (!ASSERT_OK_PTR(vmlinux_btf, "vmlinux_btf"))
		return;
	nr_base_types = btf__type_cnt(vmlinux_btf);
	if (!ASSERT_GT(nr_base_types, 0, "nr_base_types"))
		goto cleanup;

	btf1 = btf__parse_split("/sys/kernel/btf/bpf_testmod", vmlinux_btf);
	if (!ASSERT_OK_PTR(btf1, "split_btf"))
		return;

	for (i = 0; i < ARRAY_SIZE(mod_funcs); i++) {
		const struct btf_param *p;
		const struct btf_type *t;
		__u16 vlen;
		__u32 id;
		int j;

		id = btf__find_by_name_kind(btf1, mod_funcs[i], BTF_KIND_FUNC);
		if (!ASSERT_GE(id, nr_base_types, "func_id"))
			goto cleanup;
		t = btf__type_by_id(btf1, id);
		if (!ASSERT_OK_PTR(t, "func_id_type"))
			goto cleanup;
		t = btf__type_by_id(btf1, t->type);
		if (!ASSERT_OK_PTR(t, "func_proto_id_type"))
			goto cleanup;
		if (!ASSERT_EQ(btf_is_func_proto(t), true, "is_func_proto"))
			goto cleanup;
		vlen = btf_vlen(t);

		for (j = 0, p = btf_params(t); j < vlen; j++, p++) {
			/* bpf_testmod uses resilient split BTF, so any
			 * reference types will be added to split BTF and their
			 * associated targets will be base BTF types; for example
			 * for a "struct sock *" the PTR will be in split BTF
			 * while the "struct sock" will be in base.
			 *
			 * In some cases like loff_t we have to resolve
			 * multiple typedefs hence the while() loop below.
			 *
			 * Note that resilient split BTF generation depends
			 * on pahole version, so we do not assert that
			 * reference types are in split BTF, as if pahole
			 * does not support resilient split BTF they will
			 * also be base BTF types.
			 */
			id = p->type;
			do {
				t = btf__type_by_id(btf1, id);
				if (!ASSERT_OK_PTR(t, "param_ref_type"))
					goto cleanup;
				if (!btf_is_mod(t) && !btf_is_ptr(t) && !btf_is_typedef(t))
					break;
				id = t->type;
			} while (true);

			if (!ASSERT_LT(id, nr_base_types, "verify_base_type"))
				goto cleanup;
		}
	}
cleanup:
	btf__free(btf1);
	btf__free(vmlinux_btf);
}

void test_btf_dedup_split()
{
	if (test__start_subtest("split_simple"))
		test_split_simple();
	if (test__start_subtest("split_struct_duped"))
		test_split_struct_duped();
	if (test__start_subtest("split_fwd_resolve"))
		test_split_fwd_resolve();
	if (test__start_subtest("split_dup_struct_in_cu"))
		test_split_dup_struct_in_cu();
	if (test__start_subtest("split_module"))
		test_split_module();
}
