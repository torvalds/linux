// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include <linux/genetlink.h>
#include <linux/neighbour.h>
#include <linux/netdevice.h>
#include <linux/netlink.h>
#include <linux/mqueue.h>
#include <linux/rtnetlink.h>

#include "kselftest_harness.h"

#include <ynl.h>

struct ext_ack {
	int err;

	__u32 attr_offs;
	__u32 miss_type;
	__u32 miss_nest;
	const char *str;
};

enum get_ea_ret {
	ERROR = -1,
	NO_CTRL = 0,
	FOUND_DONE,
	FOUND_ERR,
	FOUND_EXTACK,
};

static enum get_ea_ret
nl_get_extack(char *buf, size_t n, struct ext_ack *ea)
{
	enum get_ea_ret ret = NO_CTRL;
	const struct nlmsghdr *nlh;
	const struct nlattr *attr;
	ssize_t rem;

	for (rem = n; rem > 0; NLMSG_NEXT(nlh, rem)) {
		nlh = (struct nlmsghdr *)&buf[n - rem];
		if (!NLMSG_OK(nlh, rem))
			return ERROR;

		if (nlh->nlmsg_type == NLMSG_ERROR)
			ret = FOUND_ERR;
		else if (nlh->nlmsg_type == NLMSG_DONE)
			ret = FOUND_DONE;
		else
			continue;

		ea->err = -*(int *)NLMSG_DATA(nlh);

		if (!(nlh->nlmsg_flags & NLM_F_ACK_TLVS))
			return ret;

		ynl_attr_for_each(attr, nlh, sizeof(int)) {
			switch (ynl_attr_type(attr)) {
			case NLMSGERR_ATTR_OFFS:
				ea->attr_offs = ynl_attr_get_u32(attr);
				break;
			case NLMSGERR_ATTR_MISS_TYPE:
				ea->miss_type = ynl_attr_get_u32(attr);
				break;
			case NLMSGERR_ATTR_MISS_NEST:
				ea->miss_nest = ynl_attr_get_u32(attr);
				break;
			case NLMSGERR_ATTR_MSG:
				ea->str = ynl_attr_get_str(attr);
				break;
			}
		}

		return FOUND_EXTACK;
	}

	return ret;
}

static const struct {
	struct nlmsghdr nlhdr;
	struct ndmsg ndm;
	struct nlattr ahdr;
	__u32 val;
} dump_neigh_bad = {
	.nlhdr = {
		.nlmsg_len	= sizeof(dump_neigh_bad),
		.nlmsg_type	= RTM_GETNEIGH,
		.nlmsg_flags	= NLM_F_REQUEST | NLM_F_ACK | NLM_F_DUMP,
		.nlmsg_seq	= 1,
	},
	.ndm = {
		.ndm_family	= 123,
	},
	.ahdr = {
		.nla_len	= 4 + 4,
		.nla_type	= NDA_FLAGS_EXT,
	},
	.val = -1, // should fail MASK validation
};

