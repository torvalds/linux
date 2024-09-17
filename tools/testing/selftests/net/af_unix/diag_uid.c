// SPDX-License-Identifier: GPL-2.0
/* Copyright Amazon.com Inc. or its affiliates. */

#define _GNU_SOURCE
#include <sched.h>

#include <unistd.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/sock_diag.h>
#include <linux/unix_diag.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>

#include "../../kselftest_harness.h"

FIXTURE(diag_uid)
{
	int netlink_fd;
	int unix_fd;
	__u32 inode;
	__u64 cookie;
};

FIXTURE_VARIANT(diag_uid)
{
	int unshare;
	int udiag_show;
};

FIXTURE_VARIANT_ADD(diag_uid, uid)
{
	.unshare = 0,
	.udiag_show = UDIAG_SHOW_UID
};

FIXTURE_VARIANT_ADD(diag_uid, uid_unshare)
{
	.unshare = CLONE_NEWUSER,
	.udiag_show = UDIAG_SHOW_UID
};

FIXTURE_SETUP(diag_uid)
{
	struct stat file_stat;
	socklen_t optlen;
	int ret;

	if (variant->unshare)
		ASSERT_EQ(unshare(variant->unshare), 0);

	self->netlink_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_SOCK_DIAG);
	ASSERT_NE(self->netlink_fd, -1);

	self->unix_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	ASSERT_NE(self->unix_fd, -1);

	ret = fstat(self->unix_fd, &file_stat);
	ASSERT_EQ(ret, 0);

	self->inode = file_stat.st_ino;

	optlen = sizeof(self->cookie);
	ret = getsockopt(self->unix_fd, SOL_SOCKET, SO_COOKIE, &self->cookie, &optlen);
	ASSERT_EQ(ret, 0);
}

FIXTURE_TEARDOWN(diag_uid)
{
	close(self->netlink_fd);
	close(self->unix_fd);
}

int send_request(struct __test_metadata *_metadata,
		 FIXTURE_DATA(diag_uid) *self,
		 const FIXTURE_VARIANT(diag_uid) *variant)
{
	struct {
		struct nlmsghdr nlh;
		struct unix_diag_req udr;
	} req = {
		.nlh = {
			.nlmsg_len = sizeof(req),
			.nlmsg_type = SOCK_DIAG_BY_FAMILY,
			.nlmsg_flags = NLM_F_REQUEST
		},
		.udr = {
			.sdiag_family = AF_UNIX,
			.udiag_ino = self->inode,
			.udiag_cookie = {
				(__u32)self->cookie,
				(__u32)(self->cookie >> 32)
			},
			.udiag_show = variant->udiag_show
		}
	};
	struct sockaddr_nl nladdr = {
		.nl_family = AF_NETLINK
	};
	struct iovec iov = {
		.iov_base = &req,
		.iov_len = sizeof(req)
	};
	struct msghdr msg = {
		.msg_name = &nladdr,
		.msg_namelen = sizeof(nladdr),
		.msg_iov = &iov,
		.msg_iovlen = 1
	};

	return sendmsg(self->netlink_fd, &msg, 0);
}

void render_response(struct __test_metadata *_metadata,
		     struct unix_diag_req *udr, __u32 len)
{
	unsigned int rta_len = len - NLMSG_LENGTH(sizeof(*udr));
	struct rtattr *attr;
	uid_t uid;

	ASSERT_GT(len, sizeof(*udr));
	ASSERT_EQ(udr->sdiag_family, AF_UNIX);

	attr = (struct rtattr *)(udr + 1);
	ASSERT_NE(RTA_OK(attr, rta_len), 0);
	ASSERT_EQ(attr->rta_type, UNIX_DIAG_UID);

	uid = *(uid_t *)RTA_DATA(attr);
	ASSERT_EQ(uid, getuid());
}

void receive_response(struct __test_metadata *_metadata,
		      FIXTURE_DATA(diag_uid) *self)
{
	long buf[8192 / sizeof(long)];
	struct sockaddr_nl nladdr = {
		.nl_family = AF_NETLINK
	};
	struct iovec iov = {
		.iov_base = buf,
		.iov_len = sizeof(buf)
	};
	struct msghdr msg = {
		.msg_name = &nladdr,
		.msg_namelen = sizeof(nladdr),
		.msg_iov = &iov,
		.msg_iovlen = 1
	};
	struct nlmsghdr *nlh;
	int ret;

	ret = recvmsg(self->netlink_fd, &msg, 0);
	ASSERT_GT(ret, 0);

	nlh = (struct nlmsghdr *)buf;
	ASSERT_NE(NLMSG_OK(nlh, ret), 0);
	ASSERT_EQ(nlh->nlmsg_type, SOCK_DIAG_BY_FAMILY);

	render_response(_metadata, NLMSG_DATA(nlh), nlh->nlmsg_len);

	nlh = NLMSG_NEXT(nlh, ret);
	ASSERT_EQ(NLMSG_OK(nlh, ret), 0);
}

TEST_F(diag_uid, 1)
{
	int ret;

	ret = send_request(_metadata, self, variant);
	ASSERT_GT(ret, 0);

	receive_response(_metadata, self);
}

TEST_HARNESS_MAIN
