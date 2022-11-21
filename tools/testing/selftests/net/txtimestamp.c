// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2014 Google Inc.
 * Author: willemb@google.com (Willem de Bruijn)
 *
 * Test software tx timestamping, including
 *
 * - SCHED, SND and ACK timestamps
 * - RAW, UDP and TCP
 * - IPv4 and IPv6
 * - various packet sizes (to test GSO and TSO)
 *
 * Consult the command line arguments for help on running
 * the various testcases.
 *
 * This test requires a dummy TCP server.
 * A simple `nc6 [-u] -l -p $DESTPORT` will do
 */

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <asm/types.h>
#include <error.h>
#include <errno.h>
#include <inttypes.h>
#include <linux/errqueue.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/ipv6.h>
#include <linux/net_tstamp.h>
#include <netdb.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define NSEC_PER_USEC	1000L
#define USEC_PER_SEC	1000000L
#define NSEC_PER_SEC	1000000000LL

/* command line parameters */
static int cfg_proto = SOCK_STREAM;
static int cfg_ipproto = IPPROTO_TCP;
static int cfg_num_pkts = 4;
static int do_ipv4 = 1;
static int do_ipv6 = 1;
static int cfg_payload_len = 10;
static int cfg_poll_timeout = 100;
static int cfg_delay_snd;
static int cfg_delay_ack;
static int cfg_delay_tolerance_usec = 500;
static bool cfg_show_payload;
static bool cfg_do_pktinfo;
static bool cfg_busy_poll;
static int cfg_sleep_usec = 50 * 1000;
static bool cfg_loop_nodata;
static bool cfg_use_cmsg;
static bool cfg_use_pf_packet;
static bool cfg_use_epoll;
static bool cfg_epollet;
static bool cfg_do_listen;
static uint16_t dest_port = 9000;
static bool cfg_print_nsec;

static struct sockaddr_in daddr;
static struct sockaddr_in6 daddr6;
static struct timespec ts_usr;

static int saved_tskey = -1;
static int saved_tskey_type = -1;

struct timing_event {
	int64_t min;
	int64_t max;
	int64_t total;
	int count;
};

static struct timing_event usr_enq;
static struct timing_event usr_snd;
static struct timing_event usr_ack;

static bool test_failed;

static int64_t timespec_to_ns64(struct timespec *ts)
{
	return ts->tv_sec * NSEC_PER_SEC + ts->tv_nsec;
}

static int64_t timespec_to_us64(struct timespec *ts)
{
	return ts->tv_sec * USEC_PER_SEC + ts->tv_nsec / NSEC_PER_USEC;
}

static void init_timing_event(struct timing_event *te)
{
	te->min = INT64_MAX;
	te->max = 0;
	te->total = 0;
	te->count = 0;
}

static void add_timing_event(struct timing_event *te,
		struct timespec *t_start, struct timespec *t_end)
{
	int64_t ts_delta = timespec_to_ns64(t_end) - timespec_to_ns64(t_start);

	te->count++;
	if (ts_delta < te->min)
		te->min = ts_delta;
	if (ts_delta > te->max)
		te->max = ts_delta;
	te->total += ts_delta;
}

static void validate_key(int tskey, int tstype)
{
	int stepsize;

	/* compare key for each subsequent request
	 * must only test for one type, the first one requested
	 */
	if (saved_tskey == -1)
		saved_tskey_type = tstype;
	else if (saved_tskey_type != tstype)
		return;

	stepsize = cfg_proto == SOCK_STREAM ? cfg_payload_len : 1;
	if (tskey != saved_tskey + stepsize) {
		fprintf(stderr, "ERROR: key %d, expected %d\n",
				tskey, saved_tskey + stepsize);
		test_failed = true;
	}

	saved_tskey = tskey;
}

static void validate_timestamp(struct timespec *cur, int min_delay)
{
	int64_t cur64, start64;
	int max_delay;

	cur64 = timespec_to_us64(cur);
	start64 = timespec_to_us64(&ts_usr);
	max_delay = min_delay + cfg_delay_tolerance_usec;

	if (cur64 < start64 + min_delay || cur64 > start64 + max_delay) {
		fprintf(stderr, "ERROR: %" PRId64 " us expected between %d and %d\n",
				cur64 - start64, min_delay, max_delay);
		test_failed = true;
	}
}

