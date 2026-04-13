// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2026 Christian Brauner <brauner@kernel.org>
/*
 * Test extended attributes on sockfs sockets.
 *
 * Sockets created via socket() have their inodes in sockfs, which supports
 * user.* xattrs with per-inode limits: up to 128 xattrs and 128KB total
 * value size. These tests verify xattr operations via fsetxattr/fgetxattr/
 * flistxattr/fremovexattr on the socket fd, as well as limit enforcement.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <unistd.h>

#include "../../kselftest_harness.h"

#define TEST_XATTR_NAME		"user.testattr"
#define TEST_XATTR_VALUE	"testvalue"
#define TEST_XATTR_VALUE2	"newvalue"

/* Per-inode limits for user.* xattrs on sockfs (from include/linux/xattr.h) */
#define SIMPLE_XATTR_MAX_NR	128
#define SIMPLE_XATTR_MAX_SIZE	(128 << 10)	/* 128 KB */

#ifndef XATTR_SIZE_MAX
#define XATTR_SIZE_MAX 65536
#endif

/*
 * Fixture for sockfs socket xattr tests.
 * Creates an AF_UNIX socket (lives in sockfs, not bound to any path).
 */
FIXTURE(xattr_sockfs)
{
	int sockfd;
};

FIXTURE_SETUP(xattr_sockfs)
{
	self->sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
	ASSERT_GE(self->sockfd, 0) {
		TH_LOG("Failed to create socket: %s", strerror(errno));
	}
}

FIXTURE_TEARDOWN(xattr_sockfs)
{
	if (self->sockfd >= 0)
		close(self->sockfd);
}

TEST_F(xattr_sockfs, set_get_user_xattr)
{
	char buf[256];
	ssize_t ret;

	ret = fsetxattr(self->sockfd, TEST_XATTR_NAME,
			TEST_XATTR_VALUE, strlen(TEST_XATTR_VALUE), 0);
	ASSERT_EQ(ret, 0) {
		TH_LOG("fsetxattr failed: %s", strerror(errno));
	}

	memset(buf, 0, sizeof(buf));
	ret = fgetxattr(self->sockfd, TEST_XATTR_NAME, buf, sizeof(buf));
	ASSERT_EQ(ret, (ssize_t)strlen(TEST_XATTR_VALUE)) {
		TH_LOG("fgetxattr returned %zd: %s", ret, strerror(errno));
	}
	ASSERT_STREQ(buf, TEST_XATTR_VALUE);
}

/*
 * Test listing xattrs on a sockfs socket.
 * Should include user.* xattrs and system.sockprotoname.
 */
TEST_F(xattr_sockfs, list_user_xattr)
{
	char list[4096];
	ssize_t ret;
	char *ptr;
	bool found_user = false;
	bool found_proto = false;

	ret = fsetxattr(self->sockfd, TEST_XATTR_NAME,
			TEST_XATTR_VALUE, strlen(TEST_XATTR_VALUE), 0);
	ASSERT_EQ(ret, 0) {
		TH_LOG("fsetxattr failed: %s", strerror(errno));
	}

	memset(list, 0, sizeof(list));
	ret = flistxattr(self->sockfd, list, sizeof(list));
	ASSERT_GT(ret, 0) {
		TH_LOG("flistxattr failed: %s", strerror(errno));
	}

	for (ptr = list; ptr < list + ret; ptr += strlen(ptr) + 1) {
		if (strcmp(ptr, TEST_XATTR_NAME) == 0)
			found_user = true;
		if (strcmp(ptr, "system.sockprotoname") == 0)
			found_proto = true;
	}
	ASSERT_TRUE(found_user) {
		TH_LOG("user xattr not found in list");
	}
	ASSERT_TRUE(found_proto) {
		TH_LOG("system.sockprotoname not found in list");
	}
}

TEST_F(xattr_sockfs, remove_user_xattr)
{
	char buf[256];
	ssize_t ret;

	ret = fsetxattr(self->sockfd, TEST_XATTR_NAME,
			TEST_XATTR_VALUE, strlen(TEST_XATTR_VALUE), 0);
	ASSERT_EQ(ret, 0);

	ret = fremovexattr(self->sockfd, TEST_XATTR_NAME);
	ASSERT_EQ(ret, 0) {
		TH_LOG("fremovexattr failed: %s", strerror(errno));
	}

	ret = fgetxattr(self->sockfd, TEST_XATTR_NAME, buf, sizeof(buf));
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, ENODATA);
}

