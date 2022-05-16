// SPDX-License-Identifier: GPL-2.0
/*
 * Test the SO_TXTIME API
 *
 * Takes two streams of { payload, delivery time }[], one input and one output.
 * Sends the input stream and verifies arrival matches the output stream.
 * The two streams can differ due to out-of-order delivery and drops.
 */

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <error.h>
#include <errno.h>
#include <inttypes.h>
#include <linux/net_tstamp.h>
#include <linux/errqueue.h>
#include <linux/if_ether.h>
#include <linux/ipv6.h>
#include <linux/udp.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static int	cfg_clockid	= CLOCK_TAI;
static bool	cfg_do_ipv4;
static bool	cfg_do_ipv6;
static uint16_t	cfg_port	= 8000;
static int	cfg_variance_us	= 4000;

static uint64_t glob_tstart;

/* encode one timed transmission (of a 1B payload) */
struct timed_send {
	char	data;
	int64_t	delay_us;
};

#define MAX_NUM_PKT	8
static struct timed_send cfg_in[MAX_NUM_PKT];
static struct timed_send cfg_out[MAX_NUM_PKT];
static int cfg_num_pkt;

static int cfg_errq_level;
static int cfg_errq_type;

static uint64_t gettime_ns(void)
{
	struct timespec ts;

	if (clock_gettime(cfg_clockid, &ts))
		error(1, errno, "gettime");

	return ts.tv_sec * (1000ULL * 1000 * 1000) + ts.tv_nsec;
}

static void do_send_one(int fdt, struct timed_send *ts)
{
	char control[CMSG_SPACE(sizeof(uint64_t))];
	struct msghdr msg = {0};
	struct iovec iov = {0};
	struct cmsghdr *cm;
	uint64_t tdeliver;
	int ret;

	iov.iov_base = &ts->data;
	iov.iov_len = 1;

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	if (ts->delay_us >= 0) {
		memset(control, 0, sizeof(control));
		msg.msg_control = &control;
		msg.msg_controllen = sizeof(control);

		tdeliver = glob_tstart + ts->delay_us * 1000;

		cm = CMSG_FIRSTHDR(&msg);
		cm->cmsg_level = SOL_SOCKET;
		cm->cmsg_type = SCM_TXTIME;
		cm->cmsg_len = CMSG_LEN(sizeof(tdeliver));
		memcpy(CMSG_DATA(cm), &tdeliver, sizeof(tdeliver));
	}

	ret = sendmsg(fdt, &msg, 0);
	if (ret == -1)
		error(1, errno, "write");
	if (ret == 0)
		error(1, 0, "write: 0B");

}

static bool do_recv_one(int fdr, struct timed_send *ts)
{
	int64_t tstop, texpect;
	char rbuf[2];
	int ret;

	ret = recv(fdr, rbuf, sizeof(rbuf), 0);
	if (ret == -1 && errno == EAGAIN)
		return true;
	if (ret == -1)
		error(1, errno, "read");
	if (ret != 1)
		error(1, 0, "read: %dB", ret);

	tstop = (gettime_ns() - glob_tstart) / 1000;
	texpect = ts->delay_us >= 0 ? ts->delay_us : 0;

	fprintf(stderr, "payload:%c delay:%lld expected:%lld (us)\n",
			rbuf[0], (long long)tstop, (long long)texpect);

	if (rbuf[0] != ts->data)
		error(1, 0, "payload mismatch. expected %c", ts->data);

	if (llabs(tstop - texpect) > cfg_variance_us)
		error(1, 0, "exceeds variance (%d us)", cfg_variance_us);

	return false;
}

static void do_recv_verify_empty(int fdr)
{
	char rbuf[1];
	int ret;

	ret = recv(fdr, rbuf, sizeof(rbuf), 0);
	if (ret != -1 || errno != EAGAIN)
		error(1, 0, "recv: not empty as expected (%d, %d)", ret, errno);
}

