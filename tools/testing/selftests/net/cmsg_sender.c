// SPDX-License-Identifier: GPL-2.0-or-later
#include <errno.h>
#include <error.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <linux/errqueue.h>
#include <linux/icmp.h>
#include <linux/icmpv6.h>
#include <linux/net_tstamp.h>
#include <linux/types.h>
#include <linux/udp.h>
#include <sys/socket.h>

enum {
	ERN_SUCCESS = 0,
	/* Well defined errors, callers may depend on these */
	ERN_SEND = 1,
	/* Informational, can reorder */
	ERN_HELP,
	ERN_SEND_SHORT,
	ERN_SOCK_CREATE,
	ERN_RESOLVE,
	ERN_CMSG_WR,
	ERN_SOCKOPT,
	ERN_GETTIME,
	ERN_RECVERR,
	ERN_CMSG_RD,
	ERN_CMSG_RCV,
};

struct option_cmsg_u32 {
	bool ena;
	unsigned int val;
};

struct options {
	bool silent_send;
	const char *host;
	const char *service;
	unsigned int size;
	struct {
		unsigned int mark;
		unsigned int dontfrag;
		unsigned int tclass;
	} sockopt;
	struct {
		unsigned int family;
		unsigned int type;
		unsigned int proto;
	} sock;
	struct option_cmsg_u32 mark;
	struct {
		bool ena;
		unsigned int delay;
	} txtime;
	struct {
		bool ena;
	} ts;
	struct {
		struct option_cmsg_u32 dontfrag;
		struct option_cmsg_u32 tclass;
	} v6;
} opt = {
	.size = 13,
	.sock = {
		.family	= AF_UNSPEC,
		.type	= SOCK_DGRAM,
		.proto	= IPPROTO_UDP,
	},
};

static struct timespec time_start_real;
static struct timespec time_start_mono;

static void __attribute__((noreturn)) cs_usage(const char *bin)
{
	printf("Usage: %s [opts] <dst host> <dst port / service>\n", bin);
	printf("Options:\n"
	       "\t\t-s      Silent send() failures\n"
	       "\t\t-S      send() size\n"
	       "\t\t-4/-6   Force IPv4 / IPv6 only\n"
	       "\t\t-p prot Socket protocol\n"
	       "\t\t        (u = UDP (default); i = ICMP; r = RAW)\n"
	       "\n"
	       "\t\t-m val  Set SO_MARK with given value\n"
	       "\t\t-M val  Set SO_MARK via setsockopt\n"
	       "\t\t-d val  Set SO_TXTIME with given delay (usec)\n"
	       "\t\t-t      Enable time stamp reporting\n"
	       "\t\t-f val  Set don't fragment via cmsg\n"
	       "\t\t-F val  Set don't fragment via setsockopt\n"
	       "\t\t-c val  Set TCLASS via cmsg\n"
	       "\t\t-C val  Set TCLASS via setsockopt\n"
	       "");
	exit(ERN_HELP);
}

static void cs_parse_args(int argc, char *argv[])
{
	char o;

	while ((o = getopt(argc, argv, "46sS:p:m:M:d:tf:F:c:C:")) != -1) {
		switch (o) {
		case 's':
			opt.silent_send = true;
			break;
		case 'S':
			opt.size = atoi(optarg);
			break;
		case '4':
			opt.sock.family = AF_INET;
			break;
		case '6':
			opt.sock.family = AF_INET6;
			break;
		case 'p':
			if (*optarg == 'u' || *optarg == 'U') {
				opt.sock.proto = IPPROTO_UDP;
			} else if (*optarg == 'i' || *optarg == 'I') {
				opt.sock.proto = IPPROTO_ICMP;
			} else if (*optarg == 'r') {
				opt.sock.type = SOCK_RAW;
			} else {
				printf("Error: unknown protocol: %s\n", optarg);
				cs_usage(argv[0]);
			}
			break;

		case 'm':
			opt.mark.ena = true;
			opt.mark.val = atoi(optarg);
			break;
		case 'M':
			opt.sockopt.mark = atoi(optarg);
			break;
		case 'd':
			opt.txtime.ena = true;
			opt.txtime.delay = atoi(optarg);
			break;
		case 't':
			opt.ts.ena = true;
			break;
		case 'f':
			opt.v6.dontfrag.ena = true;
			opt.v6.dontfrag.val = atoi(optarg);
			break;
		case 'F':
			opt.sockopt.dontfrag = atoi(optarg);
			break;
		case 'c':
			opt.v6.tclass.ena = true;
			opt.v6.tclass.val = atoi(optarg);
			break;
		case 'C':
			opt.sockopt.tclass = atoi(optarg);
			break;
		}
	}

	if (optind != argc - 2)
		cs_usage(argv[0]);

	opt.host = argv[optind];
	opt.service = argv[optind + 1];
}

