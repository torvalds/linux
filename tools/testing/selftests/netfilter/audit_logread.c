// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <linux/audit.h>
#include <linux/netlink.h>

static int fd;

#define MAX_AUDIT_MESSAGE_LENGTH	8970
struct audit_message {
	struct nlmsghdr nlh;
	union {
		struct audit_status s;
		char data[MAX_AUDIT_MESSAGE_LENGTH];
	} u;
};

int audit_recv(int fd, struct audit_message *rep)
{
	struct sockaddr_nl addr;
	socklen_t addrlen = sizeof(addr);
	int ret;

	do {
		ret = recvfrom(fd, rep, sizeof(*rep), 0,
			       (struct sockaddr *)&addr, &addrlen);
	} while (ret < 0 && errno == EINTR);

	if (ret < 0 ||
	    addrlen != sizeof(addr) ||
	    addr.nl_pid != 0 ||
	    rep->nlh.nlmsg_type == NLMSG_ERROR) /* short-cut for now */
		return -1;

	return ret;
}

int audit_send(int fd, uint16_t type, uint32_t key, uint32_t val)
{
	static int seq = 0;
	struct audit_message msg = {
		.nlh = {
			.nlmsg_len   = NLMSG_SPACE(sizeof(msg.u.s)),
			.nlmsg_type  = type,
			.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK,
			.nlmsg_seq   = ++seq,
		},
		.u.s = {
			.mask    = key,
			.enabled = key == AUDIT_STATUS_ENABLED ? val : 0,
			.pid     = key == AUDIT_STATUS_PID ? val : 0,
		}
	};
	struct sockaddr_nl addr = {
		.nl_family = AF_NETLINK,
	};
	int ret;

	do {
		ret = sendto(fd, &msg, msg.nlh.nlmsg_len, 0,
			     (struct sockaddr *)&addr, sizeof(addr));
	} while (ret < 0 && errno == EINTR);

	if (ret != (int)msg.nlh.nlmsg_len)
		return -1;
	return 0;
}

int audit_set(int fd, uint32_t key, uint32_t val)
{
	struct audit_message rep = { 0 };
	int ret;

	ret = audit_send(fd, AUDIT_SET, key, val);
	if (ret)
		return ret;

	ret = audit_recv(fd, &rep);
	if (ret < 0)
		return ret;
	return 0;
}

int readlog(int fd)
{
	struct audit_message rep = { 0 };
	int ret = audit_recv(fd, &rep);
	const char *sep = "";
	char *k, *v;

	if (ret < 0)
		return ret;

	if (rep.nlh.nlmsg_type != AUDIT_NETFILTER_CFG)
		return 0;

	/* skip the initial "audit(...): " part */
	strtok(rep.u.data, " ");

	while ((k = strtok(NULL, "="))) {
		v = strtok(NULL, " ");

		/* these vary and/or are uninteresting, ignore */
		if (!strcmp(k, "pid") ||
		    !strcmp(k, "comm") ||
		    !strcmp(k, "subj"))
			continue;

		/* strip the varying sequence number */
		if (!strcmp(k, "table"))
			*strchrnul(v, ':') = '\0';

		printf("%s%s=%s", sep, k, v);
		sep = " ";
	}
	if (*sep) {
		printf("\n");
		fflush(stdout);
	}
	return 0;
}

void cleanup(int sig)
{
	audit_set(fd, AUDIT_STATUS_ENABLED, 0);
	close(fd);
	if (sig)
		exit(0);
}

int main(int argc, char **argv)
{
	struct sigaction act = {
		.sa_handler = cleanup,
	};

	fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_AUDIT);
	if (fd < 0) {
		perror("Can't open netlink socket");
		return -1;
	}

	if (sigaction(SIGTERM, &act, NULL) < 0 ||
	    sigaction(SIGINT, &act, NULL) < 0) {
		perror("Can't set signal handler");
		close(fd);
		return -1;
	}

	audit_set(fd, AUDIT_STATUS_ENABLED, 1);
	audit_set(fd, AUDIT_STATUS_PID, getpid());

	while (1)
		readlog(fd);
}