static void __print_ts_delta_formatted(int64_t ts_delta)
{
	if (cfg_print_nsec)
		fprintf(stderr, "%" PRId64 " ns", ts_delta);
	else
		fprintf(stderr, "%" PRId64 " us", ts_delta / NSEC_PER_USEC);
}

static void __print_timestamp(const char *name, struct timespec *cur,
			      uint32_t key, int payload_len)
{
	int64_t ts_delta;

	if (!(cur->tv_sec | cur->tv_nsec))
		return;

	if (cfg_print_nsec)
		fprintf(stderr, "  %s: %lu s %lu ns (seq=%u, len=%u)",
				name, cur->tv_sec, cur->tv_nsec,
				key, payload_len);
	else
		fprintf(stderr, "  %s: %lu s %lu us (seq=%u, len=%u)",
				name, cur->tv_sec, cur->tv_nsec / NSEC_PER_USEC,
				key, payload_len);

	if (cur != &ts_usr) {
		ts_delta = timespec_to_ns64(cur) - timespec_to_ns64(&ts_usr);
		fprintf(stderr, "  (USR +");
		__print_ts_delta_formatted(ts_delta);
		fprintf(stderr, ")");
	}

	fprintf(stderr, "\n");
}

static void print_timestamp_usr(void)
{
	if (clock_gettime(CLOCK_REALTIME, &ts_usr))
		error(1, errno, "clock_gettime");

	__print_timestamp("  USR", &ts_usr, 0, 0);
}

static void print_timestamp(struct scm_timestamping *tss, int tstype,
			    int tskey, int payload_len)
{
	const char *tsname;

	validate_key(tskey, tstype);

	switch (tstype) {
	case SCM_TSTAMP_SCHED:
		tsname = "  ENQ";
		validate_timestamp(&tss->ts[0], 0);
		add_timing_event(&usr_enq, &ts_usr, &tss->ts[0]);
		break;
	case SCM_TSTAMP_SND:
		tsname = "  SND";
		validate_timestamp(&tss->ts[0], cfg_delay_snd);
		add_timing_event(&usr_snd, &ts_usr, &tss->ts[0]);
		break;
	case SCM_TSTAMP_ACK:
		tsname = "  ACK";
		validate_timestamp(&tss->ts[0], cfg_delay_ack);
		add_timing_event(&usr_ack, &ts_usr, &tss->ts[0]);
		break;
	default:
		error(1, 0, "unknown timestamp type: %u",
		tstype);
	}
	__print_timestamp(tsname, &tss->ts[0], tskey, payload_len);
}

static void print_timing_event(char *name, struct timing_event *te)
{
	if (!te->count)
		return;

	fprintf(stderr, "    %s: count=%d", name, te->count);
	fprintf(stderr, ", avg=");
	__print_ts_delta_formatted((int64_t)(te->total / te->count));
	fprintf(stderr, ", min=");
	__print_ts_delta_formatted(te->min);
	fprintf(stderr, ", max=");
	__print_ts_delta_formatted(te->max);
	fprintf(stderr, "\n");
}

/* TODO: convert to check_and_print payload once API is stable */
static void print_payload(char *data, int len)
{
	int i;

	if (!len)
		return;

	if (len > 70)
		len = 70;

	fprintf(stderr, "payload: ");
	for (i = 0; i < len; i++)
		fprintf(stderr, "%02hhx ", data[i]);
	fprintf(stderr, "\n");
}

static void print_pktinfo(int family, int ifindex, void *saddr, void *daddr)
{
	char sa[INET6_ADDRSTRLEN], da[INET6_ADDRSTRLEN];

	fprintf(stderr, "         pktinfo: ifindex=%u src=%s dst=%s\n",
		ifindex,
		saddr ? inet_ntop(family, saddr, sa, sizeof(sa)) : "unknown",
		daddr ? inet_ntop(family, daddr, da, sizeof(da)) : "unknown");
}

static void __epoll(int epfd)
{
	struct epoll_event events;
	int ret;

	memset(&events, 0, sizeof(events));
	ret = epoll_wait(epfd, &events, 1, cfg_poll_timeout);
	if (ret != 1)
		error(1, errno, "epoll_wait");
}