static void memrnd(void *s, size_t n)
{
	int *dword = s;
	char *byte;

	for (; n >= 4; n -= 4)
		*dword++ = rand();
	byte = (void *)dword;
	while (n--)
		*byte++ = rand();
}

static void
ca_write_cmsg_u32(char *cbuf, size_t cbuf_sz, size_t *cmsg_len,
		  int level, int optname, struct option_cmsg_u32 *uopt)
{
	struct cmsghdr *cmsg;

	if (!uopt->ena)
		return;

	cmsg = (struct cmsghdr *)(cbuf + *cmsg_len);
	*cmsg_len += CMSG_SPACE(sizeof(__u32));
	if (cbuf_sz < *cmsg_len)
		error(ERN_CMSG_WR, EFAULT, "cmsg buffer too small");

	cmsg->cmsg_level = level;
	cmsg->cmsg_type = optname;
	cmsg->cmsg_len = CMSG_LEN(sizeof(__u32));
	*(__u32 *)CMSG_DATA(cmsg) = uopt->val;
}

static void
cs_write_cmsg(int fd, struct msghdr *msg, char *cbuf, size_t cbuf_sz)
{
	struct cmsghdr *cmsg;
	size_t cmsg_len;

	msg->msg_control = cbuf;
	cmsg_len = 0;

	ca_write_cmsg_u32(cbuf, cbuf_sz, &cmsg_len,
			  SOL_SOCKET, SO_MARK, &opt.mark);
	ca_write_cmsg_u32(cbuf, cbuf_sz, &cmsg_len,
			  SOL_IPV6, IPV6_DONTFRAG, &opt.v6.dontfrag);
	ca_write_cmsg_u32(cbuf, cbuf_sz, &cmsg_len,
			  SOL_IPV6, IPV6_TCLASS, &opt.v6.tclass);

	if (opt.txtime.ena) {
		struct sock_txtime so_txtime = {
			.clockid = CLOCK_MONOTONIC,
		};
		__u64 txtime;

		if (setsockopt(fd, SOL_SOCKET, SO_TXTIME,
			       &so_txtime, sizeof(so_txtime)))
			error(ERN_SOCKOPT, errno, "setsockopt TXTIME");

		txtime = time_start_mono.tv_sec * (1000ULL * 1000 * 1000) +
			 time_start_mono.tv_nsec +
			 opt.txtime.delay * 1000;

		cmsg = (struct cmsghdr *)(cbuf + cmsg_len);
		cmsg_len += CMSG_SPACE(sizeof(txtime));
		if (cbuf_sz < cmsg_len)
			error(ERN_CMSG_WR, EFAULT, "cmsg buffer too small");

		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SCM_TXTIME;
		cmsg->cmsg_len = CMSG_LEN(sizeof(txtime));
		memcpy(CMSG_DATA(cmsg), &txtime, sizeof(txtime));
	}
	if (opt.ts.ena) {
		__u32 val = SOF_TIMESTAMPING_SOFTWARE |
			    SOF_TIMESTAMPING_OPT_TSONLY;

		if (setsockopt(fd, SOL_SOCKET, SO_TIMESTAMPING,
			       &val, sizeof(val)))
			error(ERN_SOCKOPT, errno, "setsockopt TIMESTAMPING");

		cmsg = (struct cmsghdr *)(cbuf + cmsg_len);
		cmsg_len += CMSG_SPACE(sizeof(__u32));
		if (cbuf_sz < cmsg_len)
			error(ERN_CMSG_WR, EFAULT, "cmsg buffer too small");

		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SO_TIMESTAMPING;
		cmsg->cmsg_len = CMSG_LEN(sizeof(__u32));
		*(__u32 *)CMSG_DATA(cmsg) = SOF_TIMESTAMPING_TX_SCHED |
					    SOF_TIMESTAMPING_TX_SOFTWARE;
	}

	if (cmsg_len)
		msg->msg_controllen = cmsg_len;
	else
		msg->msg_control = NULL;
}

