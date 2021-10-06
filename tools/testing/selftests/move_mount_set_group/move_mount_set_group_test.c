// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdarg.h>
#include <sys/syscall.h>

#include "../kselftest_harness.h"

#ifndef CLONE_NEWNS
#define CLONE_NEWNS 0x00020000
#endif

#ifndef CLONE_NEWUSER
#define CLONE_NEWUSER 0x10000000
#endif

#ifndef MS_SHARED
#define MS_SHARED (1 << 20)
#endif

#ifndef MS_PRIVATE
#define MS_PRIVATE (1<<18)
#endif

#ifndef MOVE_MOUNT_SET_GROUP
#define MOVE_MOUNT_SET_GROUP 0x00000100
#endif

#ifndef MOVE_MOUNT_F_EMPTY_PATH
#define MOVE_MOUNT_F_EMPTY_PATH 0x00000004
#endif

#ifndef MOVE_MOUNT_T_EMPTY_PATH
#define MOVE_MOUNT_T_EMPTY_PATH 0x00000040
#endif

static ssize_t write_nointr(int fd, const void *buf, size_t count)
{
	ssize_t ret;

	do {
		ret = write(fd, buf, count);
	} while (ret < 0 && errno == EINTR);

	return ret;
}

static int write_file(const char *path, const void *buf, size_t count)
{
	int fd;
	ssize_t ret;

	fd = open(path, O_WRONLY | O_CLOEXEC | O_NOCTTY | O_NOFOLLOW);
	if (fd < 0)
		return -1;

	ret = write_nointr(fd, buf, count);
	close(fd);
	if (ret < 0 || (size_t)ret != count)
		return -1;

	return 0;
}

static int create_and_enter_userns(void)
{
	uid_t uid;
	gid_t gid;
	char map[100];

	uid = getuid();
	gid = getgid();

	if (unshare(CLONE_NEWUSER))
		return -1;

	if (write_file("/proc/self/setgroups", "deny", sizeof("deny") - 1) &&
	    errno != ENOENT)
		return -1;

	snprintf(map, sizeof(map), "0 %d 1", uid);
	if (write_file("/proc/self/uid_map", map, strlen(map)))
		return -1;


	snprintf(map, sizeof(map), "0 %d 1", gid);
	if (write_file("/proc/self/gid_map", map, strlen(map)))
		return -1;

	if (setgid(0))
		return -1;

	if (setuid(0))
		return -1;

	return 0;
}

static int prepare_unpriv_mountns(void)
{
	if (create_and_enter_userns())
		return -1;

	if (unshare(CLONE_NEWNS))
		return -1;

	if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, 0))
		return -1;

	return 0;
}

static char *get_field(char *src, int nfields)
{
	int i;
	char *p = src;

	for (i = 0; i < nfields; i++) {
		while (*p && *p != ' ' && *p != '\t')
			p++;

		if (!*p)
			break;

		p++;
	}

	return p;
}

static void null_endofword(char *word)
{
	while (*word && *word != ' ' && *word != '\t')
		word++;
	*word = '\0';
}

static bool is_shared_mount(const char *path)
{
	size_t len = 0;
	char *line = NULL;
	FILE *f = NULL;

	f = fopen("/proc/self/mountinfo", "re");
	if (!f)
		return false;

	while (getline(&line, &len, f) != -1) {
		char *opts, *target;

		target = get_field(line, 4);
		if (!target)
			continue;

		opts = get_field(target, 2);
		if (!opts)
			continue;

		null_endofword(target);

		if (strcmp(target, path) != 0)
			continue;

		null_endofword(opts);
		if (strstr(opts, "shared:"))
			return true;
	}

	free(line);
	fclose(f);

	return false;
}

/* Attempt to de-conflict with the selftests tree. */
#ifndef SKIP
#define SKIP(s, ...)	XFAIL(s, ##__VA_ARGS__)
#endif

#define SET_GROUP_FROM	"/tmp/move_mount_set_group_supported_from"
#define SET_GROUP_TO	"/tmp/move_mount_set_group_supported_to"

static int move_mount_set_group_supported(void)
{
	int ret;

	if (mount("testing", "/tmp", "tmpfs", MS_NOATIME | MS_NODEV,
		  "size=100000,mode=700"))
		return -1;

	if (mount(NULL, "/tmp", NULL, MS_PRIVATE, 0))
		return -1;

	if (mkdir(SET_GROUP_FROM, 0777))
		return -1;

	if (mkdir(SET_GROUP_TO, 0777))
		return -1;

	if (mount("testing", SET_GROUP_FROM, "tmpfs", MS_NOATIME | MS_NODEV,
		  "size=100000,mode=700"))
		return -1;

	if (mount(SET_GROUP_FROM, SET_GROUP_TO, NULL, MS_BIND, NULL))
		return -1;

	if (mount(NULL, SET_GROUP_FROM, NULL, MS_SHARED, 0))
		return -1;

	ret = syscall(SYS_move_mount, AT_FDCWD, SET_GROUP_FROM,
		      AT_FDCWD, SET_GROUP_TO, MOVE_MOUNT_SET_GROUP);
	umount2("/tmp", MNT_DETACH);

	return ret < 0 ? false : true;
}

