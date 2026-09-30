// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../kselftest_harness.h"

/*
 * The fields of a uid_map/gid_map line are stored in u32. Values that
 * do not fit, and negative values, must be rejected with EINVAL instead
 * of being silently truncated, and in-range values must still be
 * accepted.
 */

static int write_file(const char *path, const char *buf)
{
	ssize_t len = strlen(buf);
	ssize_t ret;
	int fd;

	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return -errno;
	ret = write(fd, buf, len);
	if (ret < 0)
		ret = -errno;
	close(fd);
	if (ret < 0)
		return ret;
	return ret == len ? 0 : -EIO;
}

static ssize_t read_file(const char *path, char *buf, size_t size)
{
	ssize_t ret;
	int fd;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -errno;
	ret = read(fd, buf, size - 1);
	if (ret < 0)
		ret = -errno;
	else
		buf[ret] = '\0';
	close(fd);
	return ret;
}

/* Enter a fresh user namespace, remembering the outer ids. */
static int enter_userns(uid_t *uid, gid_t *gid)
{
	*uid = geteuid();
	*gid = getegid();
	if (unshare(CLONE_NEWUSER) < 0)
		return -errno;
	/* Required before an unprivileged gid_map write. */
	return write_file("/proc/self/setgroups", "deny");
}

static void expect_rejected(struct __test_metadata *_metadata,
			    const char *path, const char *line)
{
	char buf[256];

	TH_LOG("writing \"%s\" to %s", line, path);
	EXPECT_EQ(write_file(path, line), -EINVAL);
	/* Nothing may have been installed. */
	EXPECT_EQ(read_file(path, buf, sizeof(buf)), 0) {
		TH_LOG("installed: %s", buf);
	}
}

static void expect_accepted(struct __test_metadata *_metadata,
			    const char *path, const char *line,
			    unsigned int first, unsigned int lower_first,
			    unsigned int count)
{
	unsigned int f, l, c;
	char buf[256];

	TH_LOG("writing \"%s\" to %s", line, path);
	ASSERT_EQ(write_file(path, line), 0);

	ASSERT_GT(read_file(path, buf, sizeof(buf)), 0);
	ASSERT_EQ(sscanf(buf, "%u %u %u", &f, &l, &c), 3);
	EXPECT_EQ(f, first);
	EXPECT_EQ(l, lower_first);
	EXPECT_EQ(c, count);
	/* Exactly one extent. */
	EXPECT_EQ(strchr(buf, '\n'), buf + strlen(buf) - 1);
}

TEST(uid_map_first_above_u32)
{
	char line[64];
	uid_t uid;
	gid_t gid;
	int ret;

	ret = enter_userns(&uid, &gid);
	if (ret)
		SKIP(return, "cannot set up user namespace: %s", strerror(-ret));
	snprintf(line, sizeof(line), "4294967301 %u 1", uid);
	expect_rejected(_metadata, "/proc/self/uid_map", line);
}

TEST(uid_map_lower_first_above_u32)
{
	char line[64];
	uid_t uid;
	gid_t gid;
	int ret;

	ret = enter_userns(&uid, &gid);
	if (ret)
		SKIP(return, "cannot set up user namespace: %s", strerror(-ret));
	snprintf(line, sizeof(line), "0 %llu 1", (1ULL << 32) + uid);
	expect_rejected(_metadata, "/proc/self/uid_map", line);
}

TEST(uid_map_count_above_u32)
{
	char line[64];
	uid_t uid;
	gid_t gid;
	int ret;

	ret = enter_userns(&uid, &gid);
	if (ret)
		SKIP(return, "cannot set up user namespace: %s", strerror(-ret));
	snprintf(line, sizeof(line), "0 %u 4294967297", uid);
	expect_rejected(_metadata, "/proc/self/uid_map", line);
}

TEST(gid_map_first_above_u32)
{
	char line[64];
	uid_t uid;
	gid_t gid;
	int ret;

	ret = enter_userns(&uid, &gid);
	if (ret)
		SKIP(return, "cannot set up user namespace: %s", strerror(-ret));
	snprintf(line, sizeof(line), "4294967301 %u 1", gid);
	expect_rejected(_metadata, "/proc/self/gid_map", line);
}

TEST(uid_map_first_negative)
{
	char line[64];
	uid_t uid;
	gid_t gid;
	int ret;

	ret = enter_userns(&uid, &gid);
	if (ret)
		SKIP(return, "cannot set up user namespace: %s", strerror(-ret));
	snprintf(line, sizeof(line), "-1 %u 1", uid);
	expect_rejected(_metadata, "/proc/self/uid_map", line);
}

