#include <stdio.h>
#include <bpf/libbpf.h>
#include <util/llvm-utils.h>
#include <util/cache.h>
#include "llvm.h"
#include "tests.h"
#include "debug.h"

static int perf_config_cb(const char *var, const char *val,
			  void *arg __maybe_unused)
{
	return perf_default_config(var, val, arg);
}

#ifdef HAVE_LIBBPF_SUPPORT
static int test__bpf_parsing(void *obj_buf, size_t obj_buf_sz)
{
	struct bpf_object *obj;

	obj = bpf_object__open_buffer(obj_buf, obj_buf_sz, NULL);
	if (IS_ERR(obj))
		return TEST_FAIL;
	bpf_object__close(obj);
	return TEST_OK;
}
#else
static int test__bpf_parsing(void *obj_buf __maybe_unused,
			     size_t obj_buf_sz __maybe_unused)
{
	pr_debug("Skip bpf parsing\n");
	return TEST_OK;
}
#endif

static struct {
	const char *source;
	const char *desc;
} bpf_source_table[__LLVM_TESTCASE_MAX] = {
	[LLVM_TESTCASE_BASE] = {
		.source = test_llvm__bpf_base_prog,
		.desc = "Basic BPF llvm compiling test",
	},
	[LLVM_TESTCASE_KBUILD] = {
		.source = test_llvm__bpf_test_kbuild_prog,
		.desc = "Test kbuild searching",
	},
};


int
test_llvm__fetch_bpf_obj(void **p_obj_buf,
			 size_t *p_obj_buf_sz,
			 enum test_llvm__testcase idx,
			 bool force)
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

	perf_config(perf_config_cb, NULL);

	/*
	 * Skip this test if user's .perfconfig doesn't set [llvm] section
	 * and clang is not found in $PATH, and this is not perf test -v
	 */
	if (!force && (verbose == 0 &&
		       !llvm_param.user_set_param &&
		       llvm__search_clang())) {
		pr_debug("No clang and no verbosive, skip this test\n");
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

int test__llvm(void)
{
	enum test_llvm__testcase i;

	for (i = 0; i < __LLVM_TESTCASE_MAX; i++) {
		int ret;
		void *obj_buf = NULL;
		size_t obj_buf_sz = 0;

		ret = test_llvm__fetch_bpf_obj(&obj_buf, &obj_buf_sz,
					       i, false);

		if (ret == TEST_OK) {
			ret = test__bpf_parsing(obj_buf, obj_buf_sz);
			if (ret != TEST_OK)
				pr_debug("Failed to parse test case '%s'\n",
					 bpf_source_table[i].desc);
		}
		free(obj_buf);

		switch (ret) {
		case TEST_SKIP:
			return TEST_SKIP;
		case TEST_OK:
			break;
		default:
			/*
			 * Test 0 is the basic LLVM test. If test 0
			 * fail, the basic LLVM support not functional
			 * so the whole test should fail. If other test
			 * case fail, it can be fixed by adjusting
			 * config so don't report error.
			 */
			if (i == 0)
				return TEST_FAIL;
			else
				return TEST_SKIP;
		}
	}
	return TEST_OK;
}
