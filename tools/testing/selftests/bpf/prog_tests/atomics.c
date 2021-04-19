// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>

#include "atomics.skel.h"

static void test_add(struct atomics *skel)
{
	int err, prog_fd;
	__u32 duration = 0, retval;
	struct bpf_link *link;

	link = bpf_program__attach(skel->progs.add);
	if (CHECK(IS_ERR(link), "attach(add)", "err: %ld\n", PTR_ERR(link)))
		return;

	prog_fd = bpf_program__fd(skel->progs.add);
	err = bpf_prog_test_run(prog_fd, 1, NULL, 0,
				NULL, NULL, &retval, &duration);
	if (CHECK(err || retval, "test_run add",
		  "err %d errno %d retval %d duration %d\n", err, errno, retval, duration))
		goto cleanup;

	ASSERT_EQ(skel->data->add64_value, 3, "add64_value");
	ASSERT_EQ(skel->bss->add64_result, 1, "add64_result");

	ASSERT_EQ(skel->data->add32_value, 3, "add32_value");
	ASSERT_EQ(skel->bss->add32_result, 1, "add32_result");

	ASSERT_EQ(skel->bss->add_stack_value_copy, 3, "add_stack_value");
	ASSERT_EQ(skel->bss->add_stack_result, 1, "add_stack_result");

	ASSERT_EQ(skel->data->add_noreturn_value, 3, "add_noreturn_value");

cleanup:
	bpf_link__destroy(link);
}

static void test_sub(struct atomics *skel)
{
	int err, prog_fd;
	__u32 duration = 0, retval;
	struct bpf_link *link;

	link = bpf_program__attach(skel->progs.sub);
	if (CHECK(IS_ERR(link), "attach(sub)", "err: %ld\n", PTR_ERR(link)))
		return;

	prog_fd = bpf_program__fd(skel->progs.sub);
	err = bpf_prog_test_run(prog_fd, 1, NULL, 0,
				NULL, NULL, &retval, &duration);
	if (CHECK(err || retval, "test_run sub",
		  "err %d errno %d retval %d duration %d\n",
		  err, errno, retval, duration))
		goto cleanup;

	ASSERT_EQ(skel->data->sub64_value, -1, "sub64_value");
	ASSERT_EQ(skel->bss->sub64_result, 1, "sub64_result");

	ASSERT_EQ(skel->data->sub32_value, -1, "sub32_value");
	ASSERT_EQ(skel->bss->sub32_result, 1, "sub32_result");

	ASSERT_EQ(skel->bss->sub_stack_value_copy, -1, "sub_stack_value");
	ASSERT_EQ(skel->bss->sub_stack_result, 1, "sub_stack_result");

	ASSERT_EQ(skel->data->sub_noreturn_value, -1, "sub_noreturn_value");

cleanup:
	bpf_link__destroy(link);
}

static void test_and(struct atomics *skel)
{
	int err, prog_fd;
	__u32 duration = 0, retval;
	struct bpf_link *link;

	link = bpf_program__attach(skel->progs.and);
	if (CHECK(IS_ERR(link), "attach(and)", "err: %ld\n", PTR_ERR(link)))
		return;

	prog_fd = bpf_program__fd(skel->progs.and);
	err = bpf_prog_test_run(prog_fd, 1, NULL, 0,
				NULL, NULL, &retval, &duration);
	if (CHECK(err || retval, "test_run and",
		  "err %d errno %d retval %d duration %d\n", err, errno, retval, duration))
		goto cleanup;

	ASSERT_EQ(skel->data->and64_value, 0x010ull << 32, "and64_value");
	ASSERT_EQ(skel->bss->and64_result, 0x110ull << 32, "and64_result");

	ASSERT_EQ(skel->data->and32_value, 0x010, "and32_value");
	ASSERT_EQ(skel->bss->and32_result, 0x110, "and32_result");

	ASSERT_EQ(skel->data->and_noreturn_value, 0x010ull << 32, "and_noreturn_value");
cleanup:
	bpf_link__destroy(link);
}

static void test_or(struct atomics *skel)
{
	int err, prog_fd;
	__u32 duration = 0, retval;
	struct bpf_link *link;

	link = bpf_program__attach(skel->progs.or);
	if (CHECK(IS_ERR(link), "attach(or)", "err: %ld\n", PTR_ERR(link)))
		return;

	prog_fd = bpf_program__fd(skel->progs.or);
	err = bpf_prog_test_run(prog_fd, 1, NULL, 0,
				NULL, NULL, &retval, &duration);
	if (CHECK(err || retval, "test_run or",
		  "err %d errno %d retval %d duration %d\n",
		  err, errno, retval, duration))
		goto cleanup;

	ASSERT_EQ(skel->data->or64_value, 0x111ull << 32, "or64_value");
	ASSERT_EQ(skel->bss->or64_result, 0x110ull << 32, "or64_result");

	ASSERT_EQ(skel->data->or32_value, 0x111, "or32_value");
	ASSERT_EQ(skel->bss->or32_result, 0x110, "or32_result");

	ASSERT_EQ(skel->data->or_noreturn_value, 0x111ull << 32, "or_noreturn_value");
cleanup:
	bpf_link__destroy(link);
}

