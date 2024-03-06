// SPDX-License-Identifier: GPL-2.0-or-later
/* Taken & modified from iproute2's libnetlink.c
 * Authors: Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>

#include "netlink_helpers.h"

static int rcvbuf = 1024 * 1024;

void rtnl_close(struct rtnl_handle *rth)
{
	if (rth->fd >= 0) {
		close(rth->fd);
		rth->fd = -1;
	}
}

int rtnl_open_byproto(struct rtnl_handle *rth, unsigned int subscriptions,
		      int protocol)
{
	socklen_t addr_len;
	int sndbuf = 32768;
	int one = 1;

	memset(rth, 0, sizeof(*rth));
	rth->proto = protocol;
	rth->fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, protocol);
	if (rth->fd < 0) {
		perror("Cannot open netlink socket");
		return -1;
	}
	if (setsockopt(rth->fd, SOL_SOCKET, SO_SNDBUF,
		       &sndbuf, sizeof(sndbuf)) < 0) {
		perror("SO_SNDBUF");
		goto err;
	}
	if (setsockopt(rth->fd, SOL_SOCKET, SO_RCVBUF,
		       &rcvbuf, sizeof(rcvbuf)) < 0) {
		perror("SO_RCVBUF");
		goto err;
	}

	/* Older kernels may no support extended ACK reporting */
	setsockopt(rth->fd, SOL_NETLINK, NETLINK_EXT_ACK,
		   &one, sizeof(one));

	memset(&rth->local, 0, sizeof(rth->local));
	rth->local.nl_family = AF_NETLINK;
	rth->local.nl_groups = subscriptions;

	if (bind(rth->fd, (struct sockaddr *)&rth->local,
		 sizeof(rth->local)) < 0) {
		perror("Cannot bind netlink socket");
		goto err;
	}
	addr_len = sizeof(rth->local);
	if (getsockname(rth->fd, (struct sockaddr *)&rth->local,
			&addr_len) < 0) {
		perror("Cannot getsockname");
		goto err;
	}
	if (addr_len != sizeof(rth->local)) {
		fprintf(stderr, "Wrong address length %d\n", addr_len);
		goto err;
	}
	if (rth->local.nl_family != AF_NETLINK) {
		fprintf(stderr, "Wrong address family %d\n",
			rth->local.nl_family);
		goto err;
	}
	rth->seq = time(NULL);
	return 0;
err:
	rtnl_close(rth);
	return -1;
}

int rtnl_open(struct rtnl_handle *rth, unsigned int subscriptions)
{
	return rtnl_open_byproto(rth, subscriptions, NETLINK_ROUTE);
}

static int __rtnl_recvmsg(int fd, struct msghdr *msg, int flags)
{
	int len;

	do {
		len = recvmsg(fd, msg, flags);
	} while (len < 0 && (errno == EINTR || errno == EAGAIN));
	if (len < 0) {
		fprintf(stderr, "netlink receive error %s (%d)\n",
			strerror(errno), errno);
		return -errno;
	}
	if (len == 0) {
		fprintf(stderr, "EOF on netlink\n");
		return -ENODATA;
	}
	return len;
}

static int rtnl_recvmsg(int fd, struct msghdr *msg, char **answer)
{
	struct iovec *iov = msg->msg_iov;
	char *buf;
	int len;

	iov->iov_base = NULL;
	iov->iov_len = 0;

	len = __rtnl_recvmsg(fd, msg, MSG_PEEK | MSG_TRUNC);
	if (len < 0)
		return len;
	if (len < 32768)
		len = 32768;
	buf = malloc(len);
	if (!buf) {
		fprintf(stderr, "malloc error: not enough buffer\n");
		return -ENOMEM;
	}
	iov->iov_base = buf;
	iov->iov_len = len;
	len = __rtnl_recvmsg(fd, msg, 0);
	if (len < 0) {
		free(buf);
		return len;
	}
	if (answer)
		*answer = buf;
	else
		free(buf);
	return len;
}

static void rtnl_talk_error(struct nlmsghdr *h, struct nlmsgerr *err,
			    nl_ext_ack_fn_t errfn)
{
	fprintf(stderr, "RTNETLINK answers: %s\n",
		strerror(-err->error));
}

