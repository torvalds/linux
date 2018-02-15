/* Evaluate MSG_ZEROCOPY
 *
 * Send traffic between two processes over one of the supported
 * protocols and modes:
 *
 * PF_INET/PF_INET6
 * - SOCK_STREAM
 * - SOCK_DGRAM
 * - SOCK_DGRAM with UDP_CORK
 * - SOCK_RAW
 * - SOCK_RAW with IP_HDRINCL
 *
 * PF_PACKET
 * - SOCK_DGRAM
 * - SOCK_RAW
 *
 * PF_RDS
 * - SOCK_SEQPACKET
 *
 * Start this program on two connected hosts, one in send mode and
 * the other with option '-r' to put it in receiver mode.
 *
 * If zerocopy mode ('-z') is enabled, the sender will verify that
 * the kernel queues completions on the error queue for all zerocopy
 * transfers.
 */

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <error.h>
#include <errno.h>
#include <limits.h>
#include <linux/errqueue.h>
#include <linux/if_packet.h>
#include <linux/ipv6.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <poll.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <linux/rds.h>

#ifndef SO_EE_ORIGIN_ZEROCOPY
#define SO_EE_ORIGIN_ZEROCOPY		5
#endif

#ifndef SO_ZEROCOPY
#define SO_ZEROCOPY	60
#endif

#ifndef SO_EE_CODE_ZEROCOPY_COPIED
#define SO_EE_CODE_ZEROCOPY_COPIED	1
#endif

#ifndef MSG_ZEROCOPY
#define MSG_ZEROCOPY	0x4000000
#endif

static int  cfg_cork;
static bool cfg_cork_mixed;
static int  cfg_cpu		= -1;		/* default: pin to last cpu */
static int  cfg_family		= PF_UNSPEC;
static int  cfg_ifindex		= 1;
static int  cfg_payload_len;
static int  cfg_port		= 8000;
static bool cfg_rx;
static int  cfg_runtime_ms	= 4200;
static int  cfg_verbose;
static int  cfg_waittime_ms	= 500;
static bool cfg_zerocopy;

static socklen_t cfg_alen;
static struct sockaddr_storage cfg_dst_addr;
static struct sockaddr_storage cfg_src_addr;

static char payload[IP_MAXPACKET];
static long packets, bytes, completions, expected_completions;
static int  zerocopied = -1;
static uint32_t next_completion;

static unsigned long gettimeofday_ms(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}

static uint16_t get_ip_csum(const uint16_t *start, int num_words)
{
	unsigned long sum = 0;
	int i;

	for (i = 0; i < num_words; i++)
		sum += start[i];

	while (sum >> 16)
		sum = (sum & 0xFFFF) + (sum >> 16);

	return ~sum;
}

static int do_setcpu(int cpu)
{
	cpu_set_t mask;

	CPU_ZERO(&mask);
	CPU_SET(cpu, &mask);
	if (sched_setaffinity(0, sizeof(mask), &mask))
		error(1, 0, "setaffinity %d", cpu);

	if (cfg_verbose)
		fprintf(stderr, "cpu: %u\n", cpu);

	return 0;
}

static void do_setsockopt(int fd, int level, int optname, int val)
{
	if (setsockopt(fd, level, optname, &val, sizeof(val)))
		error(1, errno, "setsockopt %d.%d: %d", level, optname, val);
}

static int do_poll(int fd, int events)
{
	struct pollfd pfd;
	int ret;

	pfd.events = events;
	pfd.revents = 0;
	pfd.fd = fd;

	ret = poll(&pfd, 1, cfg_waittime_ms);
	if (ret == -1)
		error(1, errno, "poll");

	return ret && (pfd.revents & events);
}

static int do_accept(int fd)
{
	int fda = fd;

	fd = accept(fda, NULL, NULL);
	if (fd == -1)
		error(1, errno, "accept");
	if (close(fda))
		error(1, errno, "close listen sock");

	return fd;
}

static void add_zcopy_cookie(struct msghdr *msg, uint32_t cookie)
{
	struct cmsghdr *cm;

	if (!msg->msg_control)
		error(1, errno, "NULL cookie");
	cm = (void *)msg->msg_control;
	cm->cmsg_len = CMSG_LEN(sizeof(cookie));
	cm->cmsg_level = SOL_RDS;
	cm->cmsg_type = RDS_CMSG_ZCOPY_COOKIE;
	memcpy(CMSG_DATA(cm), &cookie, sizeof(cookie));
}

