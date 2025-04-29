// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025, Kylin Software */

#include <linux/sock_diag.h>
#include <linux/rtnetlink.h>
#include <linux/inet_diag.h>
#include <linux/netlink.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <linux/tcp.h>

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#ifndef IPPROTO_MPTCP
#define IPPROTO_MPTCP 262
#endif

struct mptcp_info {
	__u8	mptcpi_subflows;
	__u8	mptcpi_add_addr_signal;
	__u8	mptcpi_add_addr_accepted;
	__u8	mptcpi_subflows_max;
	__u8	mptcpi_add_addr_signal_max;
	__u8	mptcpi_add_addr_accepted_max;
	__u32	mptcpi_flags;
	__u32	mptcpi_token;
	__u64	mptcpi_write_seq;
	__u64	mptcpi_snd_una;
	__u64	mptcpi_rcv_nxt;
	__u8	mptcpi_local_addr_used;
	__u8	mptcpi_local_addr_max;
	__u8	mptcpi_csum_enabled;
	__u32	mptcpi_retransmits;
	__u64	mptcpi_bytes_retrans;
	__u64	mptcpi_bytes_sent;
	__u64	mptcpi_bytes_received;
	__u64	mptcpi_bytes_acked;
	__u8	mptcpi_subflows_total;
	__u8	reserved[3];
	__u32	mptcpi_last_data_sent;
	__u32	mptcpi_last_data_recv;
	__u32	mptcpi_last_ack_recv;
};

static void die_perror(const char *msg)
{
	perror(msg);
	exit(1);
}

static void die_usage(int r)
{
	fprintf(stderr, "Usage: mptcp_diag -t\n");
	exit(r);
}

static void send_query(int fd, __u32 token)
{
	struct sockaddr_nl nladdr = {
		.nl_family = AF_NETLINK
	};
	struct {
		struct nlmsghdr nlh;
		struct inet_diag_req_v2 r;
	} req = {
		.nlh = {
			.nlmsg_len = sizeof(req),
			.nlmsg_type = SOCK_DIAG_BY_FAMILY,
			.nlmsg_flags = NLM_F_REQUEST
		},
		.r = {
			.sdiag_family = AF_INET,
			/* Real proto is set via INET_DIAG_REQ_PROTOCOL */
			.sdiag_protocol = IPPROTO_TCP,
			.id.idiag_cookie[0] = token,
		}
	};
	struct rtattr rta_proto;
	struct iovec iov[6];
	int iovlen = 1;
	__u32 proto;

	req.r.idiag_ext |= (1 << (INET_DIAG_INFO - 1));
	proto = IPPROTO_MPTCP;
	rta_proto.rta_type = INET_DIAG_REQ_PROTOCOL;
	rta_proto.rta_len = RTA_LENGTH(sizeof(proto));

	iov[0] = (struct iovec) {
		.iov_base = &req,
		.iov_len = sizeof(req)
	};
	iov[iovlen] = (struct iovec){ &rta_proto, sizeof(rta_proto)};
	iov[iovlen + 1] = (struct iovec){ &proto, sizeof(proto)};
	req.nlh.nlmsg_len += RTA_LENGTH(sizeof(proto));
	iovlen += 2;
	struct msghdr msg = {
		.msg_name = &nladdr,
		.msg_namelen = sizeof(nladdr),
		.msg_iov = iov,
		.msg_iovlen = iovlen
	};

	for (;;) {
		if (sendmsg(fd, &msg, 0) < 0) {
			if (errno == EINTR)
				continue;
			die_perror("sendmsg");
		}
		break;
	}
}

static void parse_rtattr_flags(struct rtattr *tb[], int max, struct rtattr *rta,
			       int len, unsigned short flags)
{
	unsigned short type;

	memset(tb, 0, sizeof(struct rtattr *) * (max + 1));
	while (RTA_OK(rta, len)) {
		type = rta->rta_type & ~flags;
		if (type <= max && !tb[type])
			tb[type] = rta;
		rta = RTA_NEXT(rta, len);
	}
}

