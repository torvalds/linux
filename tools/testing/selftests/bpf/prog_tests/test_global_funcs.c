// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#include <test_progs.h>

const char *err_str;
bool found;

static int libbpf_debug_print(enum libbpf_print_level level,
			      const char *format, va_list args)
{
	char *log_buf;

	if (level != LIBBPF_WARN ||
	    strcmp(format, "libbpf: \n%s\n")) {
		vprintf(format, args);
		return 0;
	}

	log_buf = va_arg(args, char *);
	if (!log_buf)
		goto out;
	if (err_str && strstr(log_buf, err_str) == 0)
		found = true;
out:
	printf(format, log_buf);
	return 0;
}

extern int extra_prog_load_log_flags;

static int check_load(const char *file)
{
	struct bpf_prog_load_attr attr;
	struct bpf_object *obj = NULL;
	int err, prog_fd;

	memset(&attr, 0, sizeof(struct bpf_prog_load_attr));
	attr.file = file;
	attr.prog_type = BPF_PROG_TYPE_UNSPEC;
	attr.log_level = extra_prog_load_log_flags;
	attr.prog_flags = BPF_F_TEST_RND_HI32;
	found = false;
	err = bpf_prog_load_xattr(&attr, &obj, &prog_fd);
	bpf_object__close(obj);
	return err;
}

struct test_def {
	const char *file;
	const char *err_str;
};

void test_test_global_funcs(void)
{
	struct test_def tests[] = {
		{ "test_global_func1.o", "combined stack size of 4 calls is 544" },
		{ "test_global_func2.o" },
		{ "test_global_func3.o" , "the call stack of 8 frames" },
		{ "test_global_func4.o" },
		{ "test_global_func5.o" , "expected pointer to ctx, but got PTR" },
		{ "test_global_func6.o" , "modified ctx ptr R2" },
		{ "test_global_func7.o" , "foo() doesn't return scalar" },
	};
	libbpf_print_fn_t old_print_fn = NULL;
	int err, i, duration = 0;

	old_print_fn = libbpf_set_print(libbpf_debug_print);

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		const struct test_def *test = &tests[i];

		if (!test__start_subtest(test->file))
			continue;

		err_str = test->err_str;
		err = check_load(test->file);
		CHECK_FAIL(!!err ^ !!err_str);
		if (err_str)
			CHECK(found, "", "expected string '%s'", err_str);
	}
	libbpf_set_print(old_print_fn);
}
