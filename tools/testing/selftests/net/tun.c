// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include "../kselftest_harness.h"

static int tun_attach(int fd, char *dev)
{
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, dev);
	ifr.ifr_flags = IFF_ATTACH_QUEUE;

	return ioctl(fd, TUNSETQUEUE, (void *) &ifr);
}

static int tun_detach(int fd, char *dev)
{
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, dev);
	ifr.ifr_flags = IFF_DETACH_QUEUE;

	return ioctl(fd, TUNSETQUEUE, (void *) &ifr);
}

static int tun_alloc(char *dev)
{
	struct ifreq ifr;
	int fd, err;

	fd = open("/dev/net/tun", O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "can't open tun: %s\n", strerror(errno));
		return fd;
	}

	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, dev);
	ifr.ifr_flags = IFF_TAP | IFF_NAPI | IFF_MULTI_QUEUE;

	err = ioctl(fd, TUNSETIFF, (void *) &ifr);
	if (err < 0) {
		fprintf(stderr, "can't TUNSETIFF: %s\n", strerror(errno));
		close(fd);
		return err;
	}
	strcpy(dev, ifr.ifr_name);
	return fd;
}

static int tun_delete(char *dev)
{
	struct {
		struct nlmsghdr  nh;
		struct ifinfomsg ifm;
		unsigned char    data[64];
	} req;
	struct rtattr *rta;
	int ret, rtnl;

	rtnl = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
	if (rtnl < 0) {
		fprintf(stderr, "can't open rtnl: %s\n", strerror(errno));
		return 1;
	}

	memset(&req, 0, sizeof(req));
	req.nh.nlmsg_len = NLMSG_ALIGN(NLMSG_LENGTH(sizeof(req.ifm)));
	req.nh.nlmsg_flags = NLM_F_REQUEST;
	req.nh.nlmsg_type = RTM_DELLINK;

	req.ifm.ifi_family = AF_UNSPEC;

	rta = (struct rtattr *)(((char *)&req) + NLMSG_ALIGN(req.nh.nlmsg_len));
	rta->rta_type = IFLA_IFNAME;
	rta->rta_len = RTA_LENGTH(IFNAMSIZ);
	req.nh.nlmsg_len += rta->rta_len;
	memcpy(RTA_DATA(rta), dev, IFNAMSIZ);

	ret = send(rtnl, &req, req.nh.nlmsg_len, 0);
	if (ret < 0)
		fprintf(stderr, "can't send: %s\n", strerror(errno));
	ret = (unsigned int)ret != req.nh.nlmsg_len;

	close(rtnl);
	return ret;
}

FIXTURE(tun)
{
	char ifname[IFNAMSIZ];
	int fd, fd2;
};

FIXTURE_SETUP(tun)
{
	memset(self->ifname, 0, sizeof(self->ifname));

	self->fd = tun_alloc(self->ifname);
	ASSERT_GE(self->fd, 0);

	self->fd2 = tun_alloc(self->ifname);
	ASSERT_GE(self->fd2, 0);
}

FIXTURE_TEARDOWN(tun)
{
	if (self->fd >= 0)
		close(self->fd);
	if (self->fd2 >= 0)
		close(self->fd2);
}

TEST_F(tun, delete_detach_close) {
	EXPECT_EQ(tun_delete(self->ifname), 0);
	EXPECT_EQ(tun_detach(self->fd, self->ifname), -1);
	EXPECT_EQ(errno, 22);
}

TEST_F(tun, detach_delete_close) {
	EXPECT_EQ(tun_detach(self->fd, self->ifname), 0);
	EXPECT_EQ(tun_delete(self->ifname), 0);
}

TEST_F(tun, detach_close_delete) {
	EXPECT_EQ(tun_detach(self->fd, self->ifname), 0);
	close(self->fd);
	self->fd = -1;
	EXPECT_EQ(tun_delete(self->ifname), 0);
}

TEST_F(tun, reattach_delete_close) {
	EXPECT_EQ(tun_detach(self->fd, self->ifname), 0);
	EXPECT_EQ(tun_attach(self->fd, self->ifname), 0);
	EXPECT_EQ(tun_delete(self->ifname), 0);
}

TEST_F(tun, reattach_close_delete) {
	EXPECT_EQ(tun_detach(self->fd, self->ifname), 0);
	EXPECT_EQ(tun_attach(self->fd, self->ifname), 0);
	close(self->fd);
	self->fd = -1;
	EXPECT_EQ(tun_delete(self->ifname), 0);
}

TEST_HARNESS_MAIN
