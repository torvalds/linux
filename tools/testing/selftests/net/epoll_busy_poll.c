// SPDX-License-Identifier: GPL-2.0-or-later

/* Basic per-epoll context busy poll test.
 *
 * Only tests the ioctls, but should be expanded to test two connected hosts in
 * the future
 */

#define _GNU_SOURCE

#include <error.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/capability.h>

#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include "../kselftest_harness.h"

/* if the headers haven't been updated, we need to define some things */
#if !defined(EPOLL_IOC_TYPE)
struct epoll_params {
	uint32_t busy_poll_usecs;
	uint16_t busy_poll_budget;
	uint8_t prefer_busy_poll;

	/* pad the struct to a multiple of 64bits */
	uint8_t __pad;
};

#define EPOLL_IOC_TYPE 0x8A
#define EPIOCSPARAMS _IOW(EPOLL_IOC_TYPE, 0x01, struct epoll_params)
#define EPIOCGPARAMS _IOR(EPOLL_IOC_TYPE, 0x02, struct epoll_params)
#endif

FIXTURE(invalid_fd)
{
	int invalid_fd;
	struct epoll_params params;
};

FIXTURE_SETUP(invalid_fd)
{
	int ret;

	ret = socket(AF_UNIX, SOCK_DGRAM, 0);
	EXPECT_NE(-1, ret)
		TH_LOG("error creating unix socket");

	self->invalid_fd = ret;
}

FIXTURE_TEARDOWN(invalid_fd)
{
	int ret;

	ret = close(self->invalid_fd);
	EXPECT_EQ(0, ret);
}

TEST_F(invalid_fd, test_invalid_fd)
{
	int ret;

	ret = ioctl(self->invalid_fd, EPIOCGPARAMS, &self->params);

	EXPECT_EQ(-1, ret)
		TH_LOG("EPIOCGPARAMS on invalid epoll FD should error");

	EXPECT_EQ(ENOTTY, errno)
		TH_LOG("EPIOCGPARAMS on invalid epoll FD should set errno to ENOTTY");

	memset(&self->params, 0, sizeof(struct epoll_params));

	ret = ioctl(self->invalid_fd, EPIOCSPARAMS, &self->params);

	EXPECT_EQ(-1, ret)
		TH_LOG("EPIOCSPARAMS on invalid epoll FD should error");

	EXPECT_EQ(ENOTTY, errno)
		TH_LOG("EPIOCSPARAMS on invalid epoll FD should set errno to ENOTTY");
}

FIXTURE(epoll_busy_poll)
{
	int fd;
	struct epoll_params params;
	struct epoll_params *invalid_params;
	cap_t caps;
};

FIXTURE_SETUP(epoll_busy_poll)
{
	int ret;

	ret = epoll_create1(0);
	EXPECT_NE(-1, ret)
		TH_LOG("epoll_create1 failed?");

	self->fd = ret;

	self->caps = cap_get_proc();
	EXPECT_NE(NULL, self->caps);
}

FIXTURE_TEARDOWN(epoll_busy_poll)
{
	int ret;

	ret = close(self->fd);
	EXPECT_EQ(0, ret);

	ret = cap_free(self->caps);
	EXPECT_NE(-1, ret)
		TH_LOG("unable to free capabilities");
}

TEST_F(epoll_busy_poll, test_get_params)
{
	/* begin by getting the epoll params from the kernel
	 *
	 * the default should be default and all fields should be zero'd by the
	 * kernel, so set params fields to garbage to test this.
	 */
	int ret = 0;

	self->params.busy_poll_usecs = 0xff;
	self->params.busy_poll_budget = 0xff;
	self->params.prefer_busy_poll = 1;
	self->params.__pad = 0xf;

	ret = ioctl(self->fd, EPIOCGPARAMS, &self->params);
	EXPECT_EQ(0, ret)
		TH_LOG("ioctl EPIOCGPARAMS should succeed");

	EXPECT_EQ(0, self->params.busy_poll_usecs)
		TH_LOG("EPIOCGPARAMS busy_poll_usecs should have been 0");

	EXPECT_EQ(0, self->params.busy_poll_budget)
		TH_LOG("EPIOCGPARAMS busy_poll_budget should have been 0");

	EXPECT_EQ(0, self->params.prefer_busy_poll)
		TH_LOG("EPIOCGPARAMS prefer_busy_poll should have been 0");

	EXPECT_EQ(0, self->params.__pad)
		TH_LOG("EPIOCGPARAMS __pad should have been 0");

	self->invalid_params = (struct epoll_params *)0xdeadbeef;
	ret = ioctl(self->fd, EPIOCGPARAMS, self->invalid_params);

	EXPECT_EQ(-1, ret)
		TH_LOG("EPIOCGPARAMS should error with invalid params");

	EXPECT_EQ(EFAULT, errno)
		TH_LOG("EPIOCGPARAMS with invalid params should set errno to EFAULT");
}