static bool do_sendmsg(int fd, struct msghdr *msg, bool do_zerocopy, int domain)
{
	int ret, len, i, flags;
	static uint32_t cookie;
	char ckbuf[CMSG_SPACE(sizeof(cookie))];

	len = 0;
	for (i = 0; i < msg->msg_iovlen; i++)
		len += msg->msg_iov[i].iov_len;

	flags = MSG_DONTWAIT;
	if (do_zerocopy) {
		flags |= MSG_ZEROCOPY;
		if (domain == PF_RDS) {
			memset(&msg->msg_control, 0, sizeof(msg->msg_control));
			msg->msg_controllen = CMSG_SPACE(sizeof(cookie));
			msg->msg_control = (struct cmsghdr *)ckbuf;
			add_zcopy_cookie(msg, ++cookie);
		}
	}

	ret = sendmsg(fd, msg, flags);
	if (ret == -1 && errno == EAGAIN)
		return false;
	if (ret == -1)
		error(1, errno, "send");
	if (cfg_verbose && ret != len)
		fprintf(stderr, "send: ret=%u != %u\n", ret, len);

	if (len) {
		packets++;
		bytes += ret;
		if (do_zerocopy && ret)
			expected_completions++;
	}
	if (do_zerocopy && domain == PF_RDS) {
		msg->msg_control = NULL;
		msg->msg_controllen = 0;
	}

	return true;
}

static void do_sendmsg_corked(int fd, struct msghdr *msg)
{
	bool do_zerocopy = cfg_zerocopy;
	int i, payload_len, extra_len;

	/* split up the packet. for non-multiple, make first buffer longer */
	payload_len = cfg_payload_len / cfg_cork;
	extra_len = cfg_payload_len - (cfg_cork * payload_len);

	do_setsockopt(fd, IPPROTO_UDP, UDP_CORK, 1);

	for (i = 0; i < cfg_cork; i++) {

		/* in mixed-frags mode, alternate zerocopy and copy frags
		 * start with non-zerocopy, to ensure attach later works
		 */
		if (cfg_cork_mixed)
			do_zerocopy = (i & 1);

		msg->msg_iov[0].iov_len = payload_len + extra_len;
		extra_len = 0;

		do_sendmsg(fd, msg, do_zerocopy,
			   (cfg_dst_addr.ss_family == AF_INET ?
			    PF_INET : PF_INET6));
	}

	do_setsockopt(fd, IPPROTO_UDP, UDP_CORK, 0);
}

static int setup_iph(struct iphdr *iph, uint16_t payload_len)
{
	struct sockaddr_in *daddr = (void *) &cfg_dst_addr;
	struct sockaddr_in *saddr = (void *) &cfg_src_addr;

	memset(iph, 0, sizeof(*iph));

	iph->version	= 4;
	iph->tos	= 0;
	iph->ihl	= 5;
	iph->ttl	= 2;
	iph->saddr	= saddr->sin_addr.s_addr;
	iph->daddr	= daddr->sin_addr.s_addr;
	iph->protocol	= IPPROTO_EGP;
	iph->tot_len	= htons(sizeof(*iph) + payload_len);
	iph->check	= get_ip_csum((void *) iph, iph->ihl << 1);

	return sizeof(*iph);
}

static int setup_ip6h(struct ipv6hdr *ip6h, uint16_t payload_len)
{
	struct sockaddr_in6 *daddr = (void *) &cfg_dst_addr;
	struct sockaddr_in6 *saddr = (void *) &cfg_src_addr;

	memset(ip6h, 0, sizeof(*ip6h));

	ip6h->version		= 6;
	ip6h->payload_len	= htons(payload_len);
	ip6h->nexthdr		= IPPROTO_EGP;
	ip6h->hop_limit		= 2;
	ip6h->saddr		= saddr->sin6_addr;
	ip6h->daddr		= daddr->sin6_addr;

	return sizeof(*ip6h);
}


static void setup_sockaddr(int domain, const char *str_addr,
			   struct sockaddr_storage *sockaddr)
{
	struct sockaddr_in6 *addr6 = (void *) sockaddr;
	struct sockaddr_in *addr4 = (void *) sockaddr;