static void __poll(int fd)
{
	struct pollfd pollfd;
	int ret;

	memset(&pollfd, 0, sizeof(pollfd));
	pollfd.fd = fd;
	ret = poll(&pollfd, 1, cfg_poll_timeout);
	if (ret != 1)
		error(1, errno, "poll");
}

static void __recv_errmsg_cmsg(struct msghdr *msg, int payload_len)
{
	struct sock_extended_err *serr = NULL;
	struct scm_timestamping *tss = NULL;
	struct cmsghdr *cm;
	int batch = 0;

	for (cm = CMSG_FIRSTHDR(msg);
	     cm && cm->cmsg_len;
	     cm = CMSG_NXTHDR(msg, cm)) {
		if (cm->cmsg_level == SOL_SOCKET &&
		    cm->cmsg_type == SCM_TIMESTAMPING) {
			tss = (void *) CMSG_DATA(cm);
		} else if ((cm->cmsg_level == SOL_IP &&
			    cm->cmsg_type == IP_RECVERR) ||
			   (cm->cmsg_level == SOL_IPV6 &&
			    cm->cmsg_type == IPV6_RECVERR) ||
			   (cm->cmsg_level == SOL_PACKET &&
			    cm->cmsg_type == PACKET_TX_TIMESTAMP)) {
			serr = (void *) CMSG_DATA(cm);
			if (serr->ee_errno != ENOMSG ||
			    serr->ee_origin != SO_EE_ORIGIN_TIMESTAMPING) {
				fprintf(stderr, "unknown ip error %d %d\n",
						serr->ee_errno,
						serr->ee_origin);
				serr = NULL;
			}
		} else if (cm->cmsg_level == SOL_IP &&
			   cm->cmsg_type == IP_PKTINFO) {
			struct in_pktinfo *info = (void *) CMSG_DATA(cm);
			print_pktinfo(AF_INET, info->ipi_ifindex,
				      &info->ipi_spec_dst, &info->ipi_addr);
		} else if (cm->cmsg_level == SOL_IPV6 &&
			   cm->cmsg_type == IPV6_PKTINFO) {
			struct in6_pktinfo *info6 = (void *) CMSG_DATA(cm);
			print_pktinfo(AF_INET6, info6->ipi6_ifindex,
				      NULL, &info6->ipi6_addr);
		} else
			fprintf(stderr, "unknown cmsg %d,%d\n",
					cm->cmsg_level, cm->cmsg_type);

		if (serr && tss) {
			print_timestamp(tss, serr->ee_info, serr->ee_data,
					payload_len);
			serr = NULL;
			tss = NULL;
			batch++;
		}
	}

	if (batch > 1)
		fprintf(stderr, "batched %d timestamps\n", batch);
}

static int recv_errmsg(int fd)
{
	static char ctrl[1024 /* overprovision*/];
	static struct msghdr msg;
	struct iovec entry;
	static char *data;
	int ret = 0;

	data = malloc(cfg_payload_len);
	if (!data)
		error(1, 0, "malloc");

	memset(&msg, 0, sizeof(msg));
	memset(&entry, 0, sizeof(entry));
	memset(ctrl, 0, sizeof(ctrl));

	entry.iov_base = data;
	entry.iov_len = cfg_payload_len;
	msg.msg_iov = &entry;
	msg.msg_iovlen = 1;
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_control = ctrl;
	msg.msg_controllen = sizeof(ctrl);

	ret = recvmsg(fd, &msg, MSG_ERRQUEUE);
	if (ret == -1 && errno != EAGAIN)
		error(1, errno, "recvmsg");

	if (ret >= 0) {
		__recv_errmsg_cmsg(&msg, ret);
		if (cfg_show_payload)
			print_payload(data, cfg_payload_len);
	}

	free(data);
	return ret == -1;
}

static uint16_t get_ip_csum(const uint16_t *start, int num_words,
			    unsigned long sum)
{
	int i;

	for (i = 0; i < num_words; i++)
		sum += start[i];

	while (sum >> 16)
		sum = (sum & 0xFFFF) + (sum >> 16);

	return ~sum;
}

