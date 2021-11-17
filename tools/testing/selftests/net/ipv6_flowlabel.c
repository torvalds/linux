// SPDX-License-Identifier: GPL-2.0
/* Test IPV6_FLOWINFO cmsg on send and recv */

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <asm/byteorder.h>
#include <error.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/in6.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

/* uapi/glibc weirdness may leave this undefined */
#ifndef IPV6_FLOWINFO
#define IPV6_FLOWINFO 11
#endif

#ifndef IPV6_FLOWLABEL_MGR
#define IPV6_FLOWLABEL_MGR 32
#endif

#define FLOWLABEL_WILDCARD	((uint32_t) -1)

static const char cfg_data[]	= "a";
static uint32_t cfg_label	= 1;

static void do_send(int fd, bool with_flowlabel, uint32_t flowlabel)
{
	char control[CMSG_SPACE(sizeof(flowlabel))] = {0};
	struct msghdr msg = {0};
	struct iovec iov = {0};
	int ret;

	iov.iov_base = (char *)cfg_data;
	iov.iov_len = sizeof(cfg_data);

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	if (with_flowlabel) {
		struct cmsghdr *cm;

		cm = (void *)control;
		cm->cmsg_len = CMSG_LEN(sizeof(flowlabel));
		cm->cmsg_level = SOL_IPV6;
		cm->cmsg_type = IPV6_FLOWINFO;
		*(uint32_t *)CMSG_DATA(cm) = htonl(flowlabel);

		msg.msg_control = control;
		msg.msg_controllen = sizeof(control);
	}

	ret = sendmsg(fd, &msg, 0);
	if (ret == -1)
		error(1, errno, "send");

	if (with_flowlabel)
		fprintf(stderr, "sent with label %u\n", flowlabel);
	else
		fprintf(stderr, "sent without label\n");
}

static void do_recv(int fd, bool with_flowlabel, uint32_t expect)
{
	char control[CMSG_SPACE(sizeof(expect))];
	char data[sizeof(cfg_data)];
	struct msghdr msg = {0};
	struct iovec iov = {0};
	struct cmsghdr *cm;
	uint32_t flowlabel;
	int ret;

	iov.iov_base = data;
	iov.iov_len = sizeof(data);

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	memset(control, 0, sizeof(control));
	msg.msg_control = control;
	msg.msg_controllen = sizeof(control);

	ret = recvmsg(fd, &msg, 0);
	if (ret == -1)
		error(1, errno, "recv");
	if (msg.msg_flags & (MSG_TRUNC | MSG_CTRUNC))
		error(1, 0, "recv: truncated");
	if (ret != sizeof(cfg_data))
		error(1, 0, "recv: length mismatch");
	if (memcmp(data, cfg_data, sizeof(data)))
		error(1, 0, "recv: data mismatch");

	cm = CMSG_FIRSTHDR(&msg);
	if (with_flowlabel) {
		if (!cm)
			error(1, 0, "recv: missing cmsg");
		if (CMSG_NXTHDR(&msg, cm))
			error(1, 0, "recv: too many cmsg");
		if (cm->cmsg_level != SOL_IPV6 ||
		    cm->cmsg_type != IPV6_FLOWINFO)
			error(1, 0, "recv: unexpected cmsg level or type");

		flowlabel = ntohl(*(uint32_t *)CMSG_DATA(cm));
		fprintf(stderr, "recv with label %u\n", flowlabel);

		if (expect != FLOWLABEL_WILDCARD && expect != flowlabel)
			fprintf(stderr, "recv: incorrect flowlabel %u != %u\n",
					flowlabel, expect);

	} else {
		fprintf(stderr, "recv without label\n");
	}
}

static bool get_autoflowlabel_enabled(void)
{
	int fd, ret;
	char val;

	fd = open("/proc/sys/net/ipv6/auto_flowlabels", O_RDONLY);
	if (fd == -1)
		error(1, errno, "open sysctl");

	ret = read(fd, &val, 1);
	if (ret == -1)
		error(1, errno, "read sysctl");
	if (ret == 0)
		error(1, 0, "read sysctl: 0");

	if (close(fd))
		error(1, errno, "close sysctl");

	return val == '1';
}

static void flowlabel_get(int fd, uint32_t label, uint8_t share, uint16_t flags)
{
	struct in6_flowlabel_req req = {
		.flr_action = IPV6_FL_A_GET,
		.flr_label = htonl(label),
		.flr_flags = flags,
		.flr_share = share,
	};

	/* do not pass IPV6_ADDR_ANY or IPV6_ADDR_MAPPED */
	req.flr_dst.s6_addr[0] = 0xfd;
	req.flr_dst.s6_addr[15] = 0x1;

	if (setsockopt(fd, SOL_IPV6, IPV6_FLOWLABEL_MGR, &req, sizeof(req)))
		error(1, errno, "setsockopt flowlabel get");
}

static void parse_opts(int argc, char **argv)
{
	int c;

	while ((c = getopt(argc, argv, "l:")) != -1) {
		switch (c) {
		case 'l':
			cfg_label = strtoul(optarg, NULL, 0);
			break;
		default:
			error(1, 0, "%s: parse error", argv[0]);
		}
	}
}

int main(int argc, char **argv)
{
	struct sockaddr_in6 addr = {
		.sin6_family = AF_INET6,
		.sin6_port = htons(8000),
		.sin6_addr = IN6ADDR_LOOPBACK_INIT,
	};
	const int one = 1;
	int fdt, fdr;

	parse_opts(argc, argv);

	fdt = socket(PF_INET6, SOCK_DGRAM, 0);
	if (fdt == -1)
		error(1, errno, "socket t");

	fdr = socket(PF_INET6, SOCK_DGRAM, 0);
	if (fdr == -1)
		error(1, errno, "socket r");

	if (connect(fdt, (void *)&addr, sizeof(addr)))
		error(1, errno, "connect");
	if (bind(fdr, (void *)&addr, sizeof(addr)))
		error(1, errno, "bind");

	flowlabel_get(fdt, cfg_label, IPV6_FL_S_EXCL, IPV6_FL_F_CREATE);

	if (setsockopt(fdr, SOL_IPV6, IPV6_FLOWINFO, &one, sizeof(one)))
		error(1, errno, "setsockopt flowinfo");

	if (get_autoflowlabel_enabled()) {
		fprintf(stderr, "send no label: recv auto flowlabel\n");
		do_send(fdt, false, 0);
		do_recv(fdr, true, FLOWLABEL_WILDCARD);
	} else {
		fprintf(stderr, "send no label: recv no label (auto off)\n");
		do_send(fdt, false, 0);
		do_recv(fdr, false, 0);
	}

	fprintf(stderr, "send label\n");
	do_send(fdt, true, cfg_label);
	do_recv(fdr, true, cfg_label);

	if (close(fdr))
		error(1, errno, "close r");
	if (close(fdt))
		error(1, errno, "close t");

	return 0;
}
