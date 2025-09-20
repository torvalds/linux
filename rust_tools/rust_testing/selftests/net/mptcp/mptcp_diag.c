// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025, Kylin Software */

#include <linux/sock_diag.h>
#include <linux/rtnetlink.h>
#include <linux/inet_diag.h>
#include <linux/netlink.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <linux/tcp.h>
#include <arpa/inet.h>

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#ifndef IPPROTO_MPTCP
#define IPPROTO_MPTCP 262
#endif

#define parse_rtattr_nested(tb, max, rta) \
	(parse_rtattr_flags((tb), (max), RTA_DATA(rta), RTA_PAYLOAD(rta), \
			    NLA_F_NESTED))

struct params {
	__u32 target_token;
	char subflow_addrs[1024];
};

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

enum {
	MPTCP_SUBFLOW_ATTR_UNSPEC,
	MPTCP_SUBFLOW_ATTR_TOKEN_REM,
	MPTCP_SUBFLOW_ATTR_TOKEN_LOC,
	MPTCP_SUBFLOW_ATTR_RELWRITE_SEQ,
	MPTCP_SUBFLOW_ATTR_MAP_SEQ,
	MPTCP_SUBFLOW_ATTR_MAP_SFSEQ,
	MPTCP_SUBFLOW_ATTR_SSN_OFFSET,
	MPTCP_SUBFLOW_ATTR_MAP_DATALEN,
	MPTCP_SUBFLOW_ATTR_FLAGS,
	MPTCP_SUBFLOW_ATTR_ID_REM,
	MPTCP_SUBFLOW_ATTR_ID_LOC,
	MPTCP_SUBFLOW_ATTR_PAD,

	__MPTCP_SUBFLOW_ATTR_MAX
};

#define MPTCP_SUBFLOW_ATTR_MAX (__MPTCP_SUBFLOW_ATTR_MAX - 1)

#define MPTCP_SUBFLOW_FLAG_MCAP_REM		_BITUL(0)
#define MPTCP_SUBFLOW_FLAG_MCAP_LOC		_BITUL(1)
#define MPTCP_SUBFLOW_FLAG_JOIN_REM		_BITUL(2)
#define MPTCP_SUBFLOW_FLAG_JOIN_LOC		_BITUL(3)
#define MPTCP_SUBFLOW_FLAG_BKUP_REM		_BITUL(4)
#define MPTCP_SUBFLOW_FLAG_BKUP_LOC		_BITUL(5)
#define MPTCP_SUBFLOW_FLAG_FULLY_ESTABLISHED	_BITUL(6)
#define MPTCP_SUBFLOW_FLAG_CONNECTED		_BITUL(7)
#define MPTCP_SUBFLOW_FLAG_MAPVALID		_BITUL(8)

#define rta_getattr(type, value)		(*(type *)RTA_DATA(value))

static void die_perror(const char *msg)
{
	perror(msg);
	exit(1);
}

static void die_usage(int r)
{
	fprintf(stderr, "Usage:\n"
			"mptcp_diag -t <token>\n"
			"mptcp_diag -s \"<saddr>:<sport> <daddr>:<dport>\"\n");
	exit(r);
}

static void send_query(int fd, struct inet_diag_req_v2 *r, __u32 proto)
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
		.r = *r
	};
	struct rtattr rta_proto;
	struct iovec iov[6];
	int iovlen = 0;

	iov[iovlen++] = (struct iovec) {
		.iov_base = &req,
		.iov_len = sizeof(req)
	};

	if (proto == IPPROTO_MPTCP) {
		rta_proto.rta_type = INET_DIAG_REQ_PROTOCOL;
		rta_proto.rta_len = RTA_LENGTH(sizeof(proto));

		iov[iovlen++] = (struct iovec){ &rta_proto, sizeof(rta_proto)};
		iov[iovlen++] = (struct iovec){ &proto, sizeof(proto)};
		req.nlh.nlmsg_len += RTA_LENGTH(sizeof(proto));
	}

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