static uint16_t get_udp_csum(const struct udphdr *udph, int alen)
{
	unsigned long pseudo_sum, csum_len;
	const void *csum_start = udph;

	pseudo_sum = htons(IPPROTO_UDP);
	pseudo_sum += udph->len;

	/* checksum ip(v6) addresses + udp header + payload */
	csum_start -= alen * 2;
	csum_len = ntohs(udph->len) + alen * 2;

	return get_ip_csum(csum_start, csum_len >> 1, pseudo_sum);
}

static int fill_header_ipv4(void *p)
{
	struct iphdr *iph = p;

	memset(iph, 0, sizeof(*iph));

	iph->ihl	= 5;
	iph->version	= 4;
	iph->ttl	= 2;
	iph->saddr	= daddr.sin_addr.s_addr;	/* set for udp csum calc */
	iph->daddr	= daddr.sin_addr.s_addr;
	iph->protocol	= IPPROTO_UDP;

	/* kernel writes saddr, csum, len */

	return sizeof(*iph);
}

static int fill_header_ipv6(void *p)
{
	struct ipv6hdr *ip6h = p;

	memset(ip6h, 0, sizeof(*ip6h));

	ip6h->version		= 6;
	ip6h->payload_len	= htons(sizeof(struct udphdr) + cfg_payload_len);
	ip6h->nexthdr		= IPPROTO_UDP;
	ip6h->hop_limit		= 64;

	ip6h->saddr             = daddr6.sin6_addr;
	ip6h->daddr		= daddr6.sin6_addr;

	/* kernel does not write saddr in case of ipv6 */

	return sizeof(*ip6h);
}

static void fill_header_udp(void *p, bool is_ipv4)
{
	struct udphdr *udph = p;

	udph->source = ntohs(dest_port + 1);	/* spoof */
	udph->dest   = ntohs(dest_port);
	udph->len    = ntohs(sizeof(*udph) + cfg_payload_len);
	udph->check  = 0;

	udph->check  = get_udp_csum(udph, is_ipv4 ? sizeof(struct in_addr) :
						    sizeof(struct in6_addr));
}

