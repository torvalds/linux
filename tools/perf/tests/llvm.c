// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tests.h"
#include "debug.h"

#ifdef HAVE_LIBBPF_SUPPORT
#include <bpf/libbpf.h>
#include <util/llvm-utils.h>
#include "llvm.h"
static int test__bpf_parsing(void *obj_buf, size_t obj_buf_sz)
{
	struct bpf_object *obj;

	obj = bpf_object__open_mem(obj_buf, obj_buf_sz, NULL);
	if (libbpf_get_error(obj))
		return TEST_FAIL;
	bpf_object__close(obj);
	return TEST_OK;
}

static struct {
	const char *source;
	const char *desc;
	bool should_load_fail;
} bpf_source_table[__LLVM_TESTCASE_MAX] = {
	[LLVM_TESTCASE_BASE] = {
		.source = test_llvm__bpf_base_prog,
		.desc = "Basic BPF llvm compile",
	},
	[LLVM_TESTCASE_KBUILD] = {
		.source = test_llvm__bpf_test_kbuild_prog,
		.desc = "kbuild searching",
	},
	[LLVM_TESTCASE_BPF_PROLOGUE] = {
		.source = test_llvm__bpf_test_prologue_prog,
		.desc = "Compile source for BPF prologue generation",
	},
	[LLVM_TESTCASE_BPF_RELOCATION] = {
		.source = test_llvm__bpf_test_relocation,
		.desc = "Compile source for BPF relocation",
		.should_load_fail = true,
	},
};

int
test_llvm__fetch_bpf_obj(void **p_obj_buf,
			 size_t *p_obj_buf_sz,
			 enum test_llvm__testcase idx,
			 bool force,
			 bool *should_load_fail)
{
	const char *source;
	const char *desc;
	const char *tmpl_old, *clang_opt_old;
	char *tmpl_new = NULL, *clang_opt_new = NULL;
	int err, old_verbose, ret = TEST_FAIL;

	if (idx >= __LLVM_TESTCASE_MAX)
		return TEST_FAIL;

	source = bpf_source_table[idx].source;
	desc = bpf_source_table[idx].desc;
	if (should_load_fail)
		*should_load_fail = bpf_source_table[idx].should_load_fail;

	/*
	 * Skip this test if user's .perfconfig doesn't set [llvm] section
	 * and clang is not found in $PATH
	 */
	if (!force && (!llvm_param.user_set_param &&
		       llvm__search_clang())) {
		pr_debug("No clang, skip this test\n");
		return TEST_SKIP;
	}

	/*
	 * llvm is verbosity when error. Suppress all error output if
	 * not 'perf test -v'.
	 */
	old_verbose = verbose;
	if (verbose == 0)
		verbose = -1;

	*p_obj_buf = NULL;
	*p_obj_buf_sz = 0;

	if (!llvm_param.clang_bpf_cmd_template)
		goto out;

	if (!llvm_param.clang_opt)
		llvm_param.clang_opt = strdup("");

	err = asprintf(&tmpl_new, "echo '%s' | %s%s", source,
		       llvm_param.clang_bpf_cmd_template,
		       old_verbose ? "" : " 2>/dev/null");
	if (err < 0)
		goto out;
	err = asprintf(&clang_opt_new, "-xc %s", llvm_param.clang_opt);
	if (err < 0)
		goto out;

	tmpl_old = llvm_param.clang_bpf_cmd_template;
	llvm_param.clang_bpf_cmd_template = tmpl_new;
	clang_opt_old = llvm_param.clang_opt;
	llvm_param.clang_opt = clang_opt_new;

	err = llvm__compile_bpf("-", p_obj_buf, p_obj_buf_sz);

	llvm_param.clang_bpf_cmd_template = tmpl_old;
	llvm_param.clang_opt = clang_opt_old;

	verbose = old_verbose;
	if (err)
		goto out;

	ret = TEST_OK;
out:
	free(tmpl_new);
	free(clang_opt_new);
	if (ret != TEST_OK)
		pr_debug("Failed to compile test case: '%s'\n", desc);
	return ret;
}

static int test__llvm(int subtest)
{
	int ret;
	void *obj_buf = NULL;
	size_t obj_buf_sz = 0;
	bool should_load_fail = false;

	if ((subtest < 0) || (subtest >= __LLVM_TESTCASE_MAX))
		return TEST_FAIL;

	ret = test_llvm__fetch_bpf_obj(&obj_buf, &obj_buf_sz,
				       subtest, false, &should_load_fail);

	if (ret == TEST_OK && !should_load_fail) {
		ret = test__bpf_parsing(obj_buf, obj_buf_sz);
		if (ret != TEST_OK) {
			pr_debug("Failed to parse test case '%s'\n",
				 bpf_source_table[subtest].desc);
		}
	}
	free(obj_buf);

	return ret;
}
#endif //HAVE_LIBBPF_SUPPORT

static int test__llvm__bpf_base_prog(struct test_suite *test __maybe_unused,
				     int subtest __maybe_unused)
{
#ifdef HAVE_LIBBPF_SUPPORT
	return test__llvm(LLVM_TESTCASE_BASE);
#else
	pr_debug("Skip LLVM test because BPF support is not compiled\n");
	return TEST_SKIP;
#endif
}

static int test__llvm__bpf_test_kbuild_prog(struct test_suite *test __maybe_unused,
					    int subtest __maybe_unused)
{
#ifdef HAVE_LIBBPF_SUPPORT
	return test__llvm(LLVM_TESTCASE_KBUILD);
#else
	pr_debug("Skip LLVM test because BPF support is not compiled\n");
	return TEST_SKIP;
#endif
}

static int test__llvm__bpf_test_prologue_prog(struct test_suite *test __maybe_unused,
					      int subtest __maybe_unused)
{
#ifdef HAVE_LIBBPF_SUPPORT
	return test__llvm(LLVM_TESTCASE_BPF_PROLOGUE);
#else
	pr_debug("Skip LLVM test because BPF support is not compiled\n");
	return TEST_SKIP;
#endif
}

static int test__llvm__bpf_test_relocation(struct test_suite *test __maybe_unused,
					   int subtest __maybe_unused)
{
#ifdef HAVE_LIBBPF_SUPPORT
	return test__llvm(LLVM_TESTCASE_BPF_RELOCATION);
#else
	pr_debug("Skip LLVM test because BPF support is not compiled\n");
	return TEST_SKIP;
#endif
}


static struct test_case llvm_tests[] = {
#ifdef HAVE_LIBBPF_SUPPORT
	TEST_CASE("Basic BPF llvm compile", llvm__bpf_base_prog),
	TEST_CASE("kbuild searching", llvm__bpf_test_kbuild_prog),
	TEST_CASE("Compile source for BPF prologue generation",
		  llvm__bpf_test_prologue_prog),
	TEST_CASE("Compile source for BPF relocation", llvm__bpf_test_relocation),
#else
	TEST_CASE_REASON("Basic BPF llvm compile", llvm__bpf_base_prog, "not compiled in"),
	TEST_CASE_REASON("kbuild searching", llvm__bpf_test_kbuild_prog, "not compiled in"),
	TEST_CASE_REASON("Compile source for BPF prologue generation",
			llvm__bpf_test_prologue_prog, "not compiled in"),
	TEST_CASE_REASON("Compile source for BPF relocation",
			llvm__bpf_test_relocation, "not compiled in"),
#endif
	{ .name = NULL, }
};

struct test_suite suite__llvm = {
	.desc = "LLVM search and compile",
	.test_cases = llvm_tests,
};
