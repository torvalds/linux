// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026, Oracle and/or its affiliates. */

#include <test_progs.h>
#include <bpf/btf.h>
#include <bpf/libbpf.h>

#define BTF_SYSFS_DIR		"/sys/kernel/btf"
#define BTF_INLINE_SUFFIX	".inline"

/*
 * For a specific inline site, verify we have the right function,
 * loc proto and loc param representation and that the offset is
 * reasonable given the caller where it was inlined.
 */
void test_btf_inline(void)
{
	struct btf *inline_btf = NULL, *btf = NULL, *vmlinux_btf = NULL;
	const char *inline_caller = "bpf_testmod_test_write";
	const char *inline_func = "testmod_inline_len";
	unsigned long long caller_addr, base_addr;
	bool skip = false, found_loc = false;
	const struct btf_loc_param *lp;
	int locsec_id, func_id, n, i;
	const struct btf_type *t;
	struct btf_loc *l;
	const __u32 *p;
	int err = 0;

	if (!env.has_testmod) {
		test__skip();
		return;
	}

	base_addr = module_get_base_addr("bpf_testmod");
	if (!ASSERT_NEQ(base_addr, 0, "base_addr_nonzero"))
		return;

	if (!ASSERT_OK(kallsyms_find(inline_caller, &caller_addr),
		       "find_inline_caller"))
		return;
	if (!ASSERT_NEQ(caller_addr, 0, "caller_addr_nonzero"))
		return;
	if (!ASSERT_GT(caller_addr, base_addr, "caller_addr_gt_base_addr"))
		return;
	caller_addr -= base_addr;

	vmlinux_btf = btf__load_vmlinux_btf();
	if (!ASSERT_OK_PTR(vmlinux_btf, "vmlinux_btf"))
		return;

	btf = btf__parse_split(BTF_SYSFS_DIR "/bpf_testmod", vmlinux_btf);
	if (!ASSERT_OK_PTR(btf, "bpf_testmod_btf"))
		goto out;

	inline_btf = btf__parse_split(BTF_SYSFS_DIR "/bpf_testmod" BTF_INLINE_SUFFIX,
				      btf);
	err = libbpf_get_error(inline_btf);
	/* pahole may not have inline BTF feature support. */
	if (err == -ENOENT) {
		skip = true;
		goto out;
	}
	if (!ASSERT_OK(err, "parse_inline_btf"))
		goto out;
	locsec_id = btf__find_by_name_kind(inline_btf, "inline.text", BTF_KIND_LOCSEC);
	if (locsec_id < 0) {
		skip = true;
		goto out;
	}
	func_id = btf__find_by_name_kind(inline_btf, inline_func, BTF_KIND_FUNC);
	if (!ASSERT_GT(func_id, 0, "inline_caller_func"))
		goto out;
	t = btf__type_by_id(inline_btf, locsec_id);
	n = btf_vlen(t);
	for (i = 0, l = btf_locsec_locs(t); i < n; i++, l++) {
		if (l->func == func_id) {
			found_loc = true;
			break;
		}
	}
	if (!ASSERT_TRUE(found_loc, "found_loc"))
		goto out;
	if (!ASSERT_GT(l->loc_proto, 0, "loc_proto_id"))
		goto out;
	if (!ASSERT_GT(l->offset, 0, "loc_offset"))
		goto out;
	t = btf__type_by_id(inline_btf, l->loc_proto);
	if (!ASSERT_OK_PTR(t, "loc_proto_ptr"))
		goto out;
	if (!ASSERT_EQ(btf_vlen(t), 1, "loc_proto_one_param"))
		goto out;
	p = btf_loc_proto_params(t);
	if (!ASSERT_GT(*p, 0, "loc_proto_has_loc_param"))
		goto out;
	t = btf__type_by_id(inline_btf, *p);
	if (!ASSERT_OK_PTR(t, "loc_param_ptr"))
		goto out;
	lp = btf_loc_param(t);
	if (!ASSERT_TRUE(lp->flags & BTF_LOC_PARAM_REG, "param_has_reg"))
		goto out;
	if (!ASSERT_GT(l->offset, caller_addr, "inline_gt_caller"))
		goto out;
	/* simple sanity test to roughly ensure inline site still in function */
	if (!ASSERT_LT(l->offset, caller_addr + 512, "inline_in_caller"))
		goto out;
out:
	btf__free(inline_btf);
	btf__free(btf);
	btf__free(vmlinux_btf);
	if (skip)
		test__skip();
}