static void print_info_msg(struct mptcp_info *info)
{
	printf("Token & Flags\n");
	printf("token:        %x\n", info->mptcpi_token);
	printf("flags:        %x\n", info->mptcpi_flags);
	printf("csum_enabled: %u\n", info->mptcpi_csum_enabled);

	printf("\nBasic Info\n");
	printf("subflows:              %u\n", info->mptcpi_subflows);
	printf("subflows_max:          %u\n", info->mptcpi_subflows_max);
	printf("subflows_total:        %u\n", info->mptcpi_subflows_total);
	printf("local_addr_used:       %u\n", info->mptcpi_local_addr_used);
	printf("local_addr_max:        %u\n", info->mptcpi_local_addr_max);
	printf("add_addr_signal:       %u\n", info->mptcpi_add_addr_signal);
	printf("add_addr_accepted:     %u\n", info->mptcpi_add_addr_accepted);
	printf("add_addr_signal_max:   %u\n", info->mptcpi_add_addr_signal_max);
	printf("add_addr_accepted_max: %u\n", info->mptcpi_add_addr_accepted_max);

	printf("\nTransmission Info\n");
	printf("write_seq:        %llu\n", info->mptcpi_write_seq);
	printf("snd_una:          %llu\n", info->mptcpi_snd_una);
	printf("rcv_nxt:          %llu\n", info->mptcpi_rcv_nxt);
	printf("last_data_sent:   %u\n", info->mptcpi_last_data_sent);
	printf("last_data_recv:   %u\n", info->mptcpi_last_data_recv);
	printf("last_ack_recv:    %u\n", info->mptcpi_last_ack_recv);
	printf("retransmits:      %u\n", info->mptcpi_retransmits);
	printf("retransmit bytes: %llu\n", info->mptcpi_bytes_retrans);
	printf("bytes_sent:       %llu\n", info->mptcpi_bytes_sent);
	printf("bytes_received:   %llu\n", info->mptcpi_bytes_received);
	printf("bytes_acked:      %llu\n", info->mptcpi_bytes_acked);
}

static void parse_nlmsg(struct nlmsghdr *nlh)
{
	struct inet_diag_msg *r = NLMSG_DATA(nlh);
	struct rtattr *tb[INET_DIAG_MAX + 1];

	parse_rtattr_flags(tb, INET_DIAG_MAX, (struct rtattr *)(r + 1),
			   nlh->nlmsg_len - NLMSG_LENGTH(sizeof(*r)),
			   NLA_F_NESTED);

	if (tb[INET_DIAG_INFO]) {
		int len = RTA_PAYLOAD(tb[INET_DIAG_INFO]);
		struct mptcp_info *info;

		/* workaround fort older kernels with less fields */
		if (len < sizeof(*info)) {
			info = alloca(sizeof(*info));
			memcpy(info, RTA_DATA(tb[INET_DIAG_INFO]), len);
			memset((char *)info + len, 0, sizeof(*info) - len);
		} else {
			info = RTA_DATA(tb[INET_DIAG_INFO]);
		}
		print_info_msg(info);
	}
}

static void recv_nlmsg(int fd, struct nlmsghdr *nlh)
{
	char rcv_buff[8192];
	struct sockaddr_nl rcv_nladdr = {
		.nl_family = AF_NETLINK
	};
	struct iovec rcv_iov = {
		.iov_base = rcv_buff,
		.iov_len = sizeof(rcv_buff)
	};
	struct msghdr rcv_msg = {
		.msg_name = &rcv_nladdr,
		.msg_namelen = sizeof(rcv_nladdr),
		.msg_iov = &rcv_iov,
		.msg_iovlen = 1
	};
	int len;

	len = recvmsg(fd, &rcv_msg, 0);
	nlh = (struct nlmsghdr *)rcv_buff;

	while (NLMSG_OK(nlh, len)) {
		if (nlh->nlmsg_type == NLMSG_DONE) {
			printf("NLMSG_DONE\n");
			break;
		} else if (nlh->nlmsg_type == NLMSG_ERROR) {
			struct nlmsgerr *err;

			err = (struct nlmsgerr *)NLMSG_DATA(nlh);
			printf("Error %d:%s\n",
			       -(err->error), strerror(-(err->error)));
			break;
		}
		parse_nlmsg(nlh);
		nlh = NLMSG_NEXT(nlh, len);
	}
}

static void get_mptcpinfo(__u32 token)
{
	struct nlmsghdr *nlh = NULL;
	int fd;

	fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_SOCK_DIAG);
	if (fd < 0)
		die_perror("Netlink socket");

	send_query(fd, token);
	recv_nlmsg(fd, nlh);

	close(fd);
}

static void parse_opts(int argc, char **argv, __u32 *target_token)
{
	int c;

	if (argc < 2)
		die_usage(1);

	while ((c = getopt(argc, argv, "ht:")) != -1) {
		switch (c) {
		case 'h':
			die_usage(0);
			break;
		case 't':
			sscanf(optarg, "%x", target_token);
			break;
		default:
			die_usage(1);
			break;
		}
	}
}

int main(int argc, char *argv[])
{
	__u32 target_token;

	parse_opts(argc, argv, &target_token);
	get_mptcpinfo(target_token);

	return 0;
}