	switch (domain) {
	case PF_INET:
		memset(addr4, 0, sizeof(*addr4));
		addr4->sin_family = AF_INET;
		addr4->sin_port = htons(cfg_port);
		if (str_addr &&
		    inet_pton(AF_INET, str_addr, &(addr4->sin_addr)) != 1)
			error(1, 0, "ipv4 parse error: %s", str_addr);
		break;
	case PF_INET6:
		memset(addr6, 0, sizeof(*addr6));
		addr6->sin6_family = AF_INET6;
		addr6->sin6_port = htons(cfg_port);
		if (str_addr &&
		    inet_pton(AF_INET6, str_addr, &(addr6->sin6_addr)) != 1)
			error(1, 0, "ipv6 parse error: %s", str_addr);
		break;
	default:
		error(1, 0, "illegal domain");
	}
}

static int do_setup_tx(int domain, int type, int protocol)
{
	int fd;

	fd = socket(domain, type, protocol);
	if (fd == -1)
		error(1, errno, "socket t");

	do_setsockopt(fd, SOL_SOCKET, SO_SNDBUF, 1 << 21);
	if (cfg_zerocopy)
		do_setsockopt(fd, SOL_SOCKET, SO_ZEROCOPY, 1);

	if (domain != PF_PACKET && domain != PF_RDS)
		if (connect(fd, (void *) &cfg_dst_addr, cfg_alen))
			error(1, errno, "connect");

	if (domain == PF_RDS) {
		if (bind(fd, (void *) &cfg_src_addr, cfg_alen))
			error(1, errno, "bind");
	}

	return fd;
}

static int do_process_zerocopy_cookies(struct sock_extended_err *serr,
				       uint32_t *ckbuf, size_t nbytes)
{
	int ncookies, i;

	if (serr->ee_errno != 0)
		error(1, 0, "serr: wrong error code: %u", serr->ee_errno);
	ncookies = serr->ee_data;
	if (ncookies > SO_EE_ORIGIN_MAX_ZCOOKIES)
		error(1, 0, "Returned %d cookies, max expected %d\n",
		      ncookies, SO_EE_ORIGIN_MAX_ZCOOKIES);
	if (nbytes != ncookies * sizeof(uint32_t))
		error(1, 0, "Expected %d cookies, got %ld\n",
		      ncookies, nbytes/sizeof(uint32_t));
	for (i = 0; i < ncookies; i++)
		if (cfg_verbose >= 2)
			fprintf(stderr, "%d\n", ckbuf[i]);
	return ncookies;
}

static bool do_recv_completion(int fd)
{
	struct sock_extended_err *serr;
	struct msghdr msg = {};
	struct cmsghdr *cm;
	uint32_t hi, lo, range;
	int ret, zerocopy;
	char control[100];
	uint32_t ckbuf[SO_EE_ORIGIN_MAX_ZCOOKIES];
	struct iovec iov;

	msg.msg_control = control;
	msg.msg_controllen = sizeof(control);

	iov.iov_base = ckbuf;
	iov.iov_len = (SO_EE_ORIGIN_MAX_ZCOOKIES * sizeof(ckbuf[0]));
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	ret = recvmsg(fd, &msg, MSG_ERRQUEUE);
	if (ret == -1 && errno == EAGAIN)
		return false;
	if (ret == -1)
		error(1, errno, "recvmsg notification");
	if (msg.msg_flags & MSG_CTRUNC)
		error(1, errno, "recvmsg notification: truncated");

	cm = CMSG_FIRSTHDR(&msg);
	if (!cm)
		error(1, 0, "cmsg: no cmsg");
	if (!((cm->cmsg_level == SOL_IP && cm->cmsg_type == IP_RECVERR) ||
	      (cm->cmsg_level == SOL_IPV6 && cm->cmsg_type == IPV6_RECVERR) ||
	      (cm->cmsg_level == SOL_PACKET && cm->cmsg_type == PACKET_TX_TIMESTAMP)))
		error(1, 0, "serr: wrong type: %d.%d",
		      cm->cmsg_level, cm->cmsg_type);

	serr = (void *) CMSG_DATA(cm);

	if (serr->ee_origin == SO_EE_ORIGIN_ZCOOKIE) {
		completions += do_process_zerocopy_cookies(serr, ckbuf, ret);
		return true;
	}
	if (serr->ee_origin != SO_EE_ORIGIN_ZEROCOPY)
		error(1, 0, "serr: wrong origin: %u", serr->ee_origin);
	if (serr->ee_errno != 0)
		error(1, 0, "serr: wrong error code: %u", serr->ee_errno);

	hi = serr->ee_data;
	lo = serr->ee_info;
	range = hi - lo + 1;

	/* Detect notification gaps. These should not happen often, if at all.
	 * Gaps can occur due to drops, reordering and retransmissions.
	 */
	if (lo != next_completion)
		fprintf(stderr, "gap: %u..%u does not append to %u\n",
			lo, hi, next_completion);
	next_completion = hi + 1;

	zerocopy = !(serr->ee_code & SO_EE_CODE_ZEROCOPY_COPIED);
	if (zerocopied == -1)
		zerocopied = zerocopy;
	else if (zerocopied != zerocopy) {
		fprintf(stderr, "serr: inconsistent\n");
		zerocopied = zerocopy;
	}

	if (cfg_verbose >= 2)
		fprintf(stderr, "completed: %u (h=%u l=%u)\n",
			range, hi, lo);

	completions += range;
	return true;
}