static const char *cs_ts_info2str(unsigned int info)
{
	static const char *names[] = {
		[SCM_TSTAMP_SND]	= "SND",
		[SCM_TSTAMP_SCHED]	= "SCHED",
		[SCM_TSTAMP_ACK]	= "ACK",
	};

	if (info < sizeof(names) / sizeof(names[0]))
		return names[info];
	return "unknown";
}

static void
cs_read_cmsg(int fd, struct msghdr *msg, char *cbuf, size_t cbuf_sz)
{
	struct sock_extended_err *see;
	struct scm_timestamping *ts;
	struct cmsghdr *cmsg;
	int i, err;

	if (!opt.ts.ena)
		return;
	msg->msg_control = cbuf;
	msg->msg_controllen = cbuf_sz;

	while (true) {
		ts = NULL;
		see = NULL;
		memset(cbuf, 0, cbuf_sz);

		err = recvmsg(fd, msg, MSG_ERRQUEUE);
		if (err < 0) {
			if (errno == EAGAIN)
				break;
			error(ERN_RECVERR, errno, "recvmsg ERRQ");
		}

		for (cmsg = CMSG_FIRSTHDR(msg); cmsg != NULL;
		     cmsg = CMSG_NXTHDR(msg, cmsg)) {
			if (cmsg->cmsg_level == SOL_SOCKET &&
			    cmsg->cmsg_type == SO_TIMESTAMPING_OLD) {
				if (cmsg->cmsg_len < sizeof(*ts))
					error(ERN_CMSG_RD, EINVAL, "TS cmsg");

				ts = (void *)CMSG_DATA(cmsg);
			}
			if ((cmsg->cmsg_level == SOL_IP &&
			     cmsg->cmsg_type == IP_RECVERR) ||
			    (cmsg->cmsg_level == SOL_IPV6 &&
			     cmsg->cmsg_type == IPV6_RECVERR)) {
				if (cmsg->cmsg_len < sizeof(*see))
					error(ERN_CMSG_RD, EINVAL, "sock_err cmsg");

				see = (void *)CMSG_DATA(cmsg);
			}
		}

		if (!ts)
			error(ERN_CMSG_RCV, ENOENT, "TS cmsg not found");
		if (!see)
			error(ERN_CMSG_RCV, ENOENT, "sock_err cmsg not found");

		for (i = 0; i < 3; i++) {
			unsigned long long rel_time;

			if (!ts->ts[i].tv_sec && !ts->ts[i].tv_nsec)
				continue;

			rel_time = (ts->ts[i].tv_sec - time_start_real.tv_sec) *
				(1000ULL * 1000) +
				(ts->ts[i].tv_nsec - time_start_real.tv_nsec) /
				1000;
			printf(" %5s ts%d %lluus\n",
			       cs_ts_info2str(see->ee_info),
			       i, rel_time);
		}
	}
}

