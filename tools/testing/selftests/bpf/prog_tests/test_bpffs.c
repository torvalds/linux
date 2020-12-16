// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#define _GNU_SOURCE
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
	int err, duration = 0;

	err = unshare(CLONE_NEWNS);
	if (CHECK(err, "unshare", "failed: %d\n", errno))
		goto out;

	err = mount("", "/", "", MS_REC | MS_PRIVATE, NULL);
	if (CHECK(err, "mount /", "failed: %d\n", errno))
		goto out;

	err = umount(TDIR);
	if (CHECK(err, "umount " TDIR, "failed: %d\n", errno))
		goto out;

	err = mount("none", TDIR, "tmpfs", 0, NULL);
	if (CHECK(err, "mount", "mount root failed: %d\n", errno))
		goto out;

	err = mkdir(TDIR "/fs1", 0777);
	if (CHECK(err, "mkdir "TDIR"/fs1", "failed: %d\n", errno))
		goto out;
	err = mkdir(TDIR "/fs2", 0777);
	if (CHECK(err, "mkdir "TDIR"/fs2", "failed: %d\n", errno))
		goto out;

	err = mount("bpf", TDIR "/fs1", "bpf", 0, NULL);
	if (CHECK(err, "mount bpffs "TDIR"/fs1", "failed: %d\n", errno))
		goto out;
	err = mount("bpf", TDIR "/fs2", "bpf", 0, NULL);
	if (CHECK(err, "mount bpffs " TDIR "/fs2", "failed: %d\n", errno))
		goto out;

	err = read_iter(TDIR "/fs1/maps.debug");
	if (CHECK(err, "reading " TDIR "/fs1/maps.debug", "failed\n"))
		goto out;
	err = read_iter(TDIR "/fs2/progs.debug");
	if (CHECK(err, "reading " TDIR "/fs2/progs.debug", "failed\n"))
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