TEST_F(xattr_sockfs, update_user_xattr)
{
	char buf[256];
	ssize_t ret;

	ret = fsetxattr(self->sockfd, TEST_XATTR_NAME,
			TEST_XATTR_VALUE, strlen(TEST_XATTR_VALUE), 0);
	ASSERT_EQ(ret, 0);

	ret = fsetxattr(self->sockfd, TEST_XATTR_NAME,
			TEST_XATTR_VALUE2, strlen(TEST_XATTR_VALUE2), 0);
	ASSERT_EQ(ret, 0);

	memset(buf, 0, sizeof(buf));
	ret = fgetxattr(self->sockfd, TEST_XATTR_NAME, buf, sizeof(buf));
	ASSERT_EQ(ret, (ssize_t)strlen(TEST_XATTR_VALUE2));
	ASSERT_STREQ(buf, TEST_XATTR_VALUE2);
}

TEST_F(xattr_sockfs, xattr_create_flag)
{
	int ret;

	ret = fsetxattr(self->sockfd, TEST_XATTR_NAME,
			TEST_XATTR_VALUE, strlen(TEST_XATTR_VALUE), 0);
	ASSERT_EQ(ret, 0);

	ret = fsetxattr(self->sockfd, TEST_XATTR_NAME,
			TEST_XATTR_VALUE2, strlen(TEST_XATTR_VALUE2),
			XATTR_CREATE);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, EEXIST);
}

TEST_F(xattr_sockfs, xattr_replace_flag)
{
	int ret;

	ret = fsetxattr(self->sockfd, TEST_XATTR_NAME,
			TEST_XATTR_VALUE, strlen(TEST_XATTR_VALUE),
			XATTR_REPLACE);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, ENODATA);
}

TEST_F(xattr_sockfs, get_nonexistent)
{
	char buf[256];
	ssize_t ret;

	ret = fgetxattr(self->sockfd, "user.nonexistent", buf, sizeof(buf));
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, ENODATA);
}

TEST_F(xattr_sockfs, empty_value)
{
	ssize_t ret;

	ret = fsetxattr(self->sockfd, TEST_XATTR_NAME, "", 0, 0);
	ASSERT_EQ(ret, 0);

	ret = fgetxattr(self->sockfd, TEST_XATTR_NAME, NULL, 0);
	ASSERT_EQ(ret, 0);
}

TEST_F(xattr_sockfs, get_size)
{
	ssize_t ret;

	ret = fsetxattr(self->sockfd, TEST_XATTR_NAME,
			TEST_XATTR_VALUE, strlen(TEST_XATTR_VALUE), 0);
	ASSERT_EQ(ret, 0);

	ret = fgetxattr(self->sockfd, TEST_XATTR_NAME, NULL, 0);
	ASSERT_EQ(ret, (ssize_t)strlen(TEST_XATTR_VALUE));
}

TEST_F(xattr_sockfs, buffer_too_small)
{
	char buf[2];
	ssize_t ret;

	ret = fsetxattr(self->sockfd, TEST_XATTR_NAME,
			TEST_XATTR_VALUE, strlen(TEST_XATTR_VALUE), 0);
	ASSERT_EQ(ret, 0);

	ret = fgetxattr(self->sockfd, TEST_XATTR_NAME, buf, sizeof(buf));
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, ERANGE);
}

/*
 * Test maximum number of user.* xattrs per socket.
 * The kernel enforces SIMPLE_XATTR_MAX_NR (128), so the 129th should
 * fail with ENOSPC.
 */
TEST_F(xattr_sockfs, max_nr_xattrs)
{
	char name[32];
	int i, ret;

	for (i = 0; i < SIMPLE_XATTR_MAX_NR; i++) {
		snprintf(name, sizeof(name), "user.test%03d", i);
		ret = fsetxattr(self->sockfd, name, "v", 1, 0);
		ASSERT_EQ(ret, 0) {
			TH_LOG("fsetxattr %s failed at i=%d: %s",
			       name, i, strerror(errno));
		}
	}

	ret = fsetxattr(self->sockfd, "user.overflow", "v", 1, 0);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, ENOSPC) {
		TH_LOG("Expected ENOSPC for xattr %d, got %s",
		       SIMPLE_XATTR_MAX_NR + 1, strerror(errno));
	}
}