TEST(dump_extack)
{
	int netlink_sock;
	int i, cnt, ret;
	char buf[8192];
	int one = 1;
	ssize_t n;

	netlink_sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	ASSERT_GE(netlink_sock, 0);

	n = setsockopt(netlink_sock, SOL_NETLINK, NETLINK_CAP_ACK,
		       &one, sizeof(one));
	ASSERT_EQ(n, 0);
	n = setsockopt(netlink_sock, SOL_NETLINK, NETLINK_EXT_ACK,
		       &one, sizeof(one));
	ASSERT_EQ(n, 0);
	n = setsockopt(netlink_sock, SOL_NETLINK, NETLINK_GET_STRICT_CHK,
		       &one, sizeof(one));
	ASSERT_EQ(n, 0);

	/* Dump so many times we fill up the buffer */
	cnt = 80;
	for (i = 0; i < cnt; i++) {
		n = send(netlink_sock, &dump_neigh_bad,
			 sizeof(dump_neigh_bad), 0);
		ASSERT_EQ(n, sizeof(dump_neigh_bad));
	}

	/* Read out the ENOBUFS */
	n = recv(netlink_sock, buf, sizeof(buf), MSG_DONTWAIT);
	EXPECT_EQ(n, -1);
	EXPECT_EQ(errno, ENOBUFS);

	ret = NO_CTRL;
	for (i = 0; i < cnt; i++) {
		struct ext_ack ea = {};

		n = recv(netlink_sock, buf, sizeof(buf), MSG_DONTWAIT);
		if (n < 0) {
			ASSERT_GE(i, 10);
			break;
		}
		ASSERT_GE(n, (ssize_t)sizeof(struct nlmsghdr));

		ret = nl_get_extack(buf, n, &ea);
		/* Once we fill the buffer we'll see one ENOBUFS followed
		 * by a number of EBUSYs. Then the last recv() will finally
		 * trigger and complete the dump.
		 */
		if (ret == FOUND_ERR && (ea.err == ENOBUFS || ea.err == EBUSY))
			continue;
		EXPECT_EQ(ret, FOUND_EXTACK);
		EXPECT_EQ(ea.err, EINVAL);
		EXPECT_EQ(ea.attr_offs,
			  sizeof(struct nlmsghdr) + sizeof(struct ndmsg));
	}
	/* Make sure last message was a full DONE+extack */
	EXPECT_EQ(ret, FOUND_EXTACK);
}

static const struct {
	struct nlmsghdr nlhdr;
	struct genlmsghdr genlhdr;
	struct nlattr ahdr;
	__u16 val;
	__u16 pad;
} dump_policies = {
	.nlhdr = {
		.nlmsg_len	= sizeof(dump_policies),
		.nlmsg_type	= GENL_ID_CTRL,
		.nlmsg_flags	= NLM_F_REQUEST | NLM_F_ACK | NLM_F_DUMP,
		.nlmsg_seq	= 1,
	},
	.genlhdr = {
		.cmd		= CTRL_CMD_GETPOLICY,
		.version	= 2,
	},
	.ahdr = {
		.nla_len	= 6,
		.nla_type	= CTRL_ATTR_FAMILY_ID,
	},
	.val = GENL_ID_CTRL,
	.pad = 0,
};

// Sanity check for the test itself, make sure the dump doesn't fit in one msg
TEST(test_sanity)
{
	int netlink_sock;
	char buf[8192];
	ssize_t n;

	netlink_sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);
	ASSERT_GE(netlink_sock, 0);

	n = send(netlink_sock, &dump_policies, sizeof(dump_policies), 0);
	ASSERT_EQ(n, sizeof(dump_policies));

	n = recv(netlink_sock, buf, sizeof(buf), MSG_DONTWAIT);
	ASSERT_GE(n, (ssize_t)sizeof(struct nlmsghdr));

	n = recv(netlink_sock, buf, sizeof(buf), MSG_DONTWAIT);
	ASSERT_GE(n, (ssize_t)sizeof(struct nlmsghdr));

	close(netlink_sock);
}

TEST(close_in_progress)
{
	int netlink_sock;
	ssize_t n;

	netlink_sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);
	ASSERT_GE(netlink_sock, 0);

	n = send(netlink_sock, &dump_policies, sizeof(dump_policies), 0);
	ASSERT_EQ(n, sizeof(dump_policies));

	close(netlink_sock);
}

TEST(close_with_ref)
{
	char cookie[NOTIFY_COOKIE_LEN] = {};
	int netlink_sock, mq_fd;
	struct sigevent sigev;
	ssize_t n;

	netlink_sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);
	ASSERT_GE(netlink_sock, 0);

	n = send(netlink_sock, &dump_policies, sizeof(dump_policies), 0);
	ASSERT_EQ(n, sizeof(dump_policies));

	mq_fd = syscall(__NR_mq_open, "sed", O_CREAT | O_WRONLY, 0600, 0);
	ASSERT_GE(mq_fd, 0);

	memset(&sigev, 0, sizeof(sigev));
	sigev.sigev_notify		= SIGEV_THREAD;
	sigev.sigev_value.sival_ptr	= cookie;
	sigev.sigev_signo		= netlink_sock;

	syscall(__NR_mq_notify, mq_fd, &sigev);

	close(netlink_sock);

	// give mqueue time to fire
	usleep(100 * 1000);
}

