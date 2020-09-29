// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#include <test_progs.h>
#include <bpf/btf.h>

#define ASSERT_EQ(actual, expected, name) ({				\
	typeof(actual) ___act = (actual);				\
	typeof(expected) ___exp = (expected);				\
	bool ___ok = ___act == ___exp;					\
	CHECK(!___ok, (name),						\
	      "unexpected %s: actual %lld != expected %lld\n",		\
	      (name), (long long)(___act), (long long)(___exp));	\
	___ok;								\
})

#define ASSERT_STREQ(actual, expected, name) ({				\
	const char *___act = actual;					\
	const char *___exp = expected;					\
	bool ___ok = strcmp(___act, ___exp) == 0;			\
	CHECK(!___ok, (name),						\
	      "unexpected %s: actual '%s' != expected '%s'\n",		\
	      (name), ___act, ___exp);					\
	___ok;								\
})

#define ASSERT_OK(res, name) ({						\
	long long ___res = (res);					\
	bool ___ok = ___res == 0;					\
	CHECK(!___ok, (name), "unexpected error: %lld\n", ___res);	\
	___ok;								\
})

#define ASSERT_ERR(res, name) ({					\
	long long ___res = (res);					\
	bool ___ok = ___res < 0;					\
	CHECK(!___ok, (name), "unexpected success: %lld\n", ___res);	\
	___ok;								\
})

static int duration = 0;