FIXTURE(move_mount_set_group) {
};

#define SET_GROUP_A "/tmp/A"

FIXTURE_SETUP(move_mount_set_group)
{
	int ret;

	ASSERT_EQ(prepare_unpriv_mountns(), 0);

	ret = move_mount_set_group_supported();
	ASSERT_GE(ret, 0);
	if (!ret)
		SKIP(return, "move_mount(MOVE_MOUNT_SET_GROUP) is not supported");

	umount2("/tmp", MNT_DETACH);

	ASSERT_EQ(mount("testing", "/tmp", "tmpfs", MS_NOATIME | MS_NODEV,
			"size=100000,mode=700"), 0);

	ASSERT_EQ(mkdir(SET_GROUP_A, 0777), 0);

	ASSERT_EQ(mount("testing", SET_GROUP_A, "tmpfs", MS_NOATIME | MS_NODEV,
			"size=100000,mode=700"), 0);
}

FIXTURE_TEARDOWN(move_mount_set_group)
{
	int ret;

	ret = move_mount_set_group_supported();
	ASSERT_GE(ret, 0);
	if (!ret)
		SKIP(return, "move_mount(MOVE_MOUNT_SET_GROUP) is not supported");

	umount2("/tmp", MNT_DETACH);
}

#define __STACK_SIZE (8 * 1024 * 1024)
static pid_t do_clone(int (*fn)(void *), void *arg, int flags)
{
	void *stack;

	stack = malloc(__STACK_SIZE);
	if (!stack)
		return -ENOMEM;

#ifdef __ia64__
	return __clone2(fn, stack, __STACK_SIZE, flags | SIGCHLD, arg, NULL);
#else
	return clone(fn, stack + __STACK_SIZE, flags | SIGCHLD, arg, NULL);
#endif
}

static int wait_for_pid(pid_t pid)
{
	int status, ret;

again:
	ret = waitpid(pid, &status, 0);
	if (ret == -1) {
		if (errno == EINTR)
			goto again;

		return -1;
	}

	if (!WIFEXITED(status))
		return -1;

	return WEXITSTATUS(status);
}

struct child_args {
	int unsfd;
	int mntnsfd;
	bool shared;
	int mntfd;
};

static int get_nestedns_mount_cb(void *data)
{
	struct child_args *ca = (struct child_args *)data;
	int ret;

	ret = prepare_unpriv_mountns();
	if (ret)
		return 1;

	if (ca->shared) {
		ret = mount(NULL, SET_GROUP_A, NULL, MS_SHARED, 0);
		if (ret)
			return 1;
	}

	ret = open("/proc/self/ns/user", O_RDONLY);
	if (ret < 0)
		return 1;
	ca->unsfd = ret;

	ret = open("/proc/self/ns/mnt", O_RDONLY);
	if (ret < 0)
		return 1;
	ca->mntnsfd = ret;

	ret = open(SET_GROUP_A, O_RDONLY);
	if (ret < 0)
		return 1;
	ca->mntfd = ret;

	return 0;
}

TEST_F(move_mount_set_group, complex_sharing_copying)
{
	struct child_args ca_from = {
		.shared = true,
	};
	struct child_args ca_to = {
		.shared = false,
	};
	pid_t pid;
	int ret;

	ret = move_mount_set_group_supported();
	ASSERT_GE(ret, 0);
	if (!ret)
		SKIP(return, "move_mount(MOVE_MOUNT_SET_GROUP) is not supported");

	pid = do_clone(get_nestedns_mount_cb, (void *)&ca_from, CLONE_VFORK |
		       CLONE_VM | CLONE_FILES); ASSERT_GT(pid, 0);
	ASSERT_EQ(wait_for_pid(pid), 0);

	pid = do_clone(get_nestedns_mount_cb, (void *)&ca_to, CLONE_VFORK |
		       CLONE_VM | CLONE_FILES); ASSERT_GT(pid, 0);
	ASSERT_EQ(wait_for_pid(pid), 0);

	ASSERT_EQ(syscall(SYS_move_mount, ca_from.mntfd, "",
			  ca_to.mntfd, "", MOVE_MOUNT_SET_GROUP
			  | MOVE_MOUNT_F_EMPTY_PATH | MOVE_MOUNT_T_EMPTY_PATH),
		  0);

	ASSERT_EQ(setns(ca_to.mntnsfd, CLONE_NEWNS), 0);
	ASSERT_EQ(is_shared_mount(SET_GROUP_A), 1);
}

TEST_HARNESS_MAIN