static void ca_set_sockopts(int fd)
{
	if (opt.sockopt.mark &&
	    setsockopt(fd, SOL_SOCKET, SO_MARK,
		       &opt.sockopt.mark, sizeof(opt.sockopt.mark)))
		error(ERN_SOCKOPT, errno, "setsockopt SO_MARK");
	if (opt.sockopt.dontfrag &&
	    setsockopt(fd, SOL_IPV6, IPV6_DONTFRAG,
		       &opt.sockopt.dontfrag, sizeof(opt.sockopt.dontfrag)))
		error(ERN_SOCKOPT, errno, "setsockopt IPV6_DONTFRAG");
	if (opt.sockopt.tclass &&
	    setsockopt(fd, SOL_IPV6, IPV6_TCLASS,
		       &opt.sockopt.tclass, sizeof(opt.sockopt.tclass)))
		error(ERN_SOCKOPT, errno, "setsockopt IPV6_TCLASS");
}

int main(int argc, char *argv[])
{
	struct addrinfo hints, *ai;
	struct iovec iov[1];
	struct msghdr msg;
	char cbuf[1024];
	char *buf;
	int err;
	int fd;

	cs_parse_args(argc, argv);

	buf = malloc(opt.size);
	memrnd(buf, opt.size);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = opt.sock.family;

	ai = NULL;
	err = getaddrinfo(opt.host, opt.service, &hints, &ai);
	if (err) {
		fprintf(stderr, "Can't resolve address [%s]:%s\n",
			opt.host, opt.service);
		return ERN_SOCK_CREATE;
	}

	if (ai->ai_family == AF_INET6 && opt.sock.proto == IPPROTO_ICMP)
		opt.sock.proto = IPPROTO_ICMPV6;

	fd = socket(ai->ai_family, opt.sock.type, opt.sock.proto);
	if (fd < 0) {
		fprintf(stderr, "Can't open socket: %s\n", strerror(errno));
		freeaddrinfo(ai);
		return ERN_RESOLVE;
	}

	if (opt.sock.proto == IPPROTO_ICMP) {
		buf[0] = ICMP_ECHO;
		buf[1] = 0;
	} else if (opt.sock.proto == IPPROTO_ICMPV6) {
		buf[0] = ICMPV6_ECHO_REQUEST;
		buf[1] = 0;
	} else if (opt.sock.type == SOCK_RAW) {
		struct udphdr hdr = { 1, 2, htons(opt.size), 0 };
		struct sockaddr_in6 *sin6 = (void *)ai->ai_addr;;

		memcpy(buf, &hdr, sizeof(hdr));
		sin6->sin6_port = htons(opt.sock.proto);
	}

	ca_set_sockopts(fd);

	if (clock_gettime(CLOCK_REALTIME, &time_start_real))
		error(ERN_GETTIME, errno, "gettime REALTIME");
	if (clock_gettime(CLOCK_MONOTONIC, &time_start_mono))
		error(ERN_GETTIME, errno, "gettime MONOTONIC");

	iov[0].iov_base = buf;
	iov[0].iov_len = opt.size;

	memset(&msg, 0, sizeof(msg));
	msg.msg_name = ai->ai_addr;
	msg.msg_namelen = ai->ai_addrlen;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;

	cs_write_cmsg(fd, &msg, cbuf, sizeof(cbuf));

	err = sendmsg(fd, &msg, 0);
	if (err < 0) {
		if (!opt.silent_send)
			fprintf(stderr, "send failed: %s\n", strerror(errno));
		err = ERN_SEND;
		goto err_out;
	} else if (err != (int)opt.size) {
		fprintf(stderr, "short send\n");
		err = ERN_SEND_SHORT;
		goto err_out;
	} else {
		err = ERN_SUCCESS;
	}

	/* Make sure all timestamps have time to loop back */
	usleep(opt.txtime.delay);

	cs_read_cmsg(fd, &msg, cbuf, sizeof(cbuf));

err_out:
	close(fd);
	freeaddrinfo(ai);
	return err;
}
