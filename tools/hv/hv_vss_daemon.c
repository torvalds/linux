/*
 * An implementation of the host initiated guest snapshot for Hyper-V.
 *
 *
 * Copyright (C) 2013, Microsoft, Inc.
 * Author : K. Y. Srinivasan <kys@microsoft.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 */


#include <sys/types.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdio.h>
#include <mntent.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <arpa/inet.h>
#include <linux/fs.h>
#include <linux/connector.h>
#include <linux/hyperv.h>
#include <linux/netlink.h>
#include <syslog.h>

static struct sockaddr_nl addr;

#ifndef SOL_NETLINK
#define SOL_NETLINK 270
#endif


static int vss_do_freeze(char *dir, unsigned int cmd, char *fs_op)
{
	int ret, fd = open(dir, O_RDONLY);

	if (fd < 0)
		return 1;
	ret = ioctl(fd, cmd, 0);
	syslog(LOG_INFO, "VSS: %s of %s: %s\n", fs_op, dir, strerror(errno));
	close(fd);
	return !!ret;
}

static int vss_operate(int operation)
{
	char *fs_op;
	char match[] = "/dev/";
	FILE *mounts;
	struct mntent *ent;
	unsigned int cmd;
	int error = 0, root_seen = 0;

	switch (operation) {
	case VSS_OP_FREEZE:
		cmd = FIFREEZE;
		fs_op = "freeze";
		break;
	case VSS_OP_THAW:
		cmd = FITHAW;
		fs_op = "thaw";
		break;
	default:
		return -1;
	}

	mounts = setmntent("/proc/mounts", "r");
	if (mounts == NULL)
		return -1;

	while ((ent = getmntent(mounts))) {
		if (strncmp(ent->mnt_fsname, match, strlen(match)))
			continue;
		if (strcmp(ent->mnt_type, "iso9660") == 0)
			continue;
		if (strcmp(ent->mnt_type, "vfat") == 0)
			continue;
		if (strcmp(ent->mnt_dir, "/") == 0) {
			root_seen = 1;
			continue;
		}
		error |= vss_do_freeze(ent->mnt_dir, cmd, fs_op);
	}
	endmntent(mounts);

	if (root_seen) {
		error |= vss_do_freeze("/", cmd, fs_op);
	}

	return error;
}

static int netlink_send(int fd, struct cn_msg *msg)
{
	struct nlmsghdr nlh = { .nlmsg_type = NLMSG_DONE };
	unsigned int size;
	struct msghdr message;
	struct iovec iov[2];

	size = sizeof(struct cn_msg) + msg->len;

	nlh.nlmsg_pid = getpid();
	nlh.nlmsg_len = NLMSG_LENGTH(size);

	iov[0].iov_base = &nlh;
	iov[0].iov_len = sizeof(nlh);

	iov[1].iov_base = msg;
	iov[1].iov_len = size;

	memset(&message, 0, sizeof(message));
	message.msg_name = &addr;
	message.msg_namelen = sizeof(addr);
	message.msg_iov = iov;
	message.msg_iovlen = 2;

	return sendmsg(fd, &message, 0);
}

int main(void)
{
	int fd, len, nl_group;
	int error;
	struct cn_msg *message;
	struct pollfd pfd;
	struct nlmsghdr *incoming_msg;
	struct cn_msg	*incoming_cn_msg;
	int	op;
	struct hv_vss_msg *vss_msg;
	char *vss_recv_buffer;
	size_t vss_recv_buffer_len;

	if (daemon(1, 0))
		return 1;

	openlog("Hyper-V VSS", 0, LOG_USER);
	syslog(LOG_INFO, "VSS starting; pid is:%d", getpid());

	vss_recv_buffer_len = NLMSG_LENGTH(0) + sizeof(struct cn_msg) + sizeof(struct hv_vss_msg);
	vss_recv_buffer = calloc(1, vss_recv_buffer_len);
	if (!vss_recv_buffer) {
		syslog(LOG_ERR, "Failed to allocate netlink buffers");
		exit(EXIT_FAILURE);
	}

	fd = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR);
	if (fd < 0) {
		syslog(LOG_ERR, "netlink socket creation failed; error:%d %s",
				errno, strerror(errno));
		exit(EXIT_FAILURE);
	}
	addr.nl_family = AF_NETLINK;
	addr.nl_pad = 0;
	addr.nl_pid = 0;
	addr.nl_groups = 0;


	error = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
	if (error < 0) {
		syslog(LOG_ERR, "bind failed; error:%d %s", errno, strerror(errno));
		close(fd);
		exit(EXIT_FAILURE);
	}
	nl_group = CN_VSS_IDX;
	if (setsockopt(fd, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP, &nl_group, sizeof(nl_group)) < 0) {
		syslog(LOG_ERR, "setsockopt failed; error:%d %s", errno, strerror(errno));
		close(fd);
		exit(EXIT_FAILURE);
	}
	/*
	 * Register ourselves with the kernel.
	 */
	message = (struct cn_msg *)vss_recv_buffer;
	message->id.idx = CN_VSS_IDX;
	message->id.val = CN_VSS_VAL;
	message->ack = 0;
	vss_msg = (struct hv_vss_msg *)message->data;
	vss_msg->vss_hdr.operation = VSS_OP_REGISTER;

	message->len = sizeof(struct hv_vss_msg);

	len = netlink_send(fd, message);
	if (len < 0) {
		syslog(LOG_ERR, "netlink_send failed; error:%d %s", errno, strerror(errno));
		close(fd);
		exit(EXIT_FAILURE);
	}

	pfd.fd = fd;

	while (1) {
		struct sockaddr *addr_p = (struct sockaddr *) &addr;
		socklen_t addr_l = sizeof(addr);
		pfd.events = POLLIN;
		pfd.revents = 0;

		if (poll(&pfd, 1, -1) < 0) {
			syslog(LOG_ERR, "poll failed; error:%d %s", errno, strerror(errno));
			if (errno == EINVAL) {
				close(fd);
				exit(EXIT_FAILURE);
			}
			else
				continue;
		}

		len = recvfrom(fd, vss_recv_buffer, vss_recv_buffer_len, 0,
				addr_p, &addr_l);

		if (len < 0) {
			syslog(LOG_ERR, "recvfrom failed; pid:%u error:%d %s",
					addr.nl_pid, errno, strerror(errno));
			close(fd);
			return -1;
		}

		if (addr.nl_pid) {
			syslog(LOG_WARNING,
				"Received packet from untrusted pid:%u",
				addr.nl_pid);
			continue;
		}

		incoming_msg = (struct nlmsghdr *)vss_recv_buffer;

		if (incoming_msg->nlmsg_type != NLMSG_DONE)
			continue;

		incoming_cn_msg = (struct cn_msg *)NLMSG_DATA(incoming_msg);
		vss_msg = (struct hv_vss_msg *)incoming_cn_msg->data;
		op = vss_msg->vss_hdr.operation;
		error =  HV_S_OK;

		switch (op) {
		case VSS_OP_FREEZE:
		case VSS_OP_THAW:
			error = vss_operate(op);
			if (error)
				error = HV_E_FAIL;
			break;
		default:
			syslog(LOG_ERR, "Illegal op:%d\n", op);
		}
		vss_msg->error = error;
		len = netlink_send(fd, incoming_cn_msg);
		if (len < 0) {
			syslog(LOG_ERR, "net_link send failed; error:%d %s",
					errno, strerror(errno));
			exit(EXIT_FAILURE);
		}
	}

}
