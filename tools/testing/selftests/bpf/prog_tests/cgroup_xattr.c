// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <test_progs.h>
#include "cgroup_helpers.h"

#include "read_cgroupfs_xattr.skel.h"
#include "cgroup_read_xattr.skel.h"

#define CGROUP_FS_PARENT "foo/"
#define CGROUP_FS_CHILD CGROUP_FS_PARENT "bar/"
#define TMP_FILE "/tmp/selftests_cgroup_xattr"

static const char xattr_value_a[] = "bpf_selftest_value_a";
static const char xattr_value_b[] = "bpf_selftest_value_b";
static const char xattr_name[] = "user.bpf_test";

static void test_read_cgroup_xattr(void)
{
	int tmp_fd, parent_cgroup_fd = -1, child_cgroup_fd = -1;
	struct read_cgroupfs_xattr *skel = NULL;

	parent_cgroup_fd = test__join_cgroup(CGROUP_FS_PARENT);
	if (!ASSERT_OK_FD(parent_cgroup_fd, "create parent cgroup"))
		return;
	if (!ASSERT_OK(set_cgroup_xattr(CGROUP_FS_PARENT, xattr_name, xattr_value_a),
		       "set parent xattr"))
		goto out;

	child_cgroup_fd = test__join_cgroup(CGROUP_FS_CHILD);
	if (!ASSERT_OK_FD(child_cgroup_fd, "create child cgroup"))
		goto out;
	if (!ASSERT_OK(set_cgroup_xattr(CGROUP_FS_CHILD, xattr_name, xattr_value_b),
		       "set child xattr"))
		goto out;

	skel = read_cgroupfs_xattr__open_and_load();
	if (!ASSERT_OK_PTR(skel, "read_cgroupfs_xattr__open_and_load"))
		goto out;

	skel->bss->target_pid = sys_gettid();

	if (!ASSERT_OK(read_cgroupfs_xattr__attach(skel), "read_cgroupfs_xattr__attach"))
		goto out;

	tmp_fd = open(TMP_FILE, O_RDONLY | O_CREAT);
	ASSERT_OK_FD(tmp_fd, "open tmp file");
	close(tmp_fd);

	ASSERT_TRUE(skel->bss->found_value_a, "found_value_a");
	ASSERT_TRUE(skel->bss->found_value_b, "found_value_b");

out:
	close(child_cgroup_fd);
	close(parent_cgroup_fd);
	read_cgroupfs_xattr__destroy(skel);
	unlink(TMP_FILE);
}

void test_cgroup_xattr(void)
{
	RUN_TESTS(cgroup_read_xattr);

	if (test__start_subtest("read_cgroupfs_xattr"))
		test_read_cgroup_xattr();
}
