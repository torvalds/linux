// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2026 Christian Brauner <brauner@kernel.org>
/*
 * Test user.* xattrs on various socket families.
 *
 * All socket types use sockfs for their inodes, so user.* xattrs should
 * work on any socket regardless of address family. This tests AF_INET,
 * AF_INET6, AF_NETLINK, AF_PACKET, and abstract namespace AF_UNIX sockets.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/xattr.h>
#include <linux/netlink.h>
#include <unistd.h>

#include "../../kselftest_harness.h"

#define TEST_XATTR_NAME		"user.testattr"
#define TEST_XATTR_VALUE	"testvalue"

FIXTURE(xattr_socket_types)
{
	int sockfd;
};

FIXTURE_VARIANT(xattr_socket_types)
{
	int family;
	int type;
	int protocol;
};

FIXTURE_VARIANT_ADD(xattr_socket_types, inet) {
	.family = AF_INET,
	.type = SOCK_STREAM,
	.protocol = 0,
};

FIXTURE_VARIANT_ADD(xattr_socket_types, inet6) {
	.family = AF_INET6,
	.type = SOCK_STREAM,
	.protocol = 0,
};

FIXTURE_VARIANT_ADD(xattr_socket_types, netlink) {
	.family = AF_NETLINK,
	.type = SOCK_RAW,
	.protocol = NETLINK_USERSOCK,
};

FIXTURE_VARIANT_ADD(xattr_socket_types, packet) {
	.family = AF_PACKET,
	.type = SOCK_DGRAM,
	.protocol = 0,
};

FIXTURE_SETUP(xattr_socket_types)
{
	self->sockfd = socket(variant->family, variant->type,
			      variant->protocol);
	if (self->sockfd < 0 &&
	    (errno == EAFNOSUPPORT || errno == EPERM || errno == EACCES))
		SKIP(return, "socket(%d, %d, %d) not available: %s",
		     variant->family, variant->type, variant->protocol,
		     strerror(errno));
	ASSERT_GE(self->sockfd, 0) {
		TH_LOG("Failed to create socket(%d, %d, %d): %s",
		       variant->family, variant->type, variant->protocol,
		       strerror(errno));
	}
}

FIXTURE_TEARDOWN(xattr_socket_types)
{
	if (self->sockfd >= 0)
		close(self->sockfd);
}

TEST_F(xattr_socket_types, set_get_list_remove)
{
	char buf[256], list[4096], *ptr;
	ssize_t ret;
	bool found;

	ret = fsetxattr(self->sockfd, TEST_XATTR_NAME,
			TEST_XATTR_VALUE, strlen(TEST_XATTR_VALUE), 0);
	ASSERT_EQ(ret, 0) {
		TH_LOG("fsetxattr failed: %s", strerror(errno));
	}

	memset(buf, 0, sizeof(buf));
	ret = fgetxattr(self->sockfd, TEST_XATTR_NAME, buf, sizeof(buf));
	ASSERT_EQ(ret, (ssize_t)strlen(TEST_XATTR_VALUE));
	ASSERT_STREQ(buf, TEST_XATTR_VALUE);

	memset(list, 0, sizeof(list));
	ret = flistxattr(self->sockfd, list, sizeof(list));
	ASSERT_GT(ret, 0);
	found = false;
	for (ptr = list; ptr < list + ret; ptr += strlen(ptr) + 1) {
		if (strcmp(ptr, TEST_XATTR_NAME) == 0)
			found = true;
	}
	ASSERT_TRUE(found);

	ret = fremovexattr(self->sockfd, TEST_XATTR_NAME);
	ASSERT_EQ(ret, 0);

	ret = fgetxattr(self->sockfd, TEST_XATTR_NAME, buf, sizeof(buf));
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, ENODATA);
}

/*
 * Test abstract namespace AF_UNIX socket.
 * Abstract sockets don't have a filesystem path; their inodes live in
 * sockfs so user.* xattrs should work via fsetxattr/fgetxattr.
 */
FIXTURE(xattr_abstract)
{
	int sockfd;
};

FIXTURE_SETUP(xattr_abstract)
{
	struct sockaddr_un addr;
	char name[64];
	int ret, len;

	self->sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
	ASSERT_GE(self->sockfd, 0);

	len = snprintf(name, sizeof(name), "xattr_test_abstract_%d", getpid());

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	addr.sun_path[0] = '\0';
	memcpy(&addr.sun_path[1], name, len);

	ret = bind(self->sockfd, (struct sockaddr *)&addr,
		   offsetof(struct sockaddr_un, sun_path) + 1 + len);
	ASSERT_EQ(ret, 0);
}

FIXTURE_TEARDOWN(xattr_abstract)
{
	if (self->sockfd >= 0)
		close(self->sockfd);
}

TEST_F(xattr_abstract, set_get)
{
	char buf[256];
	ssize_t ret;

	ret = fsetxattr(self->sockfd, TEST_XATTR_NAME,
			TEST_XATTR_VALUE, strlen(TEST_XATTR_VALUE), 0);
	ASSERT_EQ(ret, 0) {
		TH_LOG("fsetxattr on abstract socket failed: %s",
		       strerror(errno));
	}

	memset(buf, 0, sizeof(buf));
	ret = fgetxattr(self->sockfd, TEST_XATTR_NAME, buf, sizeof(buf));
	ASSERT_EQ(ret, (ssize_t)strlen(TEST_XATTR_VALUE));
	ASSERT_STREQ(buf, TEST_XATTR_VALUE);
}

TEST_HARNESS_MAIN
