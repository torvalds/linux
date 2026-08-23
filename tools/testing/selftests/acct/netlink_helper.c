// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <linux/genetlink.h>

#include "netlink_helper.h"

int netlink_open(void)
{
	struct timeval tv = { .tv_sec = ACCT_RCV_TIMEOUT_SEC };
	struct sockaddr_nl addr = {
		.nl_family = AF_NETLINK,
		.nl_pid = getpid(),
	};
	int fd;

	fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);
	if (fd < 0)
		return -errno;

	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
		int err = -errno;

		close(fd);
		return err;
	}

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		int err = -errno;

		close(fd);
		return err;
	}

	return fd;
}

int send_request(int fd, void *buf, size_t len)
{
	struct sockaddr_nl addr = {
		.nl_family = AF_NETLINK,
	};

	if (sendto(fd, buf, len, 0, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		return -errno;

	return 0;
}

/*
 * Resolve the generic netlink family ID for @name.
 * Returns the family ID (>= 0) on success, negative errno on failure.
 */
int get_family_id(int fd, const char *name)
{
	struct {
		struct nlmsghdr nlh;
		struct genlmsghdr genl;
		char buf[256];
	} req = { 0 };
	char resp[8192];
	struct nlmsghdr *nlh;
	struct genlmsghdr *genl;
	struct nlattr *na;
	int len;
	int rem;
	int ret;

	req.nlh.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
	req.nlh.nlmsg_type = GENL_ID_CTRL;
	req.nlh.nlmsg_flags = NLM_F_REQUEST;
	req.nlh.nlmsg_seq = 1;
	req.nlh.nlmsg_pid = getpid();

	req.genl.cmd = CTRL_CMD_GETFAMILY;
	req.genl.version = 1;

	na = (struct nlattr *)((char *)&req + NLMSG_ALIGN(req.nlh.nlmsg_len));
	na->nla_type = CTRL_ATTR_FAMILY_NAME;
	na->nla_len = NLA_HDRLEN + strlen(name) + 1;
	memcpy(nla_data(na), name, strlen(name) + 1);
	req.nlh.nlmsg_len = NLMSG_ALIGN(req.nlh.nlmsg_len) + NLA_ALIGN(na->nla_len);

	ret = send_request(fd, &req, req.nlh.nlmsg_len);
	if (ret)
		return ret;

	len = recv(fd, resp, sizeof(resp), 0);
	if (len < 0)
		return -errno;

	for (nlh = (struct nlmsghdr *)resp; NLMSG_OK(nlh, len);
	     nlh = NLMSG_NEXT(nlh, len)) {
		if (nlh->nlmsg_type == NLMSG_ERROR) {
			struct nlmsgerr *err = NLMSG_DATA(nlh);

			return err->error ? err->error : -ENOENT;
		}

		genl = (struct genlmsghdr *)NLMSG_DATA(nlh);
		rem = nlh->nlmsg_len - NLMSG_HDRLEN - GENL_HDRLEN;
		na = (struct nlattr *)((char *)genl + GENL_HDRLEN);
		while (nla_ok(na, rem)) {
			if (na->nla_type == CTRL_ATTR_FAMILY_ID)
				return *(uint16_t *)nla_data(na);
			na = nla_next(na, &rem);
		}
	}

	return -ENOENT;
}