static void do_test(int family, unsigned int report_opt)
{
	char control[CMSG_SPACE(sizeof(uint32_t))];
	struct sockaddr_ll laddr;
	unsigned int sock_opt;
	struct cmsghdr *cmsg;
	struct msghdr msg;
	struct iovec iov;
	char *buf;
	int fd, i, val = 1, total_len, epfd = 0;

	init_timing_event(&usr_enq);
	init_timing_event(&usr_snd);
	init_timing_event(&usr_ack);

	total_len = cfg_payload_len;
	if (cfg_use_pf_packet || cfg_proto == SOCK_RAW) {
		total_len += sizeof(struct udphdr);
		if (cfg_use_pf_packet || cfg_ipproto == IPPROTO_RAW) {
			if (family == PF_INET)
				total_len += sizeof(struct iphdr);
			else
				total_len += sizeof(struct ipv6hdr);
		}
		/* special case, only rawv6_sendmsg:
		 * pass proto in sin6_port if not connected
		 * also see ANK comment in net/ipv4/raw.c
		 */
		daddr6.sin6_port = htons(cfg_ipproto);
	}

	buf = malloc(total_len);
	if (!buf)
		error(1, 0, "malloc");

	fd = socket(cfg_use_pf_packet ? PF_PACKET : family,
		    cfg_proto, cfg_ipproto);
	if (fd < 0)
		error(1, errno, "socket");

	if (cfg_use_epoll) {
		struct epoll_event ev;

		memset(&ev, 0, sizeof(ev));
		ev.data.fd = fd;
		if (cfg_epollet)
			ev.events |= EPOLLET;
		epfd = epoll_create(1);
		if (epfd <= 0)
			error(1, errno, "epoll_create");
		if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev))
			error(1, errno, "epoll_ctl");
	}

	/* reset expected key on each new socket */
	saved_tskey = -1;

	if (cfg_proto == SOCK_STREAM) {
		if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
			       (char*) &val, sizeof(val)))
			error(1, 0, "setsockopt no nagle");

		if (family == PF_INET) {
			if (connect(fd, (void *) &daddr, sizeof(daddr)))
				error(1, errno, "connect ipv4");
		} else {
			if (connect(fd, (void *) &daddr6, sizeof(daddr6)))
				error(1, errno, "connect ipv6");
		}
	}

	if (cfg_do_pktinfo) {
		if (family == AF_INET6) {
			if (setsockopt(fd, SOL_IPV6, IPV6_RECVPKTINFO,
				       &val, sizeof(val)))
				error(1, errno, "setsockopt pktinfo ipv6");
		} else {
			if (setsockopt(fd, SOL_IP, IP_PKTINFO,
				       &val, sizeof(val)))
				error(1, errno, "setsockopt pktinfo ipv4");
		}
	}

	sock_opt = SOF_TIMESTAMPING_SOFTWARE |
		   SOF_TIMESTAMPING_OPT_CMSG |
		   SOF_TIMESTAMPING_OPT_ID;

	if (!cfg_use_cmsg)
		sock_opt |= report_opt;

	if (cfg_loop_nodata)
		sock_opt |= SOF_TIMESTAMPING_OPT_TSONLY;

	if (setsockopt(fd, SOL_SOCKET, SO_TIMESTAMPING,
		       (char *) &sock_opt, sizeof(sock_opt)))
		error(1, 0, "setsockopt timestamping");

	for (i = 0; i < cfg_num_pkts; i++) {
		memset(&msg, 0, sizeof(msg));
		memset(buf, 'a' + i, total_len);

		if (cfg_use_pf_packet || cfg_proto == SOCK_RAW) {
			int off = 0;

			if (cfg_use_pf_packet || cfg_ipproto == IPPROTO_RAW) {
				if (family == PF_INET)
					off = fill_header_ipv4(buf);
				else
					off = fill_header_ipv6(buf);
			}

			fill_header_udp(buf + off, family == PF_INET);
		}

		print_timestamp_usr();

		iov.iov_base = buf;
		iov.iov_len = total_len;

		if (cfg_proto != SOCK_STREAM) {
			if (cfg_use_pf_packet) {
				memset(&laddr, 0, sizeof(laddr));

				laddr.sll_family	= AF_PACKET;
				laddr.sll_ifindex	= 1;
				laddr.sll_protocol	= htons(family == AF_INET ? ETH_P_IP : ETH_P_IPV6);
				laddr.sll_halen		= ETH_ALEN;

				msg.msg_name = (void *)&laddr;
				msg.msg_namelen = sizeof(laddr);
			} else if (family == PF_INET) {
				msg.msg_name = (void *)&daddr;
				msg.msg_namelen = sizeof(daddr);
			} else {
				msg.msg_name = (void *)&daddr6;
				msg.msg_namelen = sizeof(daddr6);
			}
		}

		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;

		if (cfg_use_cmsg) {
			memset(control, 0, sizeof(control));

			msg.msg_control = control;
			msg.msg_controllen = sizeof(control);

			cmsg = CMSG_FIRSTHDR(&msg);
			cmsg->cmsg_level = SOL_SOCKET;
			cmsg->cmsg_type = SO_TIMESTAMPING;
			cmsg->cmsg_len = CMSG_LEN(sizeof(uint32_t));

			*((uint32_t *) CMSG_DATA(cmsg)) = report_opt;
		}

		val = sendmsg(fd, &msg, 0);
		if (val != total_len)
			error(1, errno, "send");

		/* wait for all errors to be queued, else ACKs arrive OOO */
		if (cfg_sleep_usec)
			usleep(cfg_sleep_usec);

		if (!cfg_busy_poll) {
			if (cfg_use_epoll)
				__epoll(epfd);
			else
				__poll(fd);
		}

		while (!recv_errmsg(fd)) {}
	}

	print_timing_event("USR-ENQ", &usr_enq);
	print_timing_event("USR-SND", &usr_snd);
	print_timing_event("USR-ACK", &usr_ack);

	if (close(fd))
		error(1, errno, "close");

	free(buf);
	usleep(100 * NSEC_PER_USEC);
}