TEST(uid_map_lower_first_negative)
{
	uid_t uid;
	gid_t gid;
	int ret;

	ret = enter_userns(&uid, &gid);
	if (ret)
		SKIP(return, "cannot set up user namespace: %s", strerror(-ret));
	expect_rejected(_metadata, "/proc/self/uid_map", "0 -1 1");
}

TEST(uid_map_count_negative)
{
	char line[64];
	uid_t uid;
	gid_t gid;
	int ret;

	ret = enter_userns(&uid, &gid);
	if (ret)
		SKIP(return, "cannot set up user namespace: %s", strerror(-ret));
	snprintf(line, sizeof(line), "0 %u -1", uid);
	expect_rejected(_metadata, "/proc/self/uid_map", line);
}

TEST(uid_map_in_range)
{
	char line[64];
	uid_t uid;
	gid_t gid;
	int ret;

	ret = enter_userns(&uid, &gid);
	if (ret)
		SKIP(return, "cannot set up user namespace: %s", strerror(-ret));
	snprintf(line, sizeof(line), "5 %u 1", uid);
	expect_accepted(_metadata, "/proc/self/uid_map", line, 5, uid, 1);
}

TEST(uid_map_zero)
{
	char line[64];
	uid_t uid;
	gid_t gid;
	int ret;

	ret = enter_userns(&uid, &gid);
	if (ret)
		SKIP(return, "cannot set up user namespace: %s", strerror(-ret));
	snprintf(line, sizeof(line), "0 %u 1", uid);
	expect_accepted(_metadata, "/proc/self/uid_map", line, 0, uid, 1);
}

/* 4294967295 is (u32)-1, which map_write() refuses as a starting id. */
TEST(uid_map_largest_first)
{
	char line[64];
	uid_t uid;
	gid_t gid;
	int ret;

	ret = enter_userns(&uid, &gid);
	if (ret)
		SKIP(return, "cannot set up user namespace: %s", strerror(-ret));
	snprintf(line, sizeof(line), "4294967294 %u 1", uid);
	expect_accepted(_metadata, "/proc/self/uid_map", line,
			4294967294U, uid, 1);
}

TEST(uid_map_extra_spaces)
{
	char line[64];
	uid_t uid;
	gid_t gid;
	int ret;

	ret = enter_userns(&uid, &gid);
	if (ret)
		SKIP(return, "cannot set up user namespace: %s", strerror(-ret));
	snprintf(line, sizeof(line), "0   %u  1", uid);
	expect_accepted(_metadata, "/proc/self/uid_map", line, 0, uid, 1);
}

/*
 * An unprivileged writer may only map a single id, so the largest count
 * needs a privileged parent writing the map of a child namespace. With
 * first and lower_first 0, the no-wrap rule allows up to 4294967295.
 */
TEST(uid_map_largest_count)
{
	unsigned int first, lower_first, count;
	int to_child[2], from_child[2];
	char path[64], buf[256];
	pid_t pid;
	char c;

	if (geteuid() != 0)
		SKIP(return, "needs root in the initial user namespace");
	ASSERT_GT(read_file("/proc/self/uid_map", buf, sizeof(buf)), 0);
	ASSERT_EQ(sscanf(buf, "%u %u %u", &first, &lower_first, &count), 3);
	if (first != 0 || lower_first != 0 || count != 4294967295U)
		SKIP(return, "not in the initial user namespace");

	ASSERT_EQ(pipe(to_child), 0);
	ASSERT_EQ(pipe(from_child), 0);
	pid = fork();
	ASSERT_GE(pid, 0);
	if (pid == 0) {
		close(to_child[1]);
		close(from_child[0]);
		c = unshare(CLONE_NEWUSER) ? 'F' : 'K';
		if (write(from_child[1], &c, 1) != 1)
			_exit(1);
		/* Stay alive until the parent is done with our uid_map. */
		if (read(to_child[0], &c, 1) < 0)
			_exit(1);
		_exit(0);
	}
	close(to_child[0]);
	close(from_child[1]);

	ASSERT_EQ(read(from_child[0], &c, 1), 1);
	ASSERT_EQ(c, 'K');
	snprintf(path, sizeof(path), "/proc/%d/uid_map", pid);
	expect_accepted(_metadata, path, "0 0 4294967295", 0, 0, 4294967295U);

	close(to_child[1]);
	EXPECT_EQ(waitpid(pid, NULL, 0), pid);
}

TEST_HARNESS_MAIN