static int __rtnl_talk_iov(struct rtnl_handle *rtnl, struct iovec *iov,
			   size_t iovlen, struct nlmsghdr **answer,
			   bool show_rtnl_err, nl_ext_ack_fn_t errfn)
{
	struct sockaddr_nl nladdr = { .nl_family = AF_NETLINK };
	struct iovec riov;
	struct msghdr msg = {
		.msg_name	= &nladdr,
		.msg_namelen	= sizeof(nladdr),
		.msg_iov	= iov,
		.msg_iovlen	= iovlen,
	};
	unsigned int seq = 0;
	struct nlmsghdr *h;
	int i, status;
	char *buf;

	for (i = 0; i < iovlen; i++) {
		h = iov[i].iov_base;
		h->nlmsg_seq = seq = ++rtnl->seq;
		if (answer == NULL)
			h->nlmsg_flags |= NLM_F_ACK;
	}
	status = sendmsg(rtnl->fd, &msg, 0);
	if (status < 0) {
		perror("Cannot talk to rtnetlink");
		return -1;
	}
	/* change msg to use the response iov */
	msg.msg_iov = &riov;
	msg.msg_iovlen = 1;
	i = 0;
	while (1) {
next:
		status = rtnl_recvmsg(rtnl->fd, &msg, &buf);
		++i;
		if (status < 0)
			return status;
		if (msg.msg_namelen != sizeof(nladdr)) {
			fprintf(stderr,
				"Sender address length == %d!\n",
				msg.msg_namelen);
			exit(1);
		}
		for (h = (struct nlmsghdr *)buf; status >= sizeof(*h); ) {
			int len = h->nlmsg_len;
			int l = len - sizeof(*h);

			if (l < 0 || len > status) {
				if (msg.msg_flags & MSG_TRUNC) {
					fprintf(stderr, "Truncated message!\n");
					free(buf);
					return -1;
				}
				fprintf(stderr,
					"Malformed message: len=%d!\n",
					len);
				exit(1);
			}
			if (nladdr.nl_pid != 0 ||
			    h->nlmsg_pid != rtnl->local.nl_pid ||
			    h->nlmsg_seq > seq || h->nlmsg_seq < seq - iovlen) {
				/* Don't forget to skip that message. */
				status -= NLMSG_ALIGN(len);
				h = (struct nlmsghdr *)((char *)h + NLMSG_ALIGN(len));
				continue;
			}
			if (h->nlmsg_type == NLMSG_ERROR) {
				struct nlmsgerr *err = (struct nlmsgerr *)NLMSG_DATA(h);
				int error = err->error;

				if (l < sizeof(struct nlmsgerr)) {
					fprintf(stderr, "ERROR truncated\n");
					free(buf);
					return -1;
				}
				if (error) {
					errno = -error;
					if (rtnl->proto != NETLINK_SOCK_DIAG &&
					    show_rtnl_err)
						rtnl_talk_error(h, err, errfn);
				}
				if (i < iovlen) {
					free(buf);
					goto next;
				}
				if (error) {
					free(buf);
					return -i;
				}
				if (answer)
					*answer = (struct nlmsghdr *)buf;
				else
					free(buf);
				return 0;
			}
			if (answer) {
				*answer = (struct nlmsghdr *)buf;
				return 0;
			}
			fprintf(stderr, "Unexpected reply!\n");
			status -= NLMSG_ALIGN(len);
			h = (struct nlmsghdr *)((char *)h + NLMSG_ALIGN(len));
		}
		free(buf);
		if (msg.msg_flags & MSG_TRUNC) {
			fprintf(stderr, "Message truncated!\n");
			continue;
		}
		if (status) {
			fprintf(stderr, "Remnant of size %d!\n", status);
			exit(1);
		}
	}
}

static int __rtnl_talk(struct rtnl_handle *rtnl, struct nlmsghdr *n,
		       struct nlmsghdr **answer, bool show_rtnl_err,
		       nl_ext_ack_fn_t errfn)
{
	struct iovec iov = {
		.iov_base	= n,
		.iov_len	= n->nlmsg_len,
	};

	return __rtnl_talk_iov(rtnl, &iov, 1, answer, show_rtnl_err, errfn);
}

int rtnl_talk(struct rtnl_handle *rtnl, struct nlmsghdr *n,
	      struct nlmsghdr **answer)
{
	return __rtnl_talk(rtnl, n, answer, true, NULL);
}

int addattr(struct nlmsghdr *n, int maxlen, int type)
{
	return addattr_l(n, maxlen, type, NULL, 0);
}

int addattr8(struct nlmsghdr *n, int maxlen, int type, __u8 data)
{
	return addattr_l(n, maxlen, type, &data, sizeof(__u8));
}

int addattr16(struct nlmsghdr *n, int maxlen, int type, __u16 data)
{
	return addattr_l(n, maxlen, type, &data, sizeof(__u16));
}

int addattr32(struct nlmsghdr *n, int maxlen, int type, __u32 data)
{
	return addattr_l(n, maxlen, type, &data, sizeof(__u32));
}

int addattr64(struct nlmsghdr *n, int maxlen, int type, __u64 data)
{
	return addattr_l(n, maxlen, type, &data, sizeof(__u64));
}

int addattrstrz(struct nlmsghdr *n, int maxlen, int type, const char *str)
{
	return addattr_l(n, maxlen, type, str, strlen(str)+1);
}

int addattr_l(struct nlmsghdr *n, int maxlen, int type, const void *data,
	      int alen)
{
	int len = RTA_LENGTH(alen);
	struct rtattr *rta;

	if (NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(len) > maxlen) {
		fprintf(stderr, "%s: Message exceeded bound of %d\n",
			__func__, maxlen);
		return -1;
	}
	rta = NLMSG_TAIL(n);
	rta->rta_type = type;
	rta->rta_len = len;
	if (alen)
		memcpy(RTA_DATA(rta), data, alen);
	n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(len);
	return 0;
}

int addraw_l(struct nlmsghdr *n, int maxlen, const void *data, int len)
{
	if (NLMSG_ALIGN(n->nlmsg_len) + NLMSG_ALIGN(len) > maxlen) {
		fprintf(stderr, "%s: Message exceeded bound of %d\n",
			__func__, maxlen);
		return -1;
	}

	memcpy(NLMSG_TAIL(n), data, len);
	memset((void *) NLMSG_TAIL(n) + len, 0, NLMSG_ALIGN(len) - len);
	n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len) + NLMSG_ALIGN(len);
	return 0;
}

struct rtattr *addattr_nest(struct nlmsghdr *n, int maxlen, int type)
{
	struct rtattr *nest = NLMSG_TAIL(n);

	addattr_l(n, maxlen, type, NULL, 0);
	return nest;
}

int addattr_nest_end(struct nlmsghdr *n, struct rtattr *nest)
{
	nest->rta_len = (void *)NLMSG_TAIL(n) - (void *)nest;
	return n->nlmsg_len;
}