static void __attribute__((noreturn)) usage(const char *filepath)
{
	fprintf(stderr, "\nUsage: %s [options] hostname\n"
			"\nwhere options are:\n"
			"  -4:   only IPv4\n"
			"  -6:   only IPv6\n"
			"  -h:   show this message\n"
			"  -b:   busy poll to read from error queue\n"
			"  -c N: number of packets for each test\n"
			"  -C:   use cmsg to set tstamp recording options\n"
			"  -e:   use level-triggered epoll() instead of poll()\n"
			"  -E:   use event-triggered epoll() instead of poll()\n"
			"  -F:   poll()/epoll() waits forever for an event\n"
			"  -I:   request PKTINFO\n"
			"  -l N: send N bytes at a time\n"
			"  -L    listen on hostname and port\n"
			"  -n:   set no-payload option\n"
			"  -N:   print timestamps and durations in nsec (instead of usec)\n"
			"  -p N: connect to port N\n"
			"  -P:   use PF_PACKET\n"
			"  -r:   use raw\n"
			"  -R:   use raw (IP_HDRINCL)\n"
			"  -S N: usec to sleep before reading error queue\n"
			"  -t N: tolerance (usec) for timestamp validation\n"
			"  -u:   use udp\n"
			"  -v:   validate SND delay (usec)\n"
			"  -V:   validate ACK delay (usec)\n"
			"  -x:   show payload (up to 70 bytes)\n",
			filepath);
	exit(1);
}

static void parse_opt(int argc, char **argv)
{
	int proto_count = 0;
	int c;

	while ((c = getopt(argc, argv,
				"46bc:CeEFhIl:LnNp:PrRS:t:uv:V:x")) != -1) {
		switch (c) {
		case '4':
			do_ipv6 = 0;
			break;
		case '6':
			do_ipv4 = 0;
			break;
		case 'b':
			cfg_busy_poll = true;
			break;
		case 'c':
			cfg_num_pkts = strtoul(optarg, NULL, 10);
			break;
		case 'C':
			cfg_use_cmsg = true;
			break;
		case 'e':
			cfg_use_epoll = true;
			break;
		case 'E':
			cfg_use_epoll = true;
			cfg_epollet = true;
		case 'F':
			cfg_poll_timeout = -1;
			break;
		case 'I':
			cfg_do_pktinfo = true;
			break;
		case 'l':
			cfg_payload_len = strtoul(optarg, NULL, 10);
			break;
		case 'L':
			cfg_do_listen = true;
			break;
		case 'n':
			cfg_loop_nodata = true;
			break;
		case 'N':
			cfg_print_nsec = true;
			break;
		case 'p':
			dest_port = strtoul(optarg, NULL, 10);
			break;
		case 'P':
			proto_count++;
			cfg_use_pf_packet = true;
			cfg_proto = SOCK_DGRAM;
			cfg_ipproto = 0;
			break;
		case 'r':
			proto_count++;
			cfg_proto = SOCK_RAW;
			cfg_ipproto = IPPROTO_UDP;
			break;
		case 'R':
			proto_count++;
			cfg_proto = SOCK_RAW;
			cfg_ipproto = IPPROTO_RAW;
			break;
		case 'S':
			cfg_sleep_usec = strtoul(optarg, NULL, 10);
			break;
		case 't':
			cfg_delay_tolerance_usec = strtoul(optarg, NULL, 10);
			break;
		case 'u':
			proto_count++;
			cfg_proto = SOCK_DGRAM;
			cfg_ipproto = IPPROTO_UDP;
			break;
		case 'v':
			cfg_delay_snd = strtoul(optarg, NULL, 10);
			break;
		case 'V':
			cfg_delay_ack = strtoul(optarg, NULL, 10);
			break;
		case 'x':
			cfg_show_payload = true;
			break;
		case 'h':
		default:
			usage(argv[0]);
		}
	}

	if (!cfg_payload_len)
		error(1, 0, "payload may not be nonzero");
	if (cfg_proto != SOCK_STREAM && cfg_payload_len > 1472)
		error(1, 0, "udp packet might exceed expected MTU");
	if (!do_ipv4 && !do_ipv6)
		error(1, 0, "pass -4 or -6, not both");
	if (proto_count > 1)
		error(1, 0, "pass -P, -r, -R or -u, not multiple");
	if (cfg_do_pktinfo && cfg_use_pf_packet)
		error(1, 0, "cannot ask for pktinfo over pf_packet");
	if (cfg_busy_poll && cfg_use_epoll)
		error(1, 0, "pass epoll or busy_poll, not both");

	if (optind != argc - 1)
		error(1, 0, "missing required hostname argument");
}

