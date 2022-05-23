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
 * Test case to check that all bpf_prog_type variants are covered by
 * libbpf_bpf_prog_type_str.
 */
void test_libbpf_bpf_prog_type_str(void)
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