/*
 * Test maximum total value size for user.* xattrs.
 * The kernel enforces SIMPLE_XATTR_MAX_SIZE (128KB). Individual xattr
 * values are limited to XATTR_SIZE_MAX (64KB) by the VFS, so we need
 * at least two xattrs to hit the total limit.
 */
TEST_F(xattr_sockfs, max_xattr_size)
{
	char *value;
	int ret;

	value = malloc(XATTR_SIZE_MAX);
	ASSERT_NE(value, NULL);
	memset(value, 'A', XATTR_SIZE_MAX);

	/* First 64KB xattr - total = 64KB */
	ret = fsetxattr(self->sockfd, "user.big1", value, XATTR_SIZE_MAX, 0);
	ASSERT_EQ(ret, 0) {
		TH_LOG("first large xattr failed: %s", strerror(errno));
	}

	/* Second 64KB xattr - total = 128KB (exactly at limit) */
	ret = fsetxattr(self->sockfd, "user.big2", value, XATTR_SIZE_MAX, 0);
	free(value);
	ASSERT_EQ(ret, 0) {
		TH_LOG("second large xattr failed: %s", strerror(errno));
	}

	/* Third xattr with 1 byte - total > 128KB, should fail */
	ret = fsetxattr(self->sockfd, "user.big3", "v", 1, 0);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, ENOSPC) {
		TH_LOG("Expected ENOSPC when exceeding size limit, got %s",
		       strerror(errno));
	}
}

/*
 * Test that removing an xattr frees limit space, allowing re-addition.
 */
TEST_F(xattr_sockfs, limit_remove_readd)
{
	char name[32];
	int i, ret;

	/* Fill up to the maximum count */
	for (i = 0; i < SIMPLE_XATTR_MAX_NR; i++) {
		snprintf(name, sizeof(name), "user.test%03d", i);
		ret = fsetxattr(self->sockfd, name, "v", 1, 0);
		ASSERT_EQ(ret, 0);
	}

	/* Verify we're at the limit */
	ret = fsetxattr(self->sockfd, "user.overflow", "v", 1, 0);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, ENOSPC);

	/* Remove one xattr */
	ret = fremovexattr(self->sockfd, "user.test000");
	ASSERT_EQ(ret, 0);

	/* Now we should be able to add one more */
	ret = fsetxattr(self->sockfd, "user.newattr", "v", 1, 0);
	ASSERT_EQ(ret, 0) {
		TH_LOG("re-add after remove failed: %s", strerror(errno));
	}
}

/*
 * Test that two different sockets have independent xattr limits.
 */
TEST_F(xattr_sockfs, limits_per_inode)
{
	char buf[256];
	int sock2;
	ssize_t ret;

	sock2 = socket(AF_UNIX, SOCK_STREAM, 0);
	ASSERT_GE(sock2, 0);

	/* Set xattr on first socket */
	ret = fsetxattr(self->sockfd, TEST_XATTR_NAME,
			TEST_XATTR_VALUE, strlen(TEST_XATTR_VALUE), 0);
	ASSERT_EQ(ret, 0);

	/* First socket's xattr should not be visible on second socket */
	ret = fgetxattr(sock2, TEST_XATTR_NAME, NULL, 0);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, ENODATA);

	/* Second socket should independently accept xattrs */
	ret = fsetxattr(sock2, TEST_XATTR_NAME,
			TEST_XATTR_VALUE2, strlen(TEST_XATTR_VALUE2), 0);
	ASSERT_EQ(ret, 0);

	/* Verify each socket has its own value */
	memset(buf, 0, sizeof(buf));
	ret = fgetxattr(self->sockfd, TEST_XATTR_NAME, buf, sizeof(buf));
	ASSERT_EQ(ret, (ssize_t)strlen(TEST_XATTR_VALUE));
	ASSERT_STREQ(buf, TEST_XATTR_VALUE);

	memset(buf, 0, sizeof(buf));
	ret = fgetxattr(sock2, TEST_XATTR_NAME, buf, sizeof(buf));
	ASSERT_EQ(ret, (ssize_t)strlen(TEST_XATTR_VALUE2));
	ASSERT_STREQ(buf, TEST_XATTR_VALUE2);

	close(sock2);
}

TEST_HARNESS_MAIN
