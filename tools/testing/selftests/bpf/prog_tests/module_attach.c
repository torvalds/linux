// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */

#include <test_progs.h>
#include <stdbool.h>
#include "test_module_attach.skel.h"
#include "testing_helpers.h"

static const char * const read_tests[] = {
	"handle_raw_tp",
	"handle_tp_btf",
	"handle_fentry",
	"handle_fentry_explicit",
	"handle_fmod_ret",
};

static const char * const detach_tests[] = {
	"handle_fentry",
	"handle_fexit",
	"kprobe_multi",
};

static const int READ_SZ = 456;
static const int WRITE_SZ = 457;

static int trigger_module_test_writable(int *val)
{
	int fd, err;
	char buf[65];
	ssize_t rd;

	fd = open(BPF_TESTMOD_TEST_FILE, O_RDONLY);
	err = -errno;
	if (!ASSERT_GE(fd, 0, "testmode_file_open"))
		return err;

	rd = read(fd, buf, sizeof(buf) - 1);
	err = -errno;
	if (!ASSERT_GT(rd, 0, "testmod_file_rd_val")) {
		close(fd);
		return err;
	}

	buf[rd] = '\0';
	*val = strtol(buf, NULL, 0);
	close(fd);

	return 0;
}

static void test_module_attach_prog(const char *prog_name, int sz,
				    const char *attach_target, int ret)
{
	struct test_module_attach *skel;
	struct bpf_program *prog;
	int err;

	skel = test_module_attach__open();
	if (!ASSERT_OK_PTR(skel, "module_attach open"))
		return;

	prog = bpf_object__find_program_by_name(skel->obj, prog_name);
	if (!ASSERT_OK_PTR(prog, "module_attach find_program"))
		goto cleanup;
	bpf_program__set_autoload(prog, true);

	if (attach_target) {
		err = bpf_program__set_attach_target(prog, 0, attach_target);
		if (!ASSERT_OK(err, attach_target))
			goto cleanup;
	}

	err = test_module_attach__load(skel);
	if (!ASSERT_OK(err, "module_attach load"))
		goto cleanup;

	err = test_module_attach__attach(skel);
	if (!ASSERT_OK(err, "module_attach attach"))
		goto cleanup;

	if (sz) {
		/* trigger both read and write though each test uses only one */
		ASSERT_OK(trigger_module_test_read(sz), "trigger_read");
		ASSERT_OK(trigger_module_test_write(sz), "trigger_write");

		ASSERT_EQ(skel->bss->sz, sz, prog_name);
	}

	if (ret)
		ASSERT_EQ(skel->bss->retval, ret, "ret");
cleanup:
	test_module_attach__destroy(skel);
}

static void test_module_attach_writable(void)
{
	struct test_module_attach__bss *bss;
	struct test_module_attach *skel;
	int writable_val = 0;
	int err;

	skel = test_module_attach__open();
	if (!ASSERT_OK_PTR(skel, "module_attach open"))
		return;

	bpf_program__set_autoload(skel->progs.handle_raw_tp_writable_bare, true);

	err = test_module_attach__load(skel);
	if (!ASSERT_OK(err, "module_attach load"))
		goto cleanup;

	bss = skel->bss;

	err = test_module_attach__attach(skel);
	if (!ASSERT_OK(err, "module_attach attach"))
		goto cleanup;

	bss->raw_tp_writable_bare_early_ret = true;
	bss->raw_tp_writable_bare_out_val = 0xf1f2f3f4;
	ASSERT_OK(trigger_module_test_writable(&writable_val),
		  "trigger_writable");
	ASSERT_EQ(bss->raw_tp_writable_bare_in_val, 1024, "writable_test_in");
	ASSERT_EQ(bss->raw_tp_writable_bare_out_val, writable_val,
		  "writable_test_out");
cleanup:
	test_module_attach__destroy(skel);
}

static void test_module_attach_detach(const char *prog_name)
{
	struct test_module_attach *skel;
	struct bpf_program *prog;
	struct bpf_link *link;
	int err;

	skel = test_module_attach__open();
	if (!ASSERT_OK_PTR(skel, "module_attach open"))
		return;

	prog = bpf_object__find_program_by_name(skel->obj, prog_name);
	if (!ASSERT_OK_PTR(prog, "module_attach find_program"))
		goto cleanup;
	bpf_program__set_autoload(prog, true);

	err = test_module_attach__load(skel);
	if (!ASSERT_OK(err, "module_attach load"))
		goto cleanup;

	/* attach and make sure it gets module reference */
	link = bpf_program__attach(prog);
	if (!ASSERT_OK_PTR(link, "module_attach attach"))
		goto cleanup;

	ASSERT_ERR(unload_bpf_testmod(false), "unload_bpf_testmod");
	bpf_link__destroy(link);
cleanup:
	test_module_attach__destroy(skel);
}

void test_module_attach(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(read_tests); i++) {
		if (!test__start_subtest(read_tests[i]))
			continue;
		test_module_attach_prog(read_tests[i], READ_SZ, NULL, 0);
	}
	if (test__start_subtest("handle_raw_tp_bare"))
		test_module_attach_prog("handle_raw_tp_bare", WRITE_SZ, NULL, 0);
	if (test__start_subtest("handle_raw_tp_writable_bare"))
		test_module_attach_writable();
	if (test__start_subtest("handle_fentry_manual")) {
		test_module_attach_prog("handle_fentry_manual", READ_SZ,
					"bpf_testmod_test_read", 0);
	}
	if (test__start_subtest("handle_fentry_explicit_manual")) {
		test_module_attach_prog("handle_fentry_explicit_manual",
					READ_SZ,
					"bpf_testmod:bpf_testmod_test_read", 0);
	}
	if (test__start_subtest("handle_fexit"))
		test_module_attach_prog("handle_fexit", READ_SZ, NULL, -EIO);
	if (test__start_subtest("handle_fexit_ret"))
		test_module_attach_prog("handle_fexit_ret", 0, NULL, 0);
	for (i = 0; i < ARRAY_SIZE(detach_tests); i++) {
		char test_name[50];

		snprintf(test_name, sizeof(test_name), "%s_detach", detach_tests[i]);
		if (!test__start_subtest(test_name))
			continue;
		test_module_attach_detach(detach_tests[i]);
	}
}
