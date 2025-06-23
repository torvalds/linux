// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/xattr.h>

#include <test_progs.h>

#include "read_cgroupfs_xattr.skel.h"
#include "cgroup_read_xattr.skel.h"

#define CGROUP_FS_ROOT "/sys/fs/cgroup/"
#define CGROUP_FS_PARENT CGROUP_FS_ROOT "foo/"
#define CGROUP_FS_CHILD CGROUP_FS_PARENT "bar/"

static int move_pid_to_cgroup(const char *cgroup_folder, pid_t pid)
{
	char filename[128];
	char pid_str[64];
	int procs_fd;
	int ret;

	snprintf(filename, sizeof(filename), "%scgroup.procs", cgroup_folder);
	snprintf(pid_str, sizeof(pid_str), "%d", pid);

	procs_fd = open(filename, O_WRONLY | O_APPEND);
	if (!ASSERT_OK_FD(procs_fd, "open"))
		return -1;

	ret = write(procs_fd, pid_str, strlen(pid_str));
	close(procs_fd);
	if (!ASSERT_GT(ret, 0, "write cgroup.procs"))
		return -1;
	return 0;
}

static void reset_cgroups_and_lo(void)
{
	rmdir(CGROUP_FS_CHILD);
	rmdir(CGROUP_FS_PARENT);
	system("ip addr del 1.1.1.1/32 dev lo");
	system("ip link set dev lo down");
}

static const char xattr_value_a[] = "bpf_selftest_value_a";
static const char xattr_value_b[] = "bpf_selftest_value_b";
static const char xattr_name[] = "user.bpf_test";

static int setup_cgroups_and_lo(void)
{
	int err;

	err = mkdir(CGROUP_FS_PARENT, 0755);
	if (!ASSERT_OK(err, "mkdir 1"))
		goto error;
	err = mkdir(CGROUP_FS_CHILD, 0755);
	if (!ASSERT_OK(err, "mkdir 2"))
		goto error;

	err = setxattr(CGROUP_FS_PARENT, xattr_name, xattr_value_a,
		       strlen(xattr_value_a) + 1, 0);
	if (!ASSERT_OK(err, "setxattr 1"))
		goto error;

	err = setxattr(CGROUP_FS_CHILD, xattr_name, xattr_value_b,
		       strlen(xattr_value_b) + 1, 0);
	if (!ASSERT_OK(err, "setxattr 2"))
		goto error;

	err = system("ip link set dev lo up");
	if (!ASSERT_OK(err, "lo up"))
		goto error;

	err = system("ip addr add 1.1.1.1 dev lo");
	if (!ASSERT_OK(err, "lo addr v4"))
		goto error;

	err = write_sysctl("/proc/sys/net/ipv4/ping_group_range", "0 0");
	if (!ASSERT_OK(err, "write_sysctl"))
		goto error;

	return 0;
error:
	reset_cgroups_and_lo();
	return err;
}

static void test_read_cgroup_xattr(void)
{
	struct sockaddr_in sa4 = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
	};
	struct read_cgroupfs_xattr *skel = NULL;
	pid_t pid = gettid();
	int sock_fd = -1;
	int connect_fd = -1;

	if (!ASSERT_OK(setup_cgroups_and_lo(), "setup_cgroups_and_lo"))
		return;
	if (!ASSERT_OK(move_pid_to_cgroup(CGROUP_FS_CHILD, pid),
		       "move_pid_to_cgroup"))
		goto out;

	skel = read_cgroupfs_xattr__open_and_load();
	if (!ASSERT_OK_PTR(skel, "read_cgroupfs_xattr__open_and_load"))
		goto out;

	skel->bss->target_pid = pid;

	if (!ASSERT_OK(read_cgroupfs_xattr__attach(skel), "read_cgroupfs_xattr__attach"))
		goto out;

	sock_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
	if (!ASSERT_OK_FD(sock_fd, "sock create"))
		goto out;

	connect_fd = connect(sock_fd, &sa4, sizeof(sa4));
	if (!ASSERT_OK_FD(connect_fd, "connect 1"))
		goto out;
	close(connect_fd);

	ASSERT_TRUE(skel->bss->found_value_a, "found_value_a");
	ASSERT_TRUE(skel->bss->found_value_b, "found_value_b");

out:
	close(connect_fd);
	close(sock_fd);
	read_cgroupfs_xattr__destroy(skel);
	move_pid_to_cgroup(CGROUP_FS_ROOT, pid);
	reset_cgroups_and_lo();
}

void test_cgroup_xattr(void)
{
	RUN_TESTS(cgroup_read_xattr);

	if (test__start_subtest("read_cgroupfs_xattr"))
		test_read_cgroup_xattr();
}