static void do_recv_errqueue_timeout(int fdt)
{
	char control[CMSG_SPACE(sizeof(struct sock_extended_err)) +
		     CMSG_SPACE(sizeof(struct sockaddr_in6))] = {0};
	char data[sizeof(struct ethhdr) + sizeof(struct ipv6hdr) +
		  sizeof(struct udphdr) + 1];
	struct sock_extended_err *err;
	struct msghdr msg = {0};
	struct iovec iov = {0};
	struct cmsghdr *cm;
	int64_t tstamp = 0;
	int ret;

	iov.iov_base = data;
	iov.iov_len = sizeof(data);

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	msg.msg_control = control;
	msg.msg_controllen = sizeof(control);

	while (1) {
		const char *reason;

		ret = recvmsg(fdt, &msg, MSG_ERRQUEUE);
		if (ret == -1 && errno == EAGAIN)
			break;
		if (ret == -1)
			error(1, errno, "errqueue");
		if (msg.msg_flags != MSG_ERRQUEUE)
			error(1, 0, "errqueue: flags 0x%x\n", msg.msg_flags);

		cm = CMSG_FIRSTHDR(&msg);
		if (cm->cmsg_level != cfg_errq_level ||
		    cm->cmsg_type != cfg_errq_type)
			error(1, 0, "errqueue: type 0x%x.0x%x\n",
				    cm->cmsg_level, cm->cmsg_type);

		err = (struct sock_extended_err *)CMSG_DATA(cm);
		if (err->ee_origin != SO_EE_ORIGIN_TXTIME)
			error(1, 0, "errqueue: origin 0x%x\n", err->ee_origin);

		switch (err->ee_errno) {
		case ECANCELED:
			if (err->ee_code != SO_EE_CODE_TXTIME_MISSED)
				error(1, 0, "errqueue: unknown ECANCELED %u\n",
				      err->ee_code);
			reason = "missed txtime";
		break;
		case EINVAL:
			if (err->ee_code != SO_EE_CODE_TXTIME_INVALID_PARAM)
				error(1, 0, "errqueue: unknown EINVAL %u\n",
				      err->ee_code);
			reason = "invalid txtime";
		break;
		default:
			error(1, 0, "errqueue: errno %u code %u\n",
			      err->ee_errno, err->ee_code);
		};

		tstamp = ((int64_t) err->ee_data) << 32 | err->ee_info;
		tstamp -= (int64_t) glob_tstart;
		tstamp /= 1000 * 1000;
		fprintf(stderr, "send: pkt %c at %" PRId64 "ms dropped: %s\n",
			data[ret - 1], tstamp, reason);

		msg.msg_flags = 0;
		msg.msg_controllen = sizeof(control);
	}

	error(1, 0, "recv: timeout");
}

static void setsockopt_txtime(int fd)
{
	struct sock_txtime so_txtime_val = { .clockid = cfg_clockid };
	struct sock_txtime so_txtime_val_read = { 0 };
	socklen_t vallen = sizeof(so_txtime_val);

	so_txtime_val.flags = SOF_TXTIME_REPORT_ERRORS;

	if (setsockopt(fd, SOL_SOCKET, SO_TXTIME,
		       &so_txtime_val, sizeof(so_txtime_val)))
		error(1, errno, "setsockopt txtime");

	if (getsockopt(fd, SOL_SOCKET, SO_TXTIME,
		       &so_txtime_val_read, &vallen))
		error(1, errno, "getsockopt txtime");

	if (vallen != sizeof(so_txtime_val) ||
	    memcmp(&so_txtime_val, &so_txtime_val_read, vallen))
		error(1, 0, "getsockopt txtime: mismatch");
}

static int setup_tx(struct sockaddr *addr, socklen_t alen)
{
	int fd;

	fd = socket(addr->sa_family, SOCK_DGRAM, 0);
	if (fd == -1)
		error(1, errno, "socket t");

	if (connect(fd, addr, alen))
		error(1, errno, "connect");

	setsockopt_txtime(fd);

	return fd;
}