/*
 * 'print_subflow_info' is from 'mptcp_subflow_info'
 * which is a function in 'misc/ss.c' of iproute2.
 */
static void print_subflow_info(struct rtattr *tb[])
{
	u_int32_t flags = 0;

	printf("It's a mptcp subflow, the subflow info:\n");
	if (tb[MPTCP_SUBFLOW_ATTR_FLAGS]) {
		char caps[32 + 1] = { 0 }, *cap = &caps[0];

		flags = rta_getattr(__u32, tb[MPTCP_SUBFLOW_ATTR_FLAGS]);

		if (flags & MPTCP_SUBFLOW_FLAG_MCAP_REM)
			*cap++ = 'M';
		if (flags & MPTCP_SUBFLOW_FLAG_MCAP_LOC)
			*cap++ = 'm';
		if (flags & MPTCP_SUBFLOW_FLAG_JOIN_REM)
			*cap++ = 'J';
		if (flags & MPTCP_SUBFLOW_FLAG_JOIN_LOC)
			*cap++ = 'j';
		if (flags & MPTCP_SUBFLOW_FLAG_BKUP_REM)
			*cap++ = 'B';
		if (flags & MPTCP_SUBFLOW_FLAG_BKUP_LOC)
			*cap++ = 'b';
		if (flags & MPTCP_SUBFLOW_FLAG_FULLY_ESTABLISHED)
			*cap++ = 'e';
		if (flags & MPTCP_SUBFLOW_FLAG_CONNECTED)
			*cap++ = 'c';
		if (flags & MPTCP_SUBFLOW_FLAG_MAPVALID)
			*cap++ = 'v';

		if (flags)
			printf(" flags:%s", caps);
	}
	if (tb[MPTCP_SUBFLOW_ATTR_TOKEN_REM] &&
	    tb[MPTCP_SUBFLOW_ATTR_TOKEN_LOC] &&
	    tb[MPTCP_SUBFLOW_ATTR_ID_REM] &&
	    tb[MPTCP_SUBFLOW_ATTR_ID_LOC])
		printf(" token:%04x(id:%u)/%04x(id:%u)",
		       rta_getattr(__u32, tb[MPTCP_SUBFLOW_ATTR_TOKEN_REM]),
		       rta_getattr(__u8, tb[MPTCP_SUBFLOW_ATTR_ID_REM]),
		       rta_getattr(__u32, tb[MPTCP_SUBFLOW_ATTR_TOKEN_LOC]),
		       rta_getattr(__u8, tb[MPTCP_SUBFLOW_ATTR_ID_LOC]));
	if (tb[MPTCP_SUBFLOW_ATTR_MAP_SEQ])
		printf(" seq:%llu",
		       rta_getattr(__u64, tb[MPTCP_SUBFLOW_ATTR_MAP_SEQ]));
	if (tb[MPTCP_SUBFLOW_ATTR_MAP_SFSEQ])
		printf(" sfseq:%u",
		       rta_getattr(__u32, tb[MPTCP_SUBFLOW_ATTR_MAP_SFSEQ]));
	if (tb[MPTCP_SUBFLOW_ATTR_SSN_OFFSET])
		printf(" ssnoff:%u",
		       rta_getattr(__u32, tb[MPTCP_SUBFLOW_ATTR_SSN_OFFSET]));
	if (tb[MPTCP_SUBFLOW_ATTR_MAP_DATALEN])
		printf(" maplen:%u",
		       rta_getattr(__u32, tb[MPTCP_SUBFLOW_ATTR_MAP_DATALEN]));
	printf("\n");
}