TEST_F(epoll_busy_poll, test_set_invalid)
{
	int ret;

	memset(&self->params, 0, sizeof(struct epoll_params));

	self->params.__pad = 1;

	ret = ioctl(self->fd, EPIOCSPARAMS, &self->params);

	EXPECT_EQ(-1, ret)
		TH_LOG("EPIOCSPARAMS non-zero __pad should error");

	EXPECT_EQ(EINVAL, errno)
		TH_LOG("EPIOCSPARAMS non-zero __pad errno should be EINVAL");

	self->params.__pad = 0;
	self->params.busy_poll_usecs = (uint32_t)INT_MAX + 1;

	ret = ioctl(self->fd, EPIOCSPARAMS, &self->params);

	EXPECT_EQ(-1, ret)
		TH_LOG("EPIOCSPARAMS should error busy_poll_usecs > S32_MAX");

	EXPECT_EQ(EINVAL, errno)
		TH_LOG("EPIOCSPARAMS busy_poll_usecs > S32_MAX errno should be EINVAL");

	self->params.__pad = 0;
	self->params.busy_poll_usecs = 32;
	self->params.prefer_busy_poll = 2;

	ret = ioctl(self->fd, EPIOCSPARAMS, &self->params);

	EXPECT_EQ(-1, ret)
		TH_LOG("EPIOCSPARAMS should error prefer_busy_poll > 1");

	EXPECT_EQ(EINVAL, errno)
		TH_LOG("EPIOCSPARAMS prefer_busy_poll > 1 errno should be EINVAL");

	self->params.__pad = 0;
	self->params.busy_poll_usecs = 32;
	self->params.prefer_busy_poll = 1;

	/* set budget well above kernel's NAPI_POLL_WEIGHT of 64 */
	self->params.busy_poll_budget = UINT16_MAX;

	/* test harness should run with CAP_NET_ADMIN, but let's make sure */
	cap_flag_value_t tmp;

	ret = cap_get_flag(self->caps, CAP_NET_ADMIN, CAP_EFFECTIVE, &tmp);
	EXPECT_EQ(0, ret)
		TH_LOG("unable to get CAP_NET_ADMIN cap flag");

	EXPECT_EQ(CAP_SET, tmp)
		TH_LOG("expecting CAP_NET_ADMIN to be set for the test harness");

	/* at this point we know CAP_NET_ADMIN is available, so setting the
	 * params with a busy_poll_budget > NAPI_POLL_WEIGHT should succeed
	 */
	ret = ioctl(self->fd, EPIOCSPARAMS, &self->params);

	EXPECT_EQ(0, ret)
		TH_LOG("EPIOCSPARAMS should allow busy_poll_budget > NAPI_POLL_WEIGHT");

	/* remove CAP_NET_ADMIN from our effective set */
	cap_value_t net_admin[] = { CAP_NET_ADMIN };

	ret = cap_set_flag(self->caps, CAP_EFFECTIVE, 1, net_admin, CAP_CLEAR);
	EXPECT_EQ(0, ret)
		TH_LOG("couldn't clear CAP_NET_ADMIN");

	ret = cap_set_proc(self->caps);
	EXPECT_EQ(0, ret)
		TH_LOG("cap_set_proc should drop CAP_NET_ADMIN");

	/* this is now expected to fail */
	ret = ioctl(self->fd, EPIOCSPARAMS, &self->params);

	EXPECT_EQ(-1, ret)
		TH_LOG("EPIOCSPARAMS should error busy_poll_budget > NAPI_POLL_WEIGHT");

	EXPECT_EQ(EPERM, errno)
		TH_LOG("EPIOCSPARAMS errno should be EPERM busy_poll_budget > NAPI_POLL_WEIGHT");

	/* restore CAP_NET_ADMIN to our effective set */
	ret = cap_set_flag(self->caps, CAP_EFFECTIVE, 1, net_admin, CAP_SET);
	EXPECT_EQ(0, ret)
		TH_LOG("couldn't restore CAP_NET_ADMIN");

	ret = cap_set_proc(self->caps);
	EXPECT_EQ(0, ret)
		TH_LOG("cap_set_proc should set  CAP_NET_ADMIN");

	self->invalid_params = (struct epoll_params *)0xdeadbeef;
	ret = ioctl(self->fd, EPIOCSPARAMS, self->invalid_params);

	EXPECT_EQ(-1, ret)
		TH_LOG("EPIOCSPARAMS should error when epoll_params is invalid");

	EXPECT_EQ(EFAULT, errno)
		TH_LOG("EPIOCSPARAMS should set errno to EFAULT when epoll_params is invalid");
}

TEST_F(epoll_busy_poll, test_set_and_get_valid)
{
	int ret;

	memset(&self->params, 0, sizeof(struct epoll_params));

	self->params.busy_poll_usecs = 25;
	self->params.busy_poll_budget = 16;
	self->params.prefer_busy_poll = 1;

	ret = ioctl(self->fd, EPIOCSPARAMS, &self->params);

	EXPECT_EQ(0, ret)
		TH_LOG("EPIOCSPARAMS with valid params should not error");

	/* check that the kernel returns the same values back */

	memset(&self->params, 0, sizeof(struct epoll_params));

	ret = ioctl(self->fd, EPIOCGPARAMS, &self->params);

	EXPECT_EQ(0, ret)
		TH_LOG("EPIOCGPARAMS should not error");

	EXPECT_EQ(25, self->params.busy_poll_usecs)
		TH_LOG("params.busy_poll_usecs incorrect");

	EXPECT_EQ(16, self->params.busy_poll_budget)
		TH_LOG("params.busy_poll_budget incorrect");

	EXPECT_EQ(1, self->params.prefer_busy_poll)
		TH_LOG("params.prefer_busy_poll incorrect");

	EXPECT_EQ(0, self->params.__pad)
		TH_LOG("params.__pad was not 0");
}

TEST_F(epoll_busy_poll, test_invalid_ioctl)
{
	int invalid_ioctl = EPIOCGPARAMS + 10;
	int ret;

	ret = ioctl(self->fd, invalid_ioctl, &self->params);

	EXPECT_EQ(-1, ret)
		TH_LOG("invalid ioctl should return error");

	EXPECT_EQ(EINVAL, errno)
		TH_LOG("invalid ioctl should set errno to EINVAL");
}

TEST_HARNESS_MAIN