static int setup_rx(struct sockaddr *addr, socklen_t alen)
{
	struct timeval tv = { .tv_usec = 100 * 1000 };
	int fd;

	fd = socket(addr->sa_family, SOCK_DGRAM, 0);
	if (fd == -1)
		error(1, errno, "socket r");

	if (bind(fd, addr, alen))
		error(1, errno, "bind");

	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)))
		error(1, errno, "setsockopt rcv timeout");

	return fd;
}

static void do_test(struct sockaddr *addr, socklen_t alen)
{
	int fdt, fdr, i;

	fprintf(stderr, "\nSO_TXTIME ipv%c clock %s\n",
			addr->sa_family == PF_INET ? '4' : '6',
			cfg_clockid == CLOCK_TAI ? "tai" : "monotonic");

	fdt = setup_tx(addr, alen);
	fdr = setup_rx(addr, alen);

	glob_tstart = gettime_ns();

	for (i = 0; i < cfg_num_pkt; i++)
		do_send_one(fdt, &cfg_in[i]);
	for (i = 0; i < cfg_num_pkt; i++)
		if (do_recv_one(fdr, &cfg_out[i]))
			do_recv_errqueue_timeout(fdt);

	do_recv_verify_empty(fdr);

	if (close(fdr))
		error(1, errno, "close r");
	if (close(fdt))
		error(1, errno, "close t");
}

static int parse_io(const char *optarg, struct timed_send *array)
{
	char *arg, *tok;
	int aoff = 0;

	arg = strdup(optarg);
	if (!arg)
		error(1, errno, "strdup");

	while ((tok = strtok(arg, ","))) {
		arg = NULL;	/* only pass non-zero on first call */

		if (aoff / 2 == MAX_NUM_PKT)
			error(1, 0, "exceeds max pkt count (%d)", MAX_NUM_PKT);

		if (aoff & 1) {	/* parse delay */
			array->delay_us = strtol(tok, NULL, 0) * 1000;
			array++;
		} else {	/* parse character */
			array->data = tok[0];
		}

		aoff++;
	}

	free(arg);

	return aoff / 2;
}

static void parse_opts(int argc, char **argv)
{
	int c, ilen, olen;

	while ((c = getopt(argc, argv, "46c:")) != -1) {
		switch (c) {
		case '4':
			cfg_do_ipv4 = true;
			break;
		case '6':
			cfg_do_ipv6 = true;
			break;
		case 'c':
			if (!strcmp(optarg, "tai"))
				cfg_clockid = CLOCK_TAI;
			else if (!strcmp(optarg, "monotonic") ||
				 !strcmp(optarg, "mono"))
				cfg_clockid = CLOCK_MONOTONIC;
			else
				error(1, 0, "unknown clock id %s", optarg);
			break;
		default:
			error(1, 0, "parse error at %d", optind);
		}
	}

	if (argc - optind != 2)
		error(1, 0, "Usage: %s [-46] -c <clock> <in> <out>", argv[0]);

	ilen = parse_io(argv[optind], cfg_in);
	olen = parse_io(argv[optind + 1], cfg_out);
	if (ilen != olen)
		error(1, 0, "i/o streams len mismatch (%d, %d)\n", ilen, olen);
	cfg_num_pkt = ilen;
}

int main(int argc, char **argv)
{
	parse_opts(argc, argv);

	if (cfg_do_ipv6) {
		struct sockaddr_in6 addr6 = {0};

		addr6.sin6_family = AF_INET6;
		addr6.sin6_port = htons(cfg_port);
		addr6.sin6_addr = in6addr_loopback;

		cfg_errq_level = SOL_IPV6;
		cfg_errq_type = IPV6_RECVERR;

		do_test((void *)&addr6, sizeof(addr6));
	}

	if (cfg_do_ipv4) {
		struct sockaddr_in addr4 = {0};

		addr4.sin_family = AF_INET;
		addr4.sin_port = htons(cfg_port);
		addr4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

		cfg_errq_level = SOL_IP;
		cfg_errq_type = IP_RECVERR;

		do_test((void *)&addr4, sizeof(addr4));
	}

	return 0;
}