static int lo_set_up(int sock, bool up)
{
	struct ifreq ifr = {};

	strcpy(ifr.ifr_name, "lo");
	if (ioctl(sock, SIOCGIFFLAGS, &ifr))
		return -1;
	if (up)
		ifr.ifr_flags |= IFF_UP;
	else
		ifr.ifr_flags &= ~IFF_UP;
	return ioctl(sock, SIOCSIFFLAGS, &ifr);
}

static const struct {
	struct nlmsghdr nlhdr;
	struct ifinfomsg ifm;
} dump_links = {
	.nlhdr = {
		.nlmsg_len	= sizeof(dump_links),
		.nlmsg_type	= RTM_GETLINK,
		.nlmsg_flags	= NLM_F_REQUEST | NLM_F_ACK | NLM_F_DUMP,
		.nlmsg_seq	= 1,
	},
	.ifm = {
		.ifi_family	= AF_UNSPEC,
	},
};

// A dump started while a notification is still queued must make progress
// for a reader which peeks before reading, like libnl does
TEST(dump_with_queued_ntf)
{
	struct sockaddr_nl addr = {
		.nl_family	= AF_NETLINK,
		.nl_groups	= RTMGRP_LINK,
	};
	int netlink_sock, inet_sock, i, ret, links = 0;
	int rcvbuf = 2048;
	bool done = false;
	char buf[32768];
	ssize_t n;

	ret = unshare(CLONE_NEWNET);
	if (ret && errno == EPERM)
		SKIP(return, "Needs CAP_SYS_ADMIN for a netns");
	ASSERT_EQ(ret, 0);

	inet_sock = socket(AF_INET, SOCK_DGRAM, 0);
	ASSERT_GE(inet_sock, 0);
	netlink_sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	ASSERT_GE(netlink_sock, 0);
	ASSERT_EQ(setsockopt(netlink_sock, SOL_SOCKET, SO_RCVBUF,
			     &rcvbuf, sizeof(rcvbuf)), 0);
	ASSERT_EQ(bind(netlink_sock, (struct sockaddr *)&addr,
		       sizeof(addr)), 0);

	// A large read makes the kernel attempt large dump skbs
	ASSERT_EQ(lo_set_up(inet_sock, true), 0);
	n = recv(netlink_sock, buf, sizeof(buf), 0);
	ASSERT_GE(n, (ssize_t)sizeof(struct nlmsghdr));

	// Leave one notification queued, then start the dump
	ASSERT_EQ(lo_set_up(inet_sock, false), 0);
	n = send(netlink_sock, &dump_links, sizeof(dump_links), 0);
	ASSERT_EQ(n, sizeof(dump_links));

	for (i = 0; i < 100 && !done; i++) {
		struct nlmsghdr *nlh;

		n = recv(netlink_sock, buf, sizeof(buf),
			 MSG_PEEK | MSG_TRUNC | MSG_DONTWAIT);
		if (n < 0)
			continue;
		n = recv(netlink_sock, buf, sizeof(buf), MSG_DONTWAIT);
		if (n < 0)
			continue;

		for (nlh = (struct nlmsghdr *)buf; NLMSG_OK(nlh, n);
		     nlh = NLMSG_NEXT(nlh, n)) {
			if (nlh->nlmsg_seq != dump_links.nlhdr.nlmsg_seq)
				continue;
			if (nlh->nlmsg_type == NLMSG_ERROR)
				EXPECT_EQ(*(int *)NLMSG_DATA(nlh), 0);
			if (nlh->nlmsg_type == RTM_NEWLINK)
				links++;
			if (nlh->nlmsg_type == NLMSG_DONE) {
				EXPECT_EQ(*(int *)NLMSG_DATA(nlh), 0);
				done = true;
			}
		}
	}
	EXPECT_TRUE(done);
	EXPECT_GE(links, 1);

	close(netlink_sock);
	close(inet_sock);
}

TEST_HARNESS_MAIN