static void parse_nlmsg(struct nlmsghdr *nlh, __u32 proto)
{
	struct inet_diag_msg *r = NLMSG_DATA(nlh);
	struct rtattr *tb[INET_DIAG_MAX + 1];

	parse_rtattr_flags(tb, INET_DIAG_MAX, (struct rtattr *)(r + 1),
			   nlh->nlmsg_len - NLMSG_LENGTH(sizeof(*r)),
			   NLA_F_NESTED);

	if (proto == IPPROTO_MPTCP && tb[INET_DIAG_INFO]) {
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
	if (proto == IPPROTO_TCP && tb[INET_DIAG_ULP_INFO]) {
		struct rtattr *ulpinfo[INET_ULP_INFO_MAX + 1] = { 0 };

		parse_rtattr_nested(ulpinfo, INET_ULP_INFO_MAX,
				    tb[INET_DIAG_ULP_INFO]);

		if (ulpinfo[INET_ULP_INFO_MPTCP]) {
			struct rtattr *sfinfo[MPTCP_SUBFLOW_ATTR_MAX + 1] = { 0 };

			parse_rtattr_nested(sfinfo, MPTCP_SUBFLOW_ATTR_MAX,
					    ulpinfo[INET_ULP_INFO_MPTCP]);
			print_subflow_info(sfinfo);
		} else {
			printf("It's a normal TCP!\n");
		}
	}
}

static void recv_nlmsg(int fd, __u32 proto)
{
	char rcv_buff[8192];
	struct nlmsghdr *nlh = (struct nlmsghdr *)rcv_buff;
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
		parse_nlmsg(nlh, proto);
		nlh = NLMSG_NEXT(nlh, len);
	}
}

static void get_mptcpinfo(__u32 token)
{
	struct inet_diag_req_v2 r = {
		.sdiag_family           = AF_INET,
		/* Real proto is set via INET_DIAG_REQ_PROTOCOL */
		.sdiag_protocol         = IPPROTO_TCP,
		.idiag_ext              = 1 << (INET_DIAG_INFO - 1),
		.id.idiag_cookie[0]     = token,
	};
	__u32 proto = IPPROTO_MPTCP;
	int fd;

	fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_SOCK_DIAG);
	if (fd < 0)
		die_perror("Netlink socket");

	send_query(fd, &r, proto);
	recv_nlmsg(fd, proto);

	close(fd);
}

static void get_subflow_info(char *subflow_addrs)
{
	struct inet_diag_req_v2 r = {
		.sdiag_family           = AF_INET,
		.sdiag_protocol         = IPPROTO_TCP,
		.idiag_ext              = 1 << (INET_DIAG_INFO - 1),
		.id.idiag_cookie[0]     = INET_DIAG_NOCOOKIE,
		.id.idiag_cookie[1]     = INET_DIAG_NOCOOKIE,
	};
	char saddr[64], daddr[64];
	int sport, dport;
	int ret;
	int fd;

	ret = sscanf(subflow_addrs, "%[^:]:%d %[^:]:%d", saddr, &sport, daddr, &dport);
	if (ret != 4)
		die_perror("IP PORT Pairs has style problems!");

	printf("%s:%d -> %s:%d\n", saddr, sport, daddr, dport);

	fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_SOCK_DIAG);
	if (fd < 0)
		die_perror("Netlink socket");

	r.id.idiag_sport = htons(sport);
	r.id.idiag_dport = htons(dport);

	inet_pton(AF_INET, saddr, &r.id.idiag_src);
	inet_pton(AF_INET, daddr, &r.id.idiag_dst);
	send_query(fd, &r, IPPROTO_TCP);
	recv_nlmsg(fd, IPPROTO_TCP);
}

static void parse_opts(int argc, char **argv, struct params *p)
{
	int c;

	if (argc < 2)
		die_usage(1);

	while ((c = getopt(argc, argv, "ht:s:")) != -1) {
		switch (c) {
		case 'h':
			die_usage(0);
			break;
		case 't':
			sscanf(optarg, "%x", &p->target_token);
			break;
		case 's':
			strncpy(p->subflow_addrs, optarg,
				sizeof(p->subflow_addrs) - 1);
			break;
		default:
			die_usage(1);
			break;
		}
	}
}

int main(int argc, char *argv[])
{
	struct params p = { 0 };

	parse_opts(argc, argv, &p);

	if (p.target_token)
		get_mptcpinfo(p.target_token);

	if (p.subflow_addrs[0] != '\0')
		get_subflow_info(p.subflow_addrs);

	return 0;
}

