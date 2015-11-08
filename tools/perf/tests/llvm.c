#include <stdio.h>
#include <bpf/libbpf.h>
#include <util/llvm-utils.h>
#include <util/cache.h>
#include "tests.h"
#include "debug.h"

static int perf_config_cb(const char *var, const char *val,
			  void *arg __maybe_unused)
{
	return perf_default_config(var, val, arg);
}

/*
 * Randomly give it a "version" section since we don't really load it
 * into kernel
 */
static const char test_bpf_prog[] =
	"__attribute__((section(\"do_fork\"), used)) "
	"int fork(void *ctx) {return 0;} "
	"char _license[] __attribute__((section(\"license\"), used)) = \"GPL\";"
	"int _version __attribute__((section(\"version\"), used)) = 0x40100;";

#ifdef HAVE_LIBBPF_SUPPORT
static int test__bpf_parsing(void *obj_buf, size_t obj_buf_sz)
{
	struct bpf_object *obj;

	obj = bpf_object__open_buffer(obj_buf, obj_buf_sz, NULL);
	if (!obj)
		return -1;
	bpf_object__close(obj);
	return 0;
}
#else
static int test__bpf_parsing(void *obj_buf __maybe_unused,
			     size_t obj_buf_sz __maybe_unused)
{
	fprintf(stderr, " (skip bpf parsing)");
	return 0;
}
#endif

int test__llvm(void)
{
	char *tmpl_new, *clang_opt_new;
	void *obj_buf;
	size_t obj_buf_sz;
	int err, old_verbose;

	perf_config(perf_config_cb, NULL);

	/*
	 * Skip this test if user's .perfconfig doesn't set [llvm] section
	 * and clang is not found in $PATH, and this is not perf test -v
	 */
	if (verbose == 0 && !llvm_param.user_set_param && llvm__search_clang()) {
		fprintf(stderr, " (no clang, try 'perf test -v LLVM')");
		return TEST_SKIP;
	}

	old_verbose = verbose;
	/*
	 * llvm is verbosity when error. Suppress all error output if
	 * not 'perf test -v'.
	 */
	if (verbose == 0)
		verbose = -1;

	if (!llvm_param.clang_bpf_cmd_template)
		return -1;

	if (!llvm_param.clang_opt)
		llvm_param.clang_opt = strdup("");

	err = asprintf(&tmpl_new, "echo '%s' | %s", test_bpf_prog,
		       llvm_param.clang_bpf_cmd_template);
	if (err < 0)
		return -1;
	err = asprintf(&clang_opt_new, "-xc %s", llvm_param.clang_opt);
	if (err < 0)
		return -1;

	llvm_param.clang_bpf_cmd_template = tmpl_new;
	llvm_param.clang_opt = clang_opt_new;
	err = llvm__compile_bpf("-", &obj_buf, &obj_buf_sz);

	verbose = old_verbose;
	if (err) {
		if (!verbose)
			fprintf(stderr, " (use -v to see error message)");
		return -1;
	}

	err = test__bpf_parsing(obj_buf, obj_buf_sz);
	free(obj_buf);
	return err;
}
