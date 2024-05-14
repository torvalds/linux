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
	struct bpf_object *obj = NULL;
	struct bpf_program *prog;
	int err;

	found = false;

	obj = bpf_object__open_file(file, NULL);
	err = libbpf_get_error(obj);
	if (err)
		return err;

	prog = bpf_object__next_program(obj, NULL);
	if (!prog) {
		err = -ENOENT;
		goto err_out;
	}

	bpf_program__set_flags(prog, BPF_F_TEST_RND_HI32);
	bpf_program__set_log_level(prog, extra_prog_load_log_flags);

	err = bpf_object__load(obj);

err_out:
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
		{ "test_global_func1.bpf.o", "combined stack size of 4 calls is 544" },
		{ "test_global_func2.bpf.o" },
		{ "test_global_func3.bpf.o", "the call stack of 8 frames" },
		{ "test_global_func4.bpf.o" },
		{ "test_global_func5.bpf.o", "expected pointer to ctx, but got PTR" },
		{ "test_global_func6.bpf.o", "modified ctx ptr R2" },
		{ "test_global_func7.bpf.o", "foo() doesn't return scalar" },
		{ "test_global_func8.bpf.o" },
		{ "test_global_func9.bpf.o" },
		{ "test_global_func10.bpf.o", "invalid indirect read from stack" },
		{ "test_global_func11.bpf.o", "Caller passes invalid args into func#1" },
		{ "test_global_func12.bpf.o", "invalid mem access 'mem_or_null'" },
		{ "test_global_func13.bpf.o", "Caller passes invalid args into func#1" },
		{ "test_global_func14.bpf.o", "reference type('FWD S') size cannot be determined" },
		{ "test_global_func15.bpf.o", "At program exit the register R0 has value" },
		{ "test_global_func16.bpf.o", "invalid indirect read from stack" },
		{ "test_global_func17.bpf.o", "Caller passes invalid args into func#1" },
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
