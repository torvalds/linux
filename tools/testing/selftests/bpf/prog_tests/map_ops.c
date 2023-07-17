// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <errno.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "test_map_ops.skel.h"
#include "test_progs.h"

static void map_update(void)
{
	(void)syscall(__NR_getpid);
}

static void map_delete(void)
{
	(void)syscall(__NR_getppid);
}

static void map_push(void)
{
	(void)syscall(__NR_getuid);
}

static void map_pop(void)
{
	(void)syscall(__NR_geteuid);
}

static void map_peek(void)
{
	(void)syscall(__NR_getgid);
}

static void map_for_each_pass(void)
{
	(void)syscall(__NR_gettid);
}

static void map_for_each_fail(void)
{
	(void)syscall(__NR_getpgid);
}

static int setup(struct test_map_ops **skel)
{
	int err = 0;

	if (!skel)
		return -1;

	*skel = test_map_ops__open();
	if (!ASSERT_OK_PTR(*skel, "test_map_ops__open"))
		return -1;

	(*skel)->rodata->pid = getpid();

	err = test_map_ops__load(*skel);
	if (!ASSERT_OK(err, "test_map_ops__load"))
		return err;

	err = test_map_ops__attach(*skel);
	if (!ASSERT_OK(err, "test_map_ops__attach"))
		return err;

	return err;
}

static void teardown(struct test_map_ops **skel)
{
	if (skel && *skel)
		test_map_ops__destroy(*skel);
}

static void map_ops_update_delete_subtest(void)
{
	struct test_map_ops *skel;

	if (setup(&skel))
		goto teardown;

	map_update();
	ASSERT_OK(skel->bss->err, "map_update_initial");

	map_update();
	ASSERT_LT(skel->bss->err, 0, "map_update_existing");
	ASSERT_EQ(skel->bss->err, -EEXIST, "map_update_existing");

	map_delete();
	ASSERT_OK(skel->bss->err, "map_delete_existing");

	map_delete();
	ASSERT_LT(skel->bss->err, 0, "map_delete_non_existing");
	ASSERT_EQ(skel->bss->err, -ENOENT, "map_delete_non_existing");

teardown:
	teardown(&skel);
}

static void map_ops_push_peek_pop_subtest(void)
{
	struct test_map_ops *skel;

	if (setup(&skel))
		goto teardown;

	map_push();
	ASSERT_OK(skel->bss->err, "map_push_initial");

	map_push();
	ASSERT_LT(skel->bss->err, 0, "map_push_when_full");
	ASSERT_EQ(skel->bss->err, -E2BIG, "map_push_when_full");

	map_peek();
	ASSERT_OK(skel->bss->err, "map_peek");

	map_pop();
	ASSERT_OK(skel->bss->err, "map_pop");

	map_peek();
	ASSERT_LT(skel->bss->err, 0, "map_peek_when_empty");
	ASSERT_EQ(skel->bss->err, -ENOENT, "map_peek_when_empty");

	map_pop();
	ASSERT_LT(skel->bss->err, 0, "map_pop_when_empty");
	ASSERT_EQ(skel->bss->err, -ENOENT, "map_pop_when_empty");

teardown:
	teardown(&skel);
}

static void map_ops_for_each_subtest(void)
{
	struct test_map_ops *skel;

	if (setup(&skel))
		goto teardown;

	map_for_each_pass();
	/* expect to iterate over 1 element */
	ASSERT_EQ(skel->bss->err, 1, "map_for_each_no_flags");

	map_for_each_fail();
	ASSERT_LT(skel->bss->err, 0, "map_for_each_with_flags");
	ASSERT_EQ(skel->bss->err, -EINVAL, "map_for_each_with_flags");

teardown:
	teardown(&skel);
}

void test_map_ops(void)
{
	if (test__start_subtest("map_ops_update_delete"))
		map_ops_update_delete_subtest();

	if (test__start_subtest("map_ops_push_peek_pop"))
		map_ops_push_peek_pop_subtest();

	if (test__start_subtest("map_ops_for_each"))
		map_ops_for_each_subtest();
}
