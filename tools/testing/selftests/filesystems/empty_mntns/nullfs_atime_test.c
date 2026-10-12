// SPDX-License-Identifier: GPL-2.0
/*
 * The root of every empty mount namespace is the same nullfs inode. A read
 * of it by one user must not change the access time another user sees.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "../../kselftest_harness.h"

#ifndef UNSHARE_EMPTY_MNTNS
#define UNSHARE_EMPTY_MNTNS	0x00100000
#endif

enum {
	CHILD_OK,
	CHILD_UNSUPPORTED,
	CHILD_SETUP,
	CHILD_CHANGED,
};

static int wait_byte(int fd)
{
	char c;

	return read(fd, &c, 1) == 1 ? 0 : -1;
}

static int send_byte(int fd)
{
	return write(fd, "x", 1) == 1 ? 0 : -1;
}

static int empty_mntns(void)
{
	if (!unshare(UNSHARE_EMPTY_MNTNS))
		return 0;
	return errno == EINVAL ? CHILD_UNSUPPORTED : CHILD_SETUP;
}

/* the watcher: stats its root before and after the reader read its own */
static int watcher(int to_reader, int from_reader)
{
	struct stat before, after;
	int ret;

	ret = empty_mntns();
	if (ret)
		return ret;
	if (stat("/", &before))
		return CHILD_SETUP;
	if (send_byte(to_reader) || wait_byte(from_reader))
		return CHILD_SETUP;
	if (stat("/", &after))
		return CHILD_SETUP;
	if (before.st_atim.tv_sec != after.st_atim.tv_sec ||
	    before.st_atim.tv_nsec != after.st_atim.tv_nsec)
		return CHILD_CHANGED;
	return CHILD_OK;
}

/* the reader: lists its own root, which is the same inode */
static int reader(int to_watcher, int from_watcher)
{
	struct dirent *de;
	DIR *d;
	int ret;

	ret = empty_mntns();
	if (ret)
		return ret;
	if (wait_byte(from_watcher))
		return CHILD_SETUP;
	d = opendir("/");
	if (!d)
		return CHILD_SETUP;
	while ((de = readdir(d)))
		;
	closedir(d);
	return send_byte(to_watcher) ? CHILD_SETUP : CHILD_OK;
}

static int wait_child(pid_t pid)
{
	int status;

	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status))
		return -1;
	return WEXITSTATUS(status);
}

TEST(empty_mntns_root_atime)
{
	int to_reader[2], to_watcher[2], w, r;
	pid_t watcher_pid, reader_pid;

	if (geteuid())
		SKIP(return, "test requires root");
	ASSERT_EQ(pipe(to_reader), 0);
	ASSERT_EQ(pipe(to_watcher), 0);

	watcher_pid = fork();
	ASSERT_GE(watcher_pid, 0);
	if (watcher_pid == 0)
		_exit(watcher(to_reader[1], to_watcher[0]));
	reader_pid = fork();
	ASSERT_GE(reader_pid, 0);
	if (reader_pid == 0)
		_exit(reader(to_watcher[1], to_reader[0]));

	r = wait_child(reader_pid);
	w = wait_child(watcher_pid);
	if (r == CHILD_UNSUPPORTED || w == CHILD_UNSUPPORTED)
		SKIP(return, "UNSHARE_EMPTY_MNTNS not supported");
	EXPECT_EQ(r, CHILD_OK);
	EXPECT_EQ(w, CHILD_OK)
		TH_LOG("the access time of the root changed while this namespace did nothing");
}

TEST_HARNESS_MAIN