/* Read all outstanding messages on the errqueue */
static void do_recv_completions(int fd)
{
	while (do_recv_completion(fd)) {}
}

/* Wait for all remaining completions on the errqueue */
static void do_recv_remaining_completions(int fd)
{
	int64_t tstop = gettimeofday_ms() + cfg_waittime_ms;

	while (completions < expected_completions &&
	       gettimeofday_ms() < tstop) {
		if (do_poll(fd, POLLERR))
			do_recv_completions(fd);
	}

	if (completions < expected_completions)
		fprintf(stderr, "missing notifications: %lu < %lu\n",
			completions, expected_completions);
}

static void do_tx(int domain, int type, int protocol)
{
	struct iovec iov[3] = { {0} };
	struct sockaddr_ll laddr;
	struct msghdr msg = {0};
	struct ethhdr eth;
	union {
		struct ipv6hdr ip6h;
		struct iphdr iph;
	} nh;
	uint64_t tstop;
	int fd;

	fd = do_setup_tx(domain, type, protocol);

	if (domain == PF_PACKET) {
		uint16_t proto = cfg_family == PF_INET ? ETH_P_IP : ETH_P_IPV6;

		/* sock_raw passes ll header as data */
		if (type == SOCK_RAW) {
			memset(eth.h_dest, 0x06, ETH_ALEN);
			memset(eth.h_source, 0x02, ETH_ALEN);
			eth.h_proto = htons(proto);
			iov[0].iov_base = &eth;
			iov[0].iov_len = sizeof(eth);
			msg.msg_iovlen++;
		}

		/* both sock_raw and sock_dgram expect name */
		memset(&laddr, 0, sizeof(laddr));
		laddr.sll_family	= AF_PACKET;
		laddr.sll_ifindex	= cfg_ifindex;
		laddr.sll_protocol	= htons(proto);
		laddr.sll_halen		= ETH_ALEN;

		memset(laddr.sll_addr, 0x06, ETH_ALEN);

		msg.msg_name		= &laddr;
		msg.msg_namelen		= sizeof(laddr);
	}

	/* packet and raw sockets with hdrincl must pass network header */
	if (domain == PF_PACKET || protocol == IPPROTO_RAW) {
		if (cfg_family == PF_INET)
			iov[1].iov_len = setup_iph(&nh.iph, cfg_payload_len);
		else
			iov[1].iov_len = setup_ip6h(&nh.ip6h, cfg_payload_len);

		iov[1].iov_base = (void *) &nh;
		msg.msg_iovlen++;
	}

	if (domain == PF_RDS) {
		msg.msg_name = &cfg_dst_addr;
		msg.msg_namelen =  (cfg_dst_addr.ss_family == AF_INET ?
				    sizeof(struct sockaddr_in) :
				    sizeof(struct sockaddr_in6));
	}

	iov[2].iov_base = payload;
	iov[2].iov_len = cfg_payload_len;
	msg.msg_iovlen++;
	msg.msg_iov = &iov[3 - msg.msg_iovlen];

	tstop = gettimeofday_ms() + cfg_runtime_ms;
	do {
		if (cfg_cork)
			do_sendmsg_corked(fd, &msg);
		else
			do_sendmsg(fd, &msg, cfg_zerocopy, domain);

		while (!do_poll(fd, POLLOUT)) {
			if (cfg_zerocopy)
				do_recv_completions(fd);
		}

	} while (gettimeofday_ms() < tstop);

	if (cfg_zerocopy)
		do_recv_remaining_completions(fd);

	if (close(fd))
		error(1, errno, "close");

	fprintf(stderr, "tx=%lu (%lu MB) txc=%lu zc=%c\n",
		packets, bytes >> 20, completions,
		zerocopied == 1 ? 'y' : 'n');
}

