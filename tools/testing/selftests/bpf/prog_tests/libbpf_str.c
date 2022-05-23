// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#include <ctype.h>
#include <test_progs.h>
#include <bpf/btf.h>

/*
 * Utility function uppercasing an entire string.
 */
static void uppercase(char *s)
{
	for (; *s != '\0'; s++)
		*s = toupper(*s);
}

/*
 * Test case to check that all bpf_attach_type variants are covered by
 * libbpf_bpf_attach_type_str.
 */
static void test_libbpf_bpf_attach_type_str(void)
{
	struct btf *btf;
	const struct btf_type *t;
	const struct btf_enum *e;
	int i, n, id;

	btf = btf__parse("/sys/kernel/btf/vmlinux", NULL);
	if (!ASSERT_OK_PTR(btf, "btf_parse"))
		return;

	/* find enum bpf_attach_type and enumerate each value */
	id = btf__find_by_name_kind(btf, "bpf_attach_type", BTF_KIND_ENUM);
	if (!ASSERT_GT(id, 0, "bpf_attach_type_id"))
		goto cleanup;
	t = btf__type_by_id(btf, id);
	e = btf_enum(t);
	n = btf_vlen(t);
	for (i = 0; i < n; e++, i++) {
		enum bpf_attach_type attach_type = (enum bpf_attach_type)e->val;
		const char *attach_type_name;
		const char *attach_type_str;
		char buf[256];

		if (attach_type == __MAX_BPF_ATTACH_TYPE)
			continue;

		attach_type_name = btf__str_by_offset(btf, e->name_off);
		attach_type_str = libbpf_bpf_attach_type_str(attach_type);
		ASSERT_OK_PTR(attach_type_str, attach_type_name);

		snprintf(buf, sizeof(buf), "BPF_%s", attach_type_str);
		uppercase(buf);

		ASSERT_STREQ(buf, attach_type_name, "exp_str_value");
	}

cleanup:
	btf__free(btf);
}

/*
 * Test case to check that all bpf_map_type variants are covered by
 * libbpf_bpf_map_type_str.
 */
static void test_libbpf_bpf_map_type_str(void)
{
	struct btf *btf;
	const struct btf_type *t;
	const struct btf_enum *e;
	int i, n, id;

	btf = btf__parse("/sys/kernel/btf/vmlinux", NULL);
	if (!ASSERT_OK_PTR(btf, "btf_parse"))
		return;

	/* find enum bpf_map_type and enumerate each value */
	id = btf__find_by_name_kind(btf, "bpf_map_type", BTF_KIND_ENUM);
	if (!ASSERT_GT(id, 0, "bpf_map_type_id"))
		goto cleanup;
	t = btf__type_by_id(btf, id);
	e = btf_enum(t);
	n = btf_vlen(t);
	for (i = 0; i < n; e++, i++) {
		enum bpf_map_type map_type = (enum bpf_map_type)e->val;
		const char *map_type_name;
		const char *map_type_str;
		char buf[256];

		map_type_name = btf__str_by_offset(btf, e->name_off);
		map_type_str = libbpf_bpf_map_type_str(map_type);
		ASSERT_OK_PTR(map_type_str, map_type_name);

		snprintf(buf, sizeof(buf), "BPF_MAP_TYPE_%s", map_type_str);
		uppercase(buf);

		ASSERT_STREQ(buf, map_type_name, "exp_str_value");
	}

cleanup:
	btf__free(btf);
}

/*
 * Test case to check that all bpf_prog_type variants are covered by
 * libbpf_bpf_prog_type_str.
 */
static void test_libbpf_bpf_prog_type_str(void)
{
	struct btf *btf;
	const struct btf_type *t;
	const struct btf_enum *e;
	int i, n, id;

	btf = btf__parse("/sys/kernel/btf/vmlinux", NULL);
	if (!ASSERT_OK_PTR(btf, "btf_parse"))
		return;

	/* find enum bpf_prog_type and enumerate each value */
	id = btf__find_by_name_kind(btf, "bpf_prog_type", BTF_KIND_ENUM);
	if (!ASSERT_GT(id, 0, "bpf_prog_type_id"))
		goto cleanup;
	t = btf__type_by_id(btf, id);
	e = btf_enum(t);
	n = btf_vlen(t);
	for (i = 0; i < n; e++, i++) {
		enum bpf_prog_type prog_type = (enum bpf_prog_type)e->val;
		const char *prog_type_name;
		const char *prog_type_str;
		char buf[256];

		prog_type_name = btf__str_by_offset(btf, e->name_off);
		prog_type_str = libbpf_bpf_prog_type_str(prog_type);
		ASSERT_OK_PTR(prog_type_str, prog_type_name);

		snprintf(buf, sizeof(buf), "BPF_PROG_TYPE_%s", prog_type_str);
		uppercase(buf);

		ASSERT_STREQ(buf, prog_type_name, "exp_str_value");
	}

cleanup:
	btf__free(btf);
}

/*
 * Run all libbpf str conversion tests.
 */
void test_libbpf_str(void)
{
	if (test__start_subtest("bpf_attach_type_str"))
		test_libbpf_bpf_attach_type_str();

	if (test__start_subtest("bpf_map_type_str"))
		test_libbpf_bpf_map_type_str();

	if (test__start_subtest("bpf_prog_type_str"))
		test_libbpf_bpf_prog_type_str();
}
