// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#define _GNU_SOURCE
#include <stdio.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <test_progs.h>

#define TDIR "/sys/kernel/debug"

static int read_iter(char *file)
{
	/* 1024 should be enough to get contiguous 4 "iter" letters at some point */
	char buf[1024];
	int fd, len;

	fd = open(file, 0);
	if (fd < 0)
		return -1;
	while ((len = read(fd, buf, sizeof(buf))) > 0)
		if (strstr(buf, "iter")) {
			close(fd);
			return 0;
		}
	close(fd);
	return -1;
}

static int fn(void)
{
	struct stat a, b, c;
	int err, map;

	err = unshare(CLONE_NEWNS);
	if (!ASSERT_OK(err, "unshare"))
		goto out;

	err = mount("", "/", "", MS_REC | MS_PRIVATE, NULL);
	if (!ASSERT_OK(err, "mount /"))
		goto out;

	err = umount(TDIR);
	if (!ASSERT_OK(err, "umount " TDIR))
		goto out;

	err = mount("none", TDIR, "tmpfs", 0, NULL);
	if (!ASSERT_OK(err, "mount tmpfs"))
		goto out;

	err = mkdir(TDIR "/fs1", 0777);
	if (!ASSERT_OK(err, "mkdir " TDIR "/fs1"))
		goto out;
	err = mkdir(TDIR "/fs2", 0777);
	if (!ASSERT_OK(err, "mkdir " TDIR "/fs2"))
		goto out;

	err = mount("bpf", TDIR "/fs1", "bpf", 0, NULL);
	if (!ASSERT_OK(err, "mount bpffs " TDIR "/fs1"))
		goto out;
	err = mount("bpf", TDIR "/fs2", "bpf", 0, NULL);
	if (!ASSERT_OK(err, "mount bpffs " TDIR "/fs2"))
		goto out;

	err = read_iter(TDIR "/fs1/maps.debug");
	if (!ASSERT_OK(err, "reading " TDIR "/fs1/maps.debug"))
		goto out;
	err = read_iter(TDIR "/fs2/progs.debug");
	if (!ASSERT_OK(err, "reading " TDIR "/fs2/progs.debug"))
		goto out;

	err = mkdir(TDIR "/fs1/a", 0777);
	if (!ASSERT_OK(err, "creating " TDIR "/fs1/a"))
		goto out;
	err = mkdir(TDIR "/fs1/a/1", 0777);
	if (!ASSERT_OK(err, "creating " TDIR "/fs1/a/1"))
		goto out;
	err = mkdir(TDIR "/fs1/b", 0777);
	if (!ASSERT_OK(err, "creating " TDIR "/fs1/b"))
		goto out;

	map = bpf_map_create(BPF_MAP_TYPE_ARRAY, NULL, 4, 4, 1, NULL);
	if (!ASSERT_GT(map, 0, "create_map(ARRAY)"))
		goto out;
	err = bpf_obj_pin(map, TDIR "/fs1/c");
	if (!ASSERT_OK(err, "pin map"))
		goto out;
	close(map);

	/* Check that RENAME_EXCHANGE works for directories. */
	err = stat(TDIR "/fs1/a", &a);
	if (!ASSERT_OK(err, "stat(" TDIR "/fs1/a)"))
		goto out;
	err = renameat2(0, TDIR "/fs1/a", 0, TDIR "/fs1/b", RENAME_EXCHANGE);
	if (!ASSERT_OK(err, "renameat2(/fs1/a, /fs1/b, RENAME_EXCHANGE)"))
		goto out;
	err = stat(TDIR "/fs1/b", &b);
	if (!ASSERT_OK(err, "stat(" TDIR "/fs1/b)"))
		goto out;
	if (!ASSERT_EQ(a.st_ino, b.st_ino, "b should have a's inode"))
		goto out;
	err = access(TDIR "/fs1/b/1", F_OK);
	if (!ASSERT_OK(err, "access(" TDIR "/fs1/b/1)"))
		goto out;

	/* Check that RENAME_EXCHANGE works for mixed file types. */
	err = stat(TDIR "/fs1/c", &c);
	if (!ASSERT_OK(err, "stat(" TDIR "/fs1/map)"))
		goto out;
	err = renameat2(0, TDIR "/fs1/c", 0, TDIR "/fs1/b", RENAME_EXCHANGE);
	if (!ASSERT_OK(err, "renameat2(/fs1/c, /fs1/b, RENAME_EXCHANGE)"))
		goto out;
	err = stat(TDIR "/fs1/b", &b);
	if (!ASSERT_OK(err, "stat(" TDIR "/fs1/b)"))
		goto out;
	if (!ASSERT_EQ(c.st_ino, b.st_ino, "b should have c's inode"))
		goto out;
	err = access(TDIR "/fs1/c/1", F_OK);
	if (!ASSERT_OK(err, "access(" TDIR "/fs1/c/1)"))
		goto out;

	/* Check that RENAME_NOREPLACE works. */
	err = renameat2(0, TDIR "/fs1/b", 0, TDIR "/fs1/a", RENAME_NOREPLACE);
	if (!ASSERT_ERR(err, "renameat2(RENAME_NOREPLACE)")) {
		err = -EINVAL;
		goto out;
	}
	err = access(TDIR "/fs1/b", F_OK);
	if (!ASSERT_OK(err, "access(" TDIR "/fs1/b)"))
		goto out;

out:
	umount(TDIR "/fs1");
	umount(TDIR "/fs2");
	rmdir(TDIR "/fs1");
	rmdir(TDIR "/fs2");
	umount(TDIR);
	exit(err);
}

void test_test_bpffs(void)
{
	int err, duration = 0, status = 0;
	pid_t pid;

	pid = fork();
	if (CHECK(pid == -1, "clone", "clone failed %d", errno))
		return;
	if (pid == 0)
		fn();
	err = waitpid(pid, &status, 0);
	if (CHECK(err == -1 && errno != ECHILD, "waitpid", "failed %d", errno))
		return;
	if (CHECK(WEXITSTATUS(status), "bpffs test ", "failed %d", WEXITSTATUS(status)))
		return;
}
