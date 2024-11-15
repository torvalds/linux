// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include <linux/genetlink.h>
#include <linux/netlink.h>
#include <linux/mqueue.h>

#include "../kselftest_harness.h"

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
	ASSERT_GE(n, sizeof(struct nlmsghdr));

	n = recv(netlink_sock, buf, sizeof(buf), MSG_DONTWAIT);
	ASSERT_GE(n, sizeof(struct nlmsghdr));

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

TEST_HARNESS_MAIN