static int do_setup_rx(int domain, int type, int protocol)
{
	int fd;

	/* If tx over PF_PACKET, rx over PF_INET(6)/SOCK_RAW,
	 * to recv the only copy of the packet, not a clone
	 */
	if (domain == PF_PACKET)
		error(1, 0, "Use PF_INET/SOCK_RAW to read");

	if (type == SOCK_RAW && protocol == IPPROTO_RAW)
		error(1, 0, "IPPROTO_RAW: not supported on Rx");

	fd = socket(domain, type, protocol);
	if (fd == -1)
		error(1, errno, "socket r");

	do_setsockopt(fd, SOL_SOCKET, SO_RCVBUF, 1 << 21);
	do_setsockopt(fd, SOL_SOCKET, SO_RCVLOWAT, 1 << 16);
	do_setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, 1);

	if (bind(fd, (void *) &cfg_dst_addr, cfg_alen))
		error(1, errno, "bind");

	if (type == SOCK_STREAM) {
		if (listen(fd, 1))
			error(1, errno, "listen");
		fd = do_accept(fd);
	}

	return fd;
}

/* Flush all outstanding bytes for the tcp receive queue */
static void do_flush_tcp(int fd)
{
	int ret;

	/* MSG_TRUNC flushes up to len bytes */
	ret = recv(fd, NULL, 1 << 21, MSG_TRUNC | MSG_DONTWAIT);
	if (ret == -1 && errno == EAGAIN)
		return;
	if (ret == -1)
		error(1, errno, "flush");
	if (!ret)
		return;

	packets++;
	bytes += ret;
}

/* Flush all outstanding datagrams. Verify first few bytes of each. */
static void do_flush_datagram(int fd, int type)
{
	int ret, off = 0;
	char buf[64];

	/* MSG_TRUNC will return full datagram length */
	ret = recv(fd, buf, sizeof(buf), MSG_DONTWAIT | MSG_TRUNC);
	if (ret == -1 && errno == EAGAIN)
		return;

	/* raw ipv4 return with header, raw ipv6 without */
	if (cfg_family == PF_INET && type == SOCK_RAW) {
		off += sizeof(struct iphdr);
		ret -= sizeof(struct iphdr);
	}

	if (ret == -1)
		error(1, errno, "recv");
	if (ret != cfg_payload_len)
		error(1, 0, "recv: ret=%u != %u", ret, cfg_payload_len);
	if (ret > sizeof(buf) - off)
		ret = sizeof(buf) - off;
	if (memcmp(buf + off, payload, ret))
		error(1, 0, "recv: data mismatch");

	packets++;
	bytes += cfg_payload_len;
}


static void do_recvmsg(int fd)
{
	int ret, off = 0;
	char *buf;
	struct iovec iov;
	struct msghdr msg;
	struct sockaddr_storage din;

	buf = calloc(cfg_payload_len, sizeof(char));
	iov.iov_base = buf;
	iov.iov_len = cfg_payload_len;

	memset(&msg, 0, sizeof(msg));
	msg.msg_name = &din;
	msg.msg_namelen = sizeof(din);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	ret = recvmsg(fd, &msg, MSG_TRUNC);

	if (ret == -1)
		error(1, errno, "recv");
	if (ret != cfg_payload_len)
		error(1, 0, "recv: ret=%u != %u", ret, cfg_payload_len);

	if (memcmp(buf + off, payload, ret))
		error(1, 0, "recv: data mismatch");

	free(buf);
	packets++;
	bytes += cfg_payload_len;
}

static void do_rx(int domain, int type, int protocol)
{
	uint64_t tstop;
	int fd;

	fd = do_setup_rx(domain, type, protocol);

	tstop = gettimeofday_ms() + cfg_runtime_ms;
	do {
		if (type == SOCK_STREAM)
			do_flush_tcp(fd);
		else if (domain == PF_RDS)
			do_recvmsg(fd);
		else
			do_flush_datagram(fd, type);

		do_poll(fd, POLLIN);

	} while (gettimeofday_ms() < tstop);

	if (close(fd))
		error(1, errno, "close");

	fprintf(stderr, "rx=%lu (%lu MB)\n", packets, bytes >> 20);
}