void test_btf_write() {
	const struct btf_var_secinfo *vi;
	const struct btf_type *t;
	const struct btf_member *m;
	const struct btf_enum *v;
	const struct btf_param *p;
	struct btf *btf;
	int id, err, str_off;

	btf = btf__new_empty();
	if (CHECK(IS_ERR(btf), "new_empty", "failed: %ld\n", PTR_ERR(btf)))
		return;

	str_off = btf__find_str(btf, "int");
	ASSERT_EQ(str_off, -ENOENT, "int_str_missing_off");

	str_off = btf__add_str(btf, "int");
	ASSERT_EQ(str_off, 1, "int_str_off");

	str_off = btf__find_str(btf, "int");
	ASSERT_EQ(str_off, 1, "int_str_found_off");

	/* BTF_KIND_INT */
	id = btf__add_int(btf, "int", 4,  BTF_INT_SIGNED);
	ASSERT_EQ(id, 1, "int_id");

	t = btf__type_by_id(btf, 1);
	/* should re-use previously added "int" string */
	ASSERT_EQ(t->name_off, str_off, "int_name_off");
	ASSERT_STREQ(btf__str_by_offset(btf, t->name_off), "int", "int_name");
	ASSERT_EQ(btf_kind(t), BTF_KIND_INT, "int_kind");
	ASSERT_EQ(t->size, 4, "int_sz");
	ASSERT_EQ(btf_int_encoding(t), BTF_INT_SIGNED, "int_enc");
	ASSERT_EQ(btf_int_bits(t), 32, "int_bits");

	/* invalid int size */
	id = btf__add_int(btf, "bad sz int", 7, 0);
	ASSERT_ERR(id, "int_bad_sz");
	/* invalid encoding */
	id = btf__add_int(btf, "bad enc int", 4, 123);
	ASSERT_ERR(id, "int_bad_enc");
	/* NULL name */
	id = btf__add_int(btf, NULL, 4, 0);
	ASSERT_ERR(id, "int_bad_null_name");
	/* empty name */
	id = btf__add_int(btf, "", 4, 0);
	ASSERT_ERR(id, "int_bad_empty_name");

	/* PTR/CONST/VOLATILE/RESTRICT */
	id = btf__add_ptr(btf, 1);
	ASSERT_EQ(id, 2, "ptr_id");
	t = btf__type_by_id(btf, 2);
	ASSERT_EQ(btf_kind(t), BTF_KIND_PTR, "ptr_kind");
	ASSERT_EQ(t->type, 1, "ptr_type");

	id = btf__add_const(btf, 5); /* points forward to restrict */
	ASSERT_EQ(id, 3, "const_id");
	t = btf__type_by_id(btf, 3);
	ASSERT_EQ(btf_kind(t), BTF_KIND_CONST, "const_kind");
	ASSERT_EQ(t->type, 5, "const_type");

	id = btf__add_volatile(btf, 3);
	ASSERT_EQ(id, 4, "volatile_id");
	t = btf__type_by_id(btf, 4);
	ASSERT_EQ(btf_kind(t), BTF_KIND_VOLATILE, "volatile_kind");
	ASSERT_EQ(t->type, 3, "volatile_type");

	id = btf__add_restrict(btf, 4);
	ASSERT_EQ(id, 5, "restrict_id");
	t = btf__type_by_id(btf, 5);
	ASSERT_EQ(btf_kind(t), BTF_KIND_RESTRICT, "restrict_kind");
	ASSERT_EQ(t->type, 4, "restrict_type");

	/* ARRAY */
	id = btf__add_array(btf, 1, 2, 10); /* int *[10] */
	ASSERT_EQ(id, 6, "array_id");
	t = btf__type_by_id(btf, 6);
	ASSERT_EQ(btf_kind(t), BTF_KIND_ARRAY, "array_kind");
	ASSERT_EQ(btf_array(t)->index_type, 1, "array_index_type");
	ASSERT_EQ(btf_array(t)->type, 2, "array_elem_type");
	ASSERT_EQ(btf_array(t)->nelems, 10, "array_nelems");

	/* STRUCT */
	err = btf__add_field(btf, "field", 1, 0, 0);
	ASSERT_ERR(err, "no_struct_field");
	id = btf__add_struct(btf, "s1", 8);
	ASSERT_EQ(id, 7, "struct_id");
	err = btf__add_field(btf, "f1", 1, 0, 0);
	ASSERT_OK(err, "f1_res");
	err = btf__add_field(btf, "f2", 1, 32, 16);
	ASSERT_OK(err, "f2_res");

	t = btf__type_by_id(btf, 7);
	ASSERT_STREQ(btf__str_by_offset(btf, t->name_off), "s1", "struct_name");
	ASSERT_EQ(btf_kind(t), BTF_KIND_STRUCT, "struct_kind");
	ASSERT_EQ(btf_vlen(t), 2, "struct_vlen");
	ASSERT_EQ(btf_kflag(t), true, "struct_kflag");
	ASSERT_EQ(t->size, 8, "struct_sz");
	m = btf_members(t) + 0;
	ASSERT_STREQ(btf__str_by_offset(btf, m->name_off), "f1", "f1_name");
	ASSERT_EQ(m->type, 1, "f1_type");
	ASSERT_EQ(btf_member_bit_offset(t, 0), 0, "f1_bit_off");
	ASSERT_EQ(btf_member_bitfield_size(t, 0), 0, "f1_bit_sz");
	m = btf_members(t) + 1;
	ASSERT_STREQ(btf__str_by_offset(btf, m->name_off), "f2", "f2_name");
	ASSERT_EQ(m->type, 1, "f2_type");
	ASSERT_EQ(btf_member_bit_offset(t, 1), 32, "f2_bit_off");
	ASSERT_EQ(btf_member_bitfield_size(t, 1), 16, "f2_bit_sz");

	/* UNION */
	id = btf__add_union(btf, "u1", 8);
	ASSERT_EQ(id, 8, "union_id");

	/* invalid, non-zero offset */
	err = btf__add_field(btf, "field", 1, 1, 0);
	ASSERT_ERR(err, "no_struct_field");

	err = btf__add_field(btf, "f1", 1, 0, 16);
	ASSERT_OK(err, "f1_res");

	t = btf__type_by_id(btf, 8);
	ASSERT_STREQ(btf__str_by_offset(btf, t->name_off), "u1", "union_name");
	ASSERT_EQ(btf_kind(t), BTF_KIND_UNION, "union_kind");
	ASSERT_EQ(btf_vlen(t), 1, "union_vlen");
	ASSERT_EQ(btf_kflag(t), true, "union_kflag");
	ASSERT_EQ(t->size, 8, "union_sz");
	m = btf_members(t) + 0;
	ASSERT_STREQ(btf__str_by_offset(btf, m->name_off), "f1", "f1_name");
	ASSERT_EQ(m->type, 1, "f1_type");
	ASSERT_EQ(btf_member_bit_offset(t, 0), 0, "f1_bit_off");
	ASSERT_EQ(btf_member_bitfield_size(t, 0), 16, "f1_bit_sz");

	/* ENUM */
	id = btf__add_enum(btf, "e1", 4);
	ASSERT_EQ(id, 9, "enum_id");
	err = btf__add_enum_value(btf, "v1", 1);
	ASSERT_OK(err, "v1_res");
	err = btf__add_enum_value(btf, "v2", 2);
	ASSERT_OK(err, "v2_res");

	t = btf__type_by_id(btf, 9);
	ASSERT_STREQ(btf__str_by_offset(btf, t->name_off), "e1", "enum_name");
	ASSERT_EQ(btf_kind(t), BTF_KIND_ENUM, "enum_kind");
	ASSERT_EQ(btf_vlen(t), 2, "enum_vlen");
	ASSERT_EQ(t->size, 4, "enum_sz");
	v = btf_enum(t) + 0;
	ASSERT_STREQ(btf__str_by_offset(btf, v->name_off), "v1", "v1_name");
	ASSERT_EQ(v->val, 1, "v1_val");
	v = btf_enum(t) + 1;
	ASSERT_STREQ(btf__str_by_offset(btf, v->name_off), "v2", "v2_name");
	ASSERT_EQ(v->val, 2, "v2_val");

	/* FWDs */
	id = btf__add_fwd(btf, "struct_fwd", BTF_FWD_STRUCT);
	ASSERT_EQ(id, 10, "struct_fwd_id");
	t = btf__type_by_id(btf, 10);
	ASSERT_STREQ(btf__str_by_offset(btf, t->name_off), "struct_fwd", "fwd_name");
	ASSERT_EQ(btf_kind(t), BTF_KIND_FWD, "fwd_kind");
	ASSERT_EQ(btf_kflag(t), 0, "fwd_kflag");

	id = btf__add_fwd(btf, "union_fwd", BTF_FWD_UNION);
	ASSERT_EQ(id, 11, "union_fwd_id");
	t = btf__type_by_id(btf, 11);
	ASSERT_STREQ(btf__str_by_offset(btf, t->name_off), "union_fwd", "fwd_name");
	ASSERT_EQ(btf_kind(t), BTF_KIND_FWD, "fwd_kind");
	ASSERT_EQ(btf_kflag(t), 1, "fwd_kflag");

	id = btf__add_fwd(btf, "enum_fwd", BTF_FWD_ENUM);
	ASSERT_EQ(id, 12, "enum_fwd_id");
	t = btf__type_by_id(btf, 12);
	ASSERT_STREQ(btf__str_by_offset(btf, t->name_off), "enum_fwd", "fwd_name");
	ASSERT_EQ(btf_kind(t), BTF_KIND_ENUM, "enum_fwd_kind");
	ASSERT_EQ(btf_vlen(t), 0, "enum_fwd_kind");
	ASSERT_EQ(t->size, 4, "enum_fwd_sz");

	/* TYPEDEF */
	id = btf__add_typedef(btf, "typedef1", 1);
	ASSERT_EQ(id, 13, "typedef_fwd_id");
	t = btf__type_by_id(btf, 13);
	ASSERT_STREQ(btf__str_by_offset(btf, t->name_off), "typedef1", "typedef_name");
	ASSERT_EQ(btf_kind(t), BTF_KIND_TYPEDEF, "typedef_kind");
	ASSERT_EQ(t->type, 1, "typedef_type");

	/* FUNC & FUNC_PROTO */
	id = btf__add_func(btf, "func1", BTF_FUNC_GLOBAL, 15);
	ASSERT_EQ(id, 14, "func_id");
	t = btf__type_by_id(btf, 14);
	ASSERT_STREQ(btf__str_by_offset(btf, t->name_off), "func1", "func_name");
	ASSERT_EQ(t->type, 15, "func_type");
	ASSERT_EQ(btf_kind(t), BTF_KIND_FUNC, "func_kind");
	ASSERT_EQ(btf_vlen(t), BTF_FUNC_GLOBAL, "func_vlen");

	id = btf__add_func_proto(btf, 1);
	ASSERT_EQ(id, 15, "func_proto_id");
	err = btf__add_func_param(btf, "p1", 1);
	ASSERT_OK(err, "p1_res");
	err = btf__add_func_param(btf, "p2", 2);
	ASSERT_OK(err, "p2_res");

	t = btf__type_by_id(btf, 15);
	ASSERT_EQ(btf_kind(t), BTF_KIND_FUNC_PROTO, "func_proto_kind");
	ASSERT_EQ(btf_vlen(t), 2, "func_proto_vlen");
	ASSERT_EQ(t->type, 1, "func_proto_ret_type");
	p = btf_params(t) + 0;
	ASSERT_STREQ(btf__str_by_offset(btf, p->name_off), "p1", "p1_name");
	ASSERT_EQ(p->type, 1, "p1_type");
	p = btf_params(t) + 1;
	ASSERT_STREQ(btf__str_by_offset(btf, p->name_off), "p2", "p2_name");
	ASSERT_EQ(p->type, 2, "p2_type");

	/* VAR */
	id = btf__add_var(btf, "var1", BTF_VAR_GLOBAL_ALLOCATED, 1);
	ASSERT_EQ(id, 16, "var_id");
	t = btf__type_by_id(btf, 16);
	ASSERT_STREQ(btf__str_by_offset(btf, t->name_off), "var1", "var_name");
	ASSERT_EQ(btf_kind(t), BTF_KIND_VAR, "var_kind");
	ASSERT_EQ(t->type, 1, "var_type");
	ASSERT_EQ(btf_var(t)->linkage, BTF_VAR_GLOBAL_ALLOCATED, "var_type");

	/* DATASECT */
	id = btf__add_datasec(btf, "datasec1", 12);
	ASSERT_EQ(id, 17, "datasec_id");
	err = btf__add_datasec_var_info(btf, 1, 4, 8);
	ASSERT_OK(err, "v1_res");

	t = btf__type_by_id(btf, 17);
	ASSERT_STREQ(btf__str_by_offset(btf, t->name_off), "datasec1", "datasec_name");
	ASSERT_EQ(t->size, 12, "datasec_sz");
	ASSERT_EQ(btf_kind(t), BTF_KIND_DATASEC, "datasec_kind");
	ASSERT_EQ(btf_vlen(t), 1, "datasec_vlen");
	vi = btf_var_secinfos(t) + 0;
	ASSERT_EQ(vi->type, 1, "v1_type");
	ASSERT_EQ(vi->offset, 4, "v1_off");
	ASSERT_EQ(vi->size, 8, "v1_sz");

	btf__free(btf);
}