static void test_xor(struct atomics *skel)
{
	int err, prog_fd;
	__u32 duration = 0, retval;
	struct bpf_link *link;

	link = bpf_program__attach(skel->progs.xor);
	if (CHECK(IS_ERR(link), "attach(xor)", "err: %ld\n", PTR_ERR(link)))
		return;

	prog_fd = bpf_program__fd(skel->progs.xor);
	err = bpf_prog_test_run(prog_fd, 1, NULL, 0,
				NULL, NULL, &retval, &duration);
	if (CHECK(err || retval, "test_run xor",
		  "err %d errno %d retval %d duration %d\n", err, errno, retval, duration))
		goto cleanup;

	ASSERT_EQ(skel->data->xor64_value, 0x101ull << 32, "xor64_value");
	ASSERT_EQ(skel->bss->xor64_result, 0x110ull << 32, "xor64_result");

	ASSERT_EQ(skel->data->xor32_value, 0x101, "xor32_value");
	ASSERT_EQ(skel->bss->xor32_result, 0x110, "xor32_result");

	ASSERT_EQ(skel->data->xor_noreturn_value, 0x101ull << 32, "xor_nxoreturn_value");
cleanup:
	bpf_link__destroy(link);
}

static void test_cmpxchg(struct atomics *skel)
{
	int err, prog_fd;
	__u32 duration = 0, retval;
	struct bpf_link *link;

	link = bpf_program__attach(skel->progs.cmpxchg);
	if (CHECK(IS_ERR(link), "attach(cmpxchg)", "err: %ld\n", PTR_ERR(link)))
		return;

	prog_fd = bpf_program__fd(skel->progs.cmpxchg);
	err = bpf_prog_test_run(prog_fd, 1, NULL, 0,
				NULL, NULL, &retval, &duration);
	if (CHECK(err || retval, "test_run add",
		  "err %d errno %d retval %d duration %d\n", err, errno, retval, duration))
		goto cleanup;

	ASSERT_EQ(skel->data->cmpxchg64_value, 2, "cmpxchg64_value");
	ASSERT_EQ(skel->bss->cmpxchg64_result_fail, 1, "cmpxchg_result_fail");
	ASSERT_EQ(skel->bss->cmpxchg64_result_succeed, 1, "cmpxchg_result_succeed");

	ASSERT_EQ(skel->data->cmpxchg32_value, 2, "lcmpxchg32_value");
	ASSERT_EQ(skel->bss->cmpxchg32_result_fail, 1, "cmpxchg_result_fail");
	ASSERT_EQ(skel->bss->cmpxchg32_result_succeed, 1, "cmpxchg_result_succeed");

cleanup:
	bpf_link__destroy(link);
}

static void test_xchg(struct atomics *skel)
{
	int err, prog_fd;
	__u32 duration = 0, retval;
	struct bpf_link *link;

	link = bpf_program__attach(skel->progs.xchg);
	if (CHECK(IS_ERR(link), "attach(xchg)", "err: %ld\n", PTR_ERR(link)))
		return;

	prog_fd = bpf_program__fd(skel->progs.xchg);
	err = bpf_prog_test_run(prog_fd, 1, NULL, 0,
				NULL, NULL, &retval, &duration);
	if (CHECK(err || retval, "test_run add",
		  "err %d errno %d retval %d duration %d\n", err, errno, retval, duration))
		goto cleanup;

	ASSERT_EQ(skel->data->xchg64_value, 2, "xchg64_value");
	ASSERT_EQ(skel->bss->xchg64_result, 1, "xchg64_result");

	ASSERT_EQ(skel->data->xchg32_value, 2, "xchg32_value");
	ASSERT_EQ(skel->bss->xchg32_result, 1, "xchg32_result");

cleanup:
	bpf_link__destroy(link);
}

void test_atomics(void)
{
	struct atomics *skel;
	__u32 duration = 0;

	skel = atomics__open_and_load();
	if (CHECK(!skel, "skel_load", "atomics skeleton failed\n"))
		return;

	if (skel->data->skip_tests) {
		printf("%s:SKIP:no ENABLE_ATOMICS_TESTS (missing Clang BPF atomics support)",
		       __func__);
		test__skip();
		goto cleanup;
	}

	if (test__start_subtest("add"))
		test_add(skel);
	if (test__start_subtest("sub"))
		test_sub(skel);
	if (test__start_subtest("and"))
		test_and(skel);
	if (test__start_subtest("or"))
		test_or(skel);
	if (test__start_subtest("xor"))
		test_xor(skel);
	if (test__start_subtest("cmpxchg"))
		test_cmpxchg(skel);
	if (test__start_subtest("xchg"))
		test_xchg(skel);

cleanup:
	atomics__destroy(skel);
}