static void do_test(int domain, int type, int protocol)
{
	int i;

	if (cfg_cork && (domain == PF_PACKET || type != SOCK_DGRAM))
		error(1, 0, "can only cork udp sockets");

	do_setcpu(cfg_cpu);

	for (i = 0; i < IP_MAXPACKET; i++)
		payload[i] = 'a' + (i % 26);

	if (cfg_rx)
		do_rx(domain, type, protocol);
	else
		do_tx(domain, type, protocol);
}

static void usage(const char *filepath)
{
	error(1, 0, "Usage: %s [options] <test>", filepath);
}

static void parse_opts(int argc, char **argv)
{
	const int max_payload_len = sizeof(payload) -
				    sizeof(struct ipv6hdr) -
				    sizeof(struct tcphdr) -
				    40 /* max tcp options */;
	int c;
	char *daddr = NULL, *saddr = NULL;
	char *cfg_test;

	cfg_payload_len = max_payload_len;

	while ((c = getopt(argc, argv, "46c:C:D:i:mp:rs:S:t:vz")) != -1) {
		switch (c) {
		case '4':
			if (cfg_family != PF_UNSPEC)
				error(1, 0, "Pass one of -4 or -6");
			cfg_family = PF_INET;
			cfg_alen = sizeof(struct sockaddr_in);
			break;
		case '6':
			if (cfg_family != PF_UNSPEC)
				error(1, 0, "Pass one of -4 or -6");
			cfg_family = PF_INET6;
			cfg_alen = sizeof(struct sockaddr_in6);
			break;
		case 'c':
			cfg_cork = strtol(optarg, NULL, 0);
			break;
		case 'C':
			cfg_cpu = strtol(optarg, NULL, 0);
			break;
		case 'D':
			daddr = optarg;
			break;
		case 'i':
			cfg_ifindex = if_nametoindex(optarg);
			if (cfg_ifindex == 0)
				error(1, errno, "invalid iface: %s", optarg);
			break;
		case 'm':
			cfg_cork_mixed = true;
			break;
		case 'p':
			cfg_port = strtoul(optarg, NULL, 0);
			break;
		case 'r':
			cfg_rx = true;
			break;
		case 's':
			cfg_payload_len = strtoul(optarg, NULL, 0);
			break;
		case 'S':
			saddr = optarg;
			break;
		case 't':
			cfg_runtime_ms = 200 + strtoul(optarg, NULL, 10) * 1000;
			break;
		case 'v':
			cfg_verbose++;
			break;
		case 'z':
			cfg_zerocopy = true;
			break;
		}
	}

	cfg_test = argv[argc - 1];
	if (strcmp(cfg_test, "rds") == 0) {
		if (!daddr)
			error(1, 0, "-D <server addr> required for PF_RDS\n");
		if (!cfg_rx && !saddr)
			error(1, 0, "-S <client addr> required for PF_RDS\n");
	}
	setup_sockaddr(cfg_family, daddr, &cfg_dst_addr);
	setup_sockaddr(cfg_family, saddr, &cfg_src_addr);

	if (cfg_payload_len > max_payload_len)
		error(1, 0, "-s: payload exceeds max (%d)", max_payload_len);
	if (cfg_cork_mixed && (!cfg_zerocopy || !cfg_cork))
		error(1, 0, "-m: cork_mixed requires corking and zerocopy");

	if (optind != argc - 1)
		usage(argv[0]);
}

int main(int argc, char **argv)
{
	const char *cfg_test;

	parse_opts(argc, argv);

	cfg_test = argv[argc - 1];

	if (!strcmp(cfg_test, "packet"))
		do_test(PF_PACKET, SOCK_RAW, 0);
	else if (!strcmp(cfg_test, "packet_dgram"))
		do_test(PF_PACKET, SOCK_DGRAM, 0);
	else if (!strcmp(cfg_test, "raw"))
		do_test(cfg_family, SOCK_RAW, IPPROTO_EGP);
	else if (!strcmp(cfg_test, "raw_hdrincl"))
		do_test(cfg_family, SOCK_RAW, IPPROTO_RAW);
	else if (!strcmp(cfg_test, "tcp"))
		do_test(cfg_family, SOCK_STREAM, 0);
	else if (!strcmp(cfg_test, "udp"))
		do_test(cfg_family, SOCK_DGRAM, 0);
	else if (!strcmp(cfg_test, "rds"))
		do_test(PF_RDS, SOCK_SEQPACKET, 0);
	else
		error(1, 0, "unknown cfg_test %s", cfg_test);

	return 0;
}