static void resolve_hostname(const char *hostname)
{
	struct addrinfo hints = { .ai_family = do_ipv4 ? AF_INET : AF_INET6 };
	struct addrinfo *addrs, *cur;
	int have_ipv4 = 0, have_ipv6 = 0;

retry:
	if (getaddrinfo(hostname, NULL, &hints, &addrs))
		error(1, errno, "getaddrinfo");

	cur = addrs;
	while (cur && !have_ipv4 && !have_ipv6) {
		if (!have_ipv4 && cur->ai_family == AF_INET) {
			memcpy(&daddr, cur->ai_addr, sizeof(daddr));
			daddr.sin_port = htons(dest_port);
			have_ipv4 = 1;
		}
		else if (!have_ipv6 && cur->ai_family == AF_INET6) {
			memcpy(&daddr6, cur->ai_addr, sizeof(daddr6));
			daddr6.sin6_port = htons(dest_port);
			have_ipv6 = 1;
		}
		cur = cur->ai_next;
	}
	if (addrs)
		freeaddrinfo(addrs);

	if (do_ipv6 && hints.ai_family != AF_INET6) {
		hints.ai_family = AF_INET6;
		goto retry;
	}

	do_ipv4 &= have_ipv4;
	do_ipv6 &= have_ipv6;
}

static void do_listen(int family, void *addr, int alen)
{
	int fd, type;

	type = cfg_proto == SOCK_RAW ? SOCK_DGRAM : cfg_proto;

	fd = socket(family, type, 0);
	if (fd == -1)
		error(1, errno, "socket rx");

	if (bind(fd, addr, alen))
		error(1, errno, "bind rx");

	if (type == SOCK_STREAM && listen(fd, 10))
		error(1, errno, "listen rx");

	/* leave fd open, will be closed on process exit.
	 * this enables connect() to succeed and avoids icmp replies
	 */
}

static void do_main(int family)
{
	fprintf(stderr, "family:       %s %s\n",
			family == PF_INET ? "INET" : "INET6",
			cfg_use_pf_packet ? "(PF_PACKET)" : "");

	fprintf(stderr, "test SND\n");
	do_test(family, SOF_TIMESTAMPING_TX_SOFTWARE);

	fprintf(stderr, "test ENQ\n");
	do_test(family, SOF_TIMESTAMPING_TX_SCHED);

	fprintf(stderr, "test ENQ + SND\n");
	do_test(family, SOF_TIMESTAMPING_TX_SCHED |
			SOF_TIMESTAMPING_TX_SOFTWARE);

	if (cfg_proto == SOCK_STREAM) {
		fprintf(stderr, "\ntest ACK\n");
		do_test(family, SOF_TIMESTAMPING_TX_ACK);

		fprintf(stderr, "\ntest SND + ACK\n");
		do_test(family, SOF_TIMESTAMPING_TX_SOFTWARE |
				SOF_TIMESTAMPING_TX_ACK);

		fprintf(stderr, "\ntest ENQ + SND + ACK\n");
		do_test(family, SOF_TIMESTAMPING_TX_SCHED |
				SOF_TIMESTAMPING_TX_SOFTWARE |
				SOF_TIMESTAMPING_TX_ACK);
	}
}

const char *sock_names[] = { NULL, "TCP", "UDP", "RAW" };

int main(int argc, char **argv)
{
	if (argc == 1)
		usage(argv[0]);

	parse_opt(argc, argv);
	resolve_hostname(argv[argc - 1]);

	fprintf(stderr, "protocol:     %s\n", sock_names[cfg_proto]);
	fprintf(stderr, "payload:      %u\n", cfg_payload_len);
	fprintf(stderr, "server port:  %u\n", dest_port);
	fprintf(stderr, "\n");

	if (do_ipv4) {
		if (cfg_do_listen)
			do_listen(PF_INET, &daddr, sizeof(daddr));
		do_main(PF_INET);
	}

	if (do_ipv6) {
		if (cfg_do_listen)
			do_listen(PF_INET6, &daddr6, sizeof(daddr6));
		do_main(PF_INET6);
	}

	return test_failed;
}
