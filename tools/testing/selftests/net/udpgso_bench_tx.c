// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <error.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/udp.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef ETH_MAX_MTU
#define ETH_MAX_MTU 0xFFFFU
#endif

#ifndef UDP_SEGMENT
#define UDP_SEGMENT		103
#endif

#ifndef SO_ZEROCOPY
#define SO_ZEROCOPY	60
#endif

#ifndef MSG_ZEROCOPY
#define MSG_ZEROCOPY	0x4000000
#endif

#define NUM_PKT		100

static bool	cfg_cache_trash;
static int	cfg_cpu		= -1;
static int	cfg_connected	= true;
static int	cfg_family	= PF_UNSPEC;
static uint16_t	cfg_mss;
static int	cfg_payload_len	= (1472 * 42);
static int	cfg_port	= 8000;
static int	cfg_runtime_ms	= -1;
static bool	cfg_segment;
static bool	cfg_sendmmsg;
static bool	cfg_tcp;
static bool	cfg_zerocopy;
static int	cfg_msg_nr;
static uint16_t	cfg_gso_size;

static socklen_t cfg_alen;
static struct sockaddr_storage cfg_dst_addr;

static bool interrupted;
static char buf[NUM_PKT][ETH_MAX_MTU];

static void sigint_handler(int signum)
{
	if (signum == SIGINT)
		interrupted = true;
}

static unsigned long gettimeofday_ms(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}

static int set_cpu(int cpu)
{
	cpu_set_t mask;

	CPU_ZERO(&mask);
	CPU_SET(cpu, &mask);
	if (sched_setaffinity(0, sizeof(mask), &mask))
		error(1, 0, "setaffinity %d", cpu);

	return 0;
}

static void setup_sockaddr(int domain, const char *str_addr, void *sockaddr)
{
	struct sockaddr_in6 *addr6 = (void *) sockaddr;
	struct sockaddr_in *addr4 = (void *) sockaddr;

	switch (domain) {
	case PF_INET:
		addr4->sin_family = AF_INET;
		addr4->sin_port = htons(cfg_port);
		if (inet_pton(AF_INET, str_addr, &(addr4->sin_addr)) != 1)
			error(1, 0, "ipv4 parse error: %s", str_addr);
		break;
	case PF_INET6:
		addr6->sin6_family = AF_INET6;
		addr6->sin6_port = htons(cfg_port);
		if (inet_pton(AF_INET6, str_addr, &(addr6->sin6_addr)) != 1)
			error(1, 0, "ipv6 parse error: %s", str_addr);
		break;
	default:
		error(1, 0, "illegal domain");
	}
}

static void flush_zerocopy(int fd)
{
	struct msghdr msg = {0};	/* flush */
	int ret;

	while (1) {
		ret = recvmsg(fd, &msg, MSG_ERRQUEUE);
		if (ret == -1 && errno == EAGAIN)
			break;
		if (ret == -1)
			error(1, errno, "errqueue");
		if (msg.msg_flags != (MSG_ERRQUEUE | MSG_CTRUNC))
			error(1, 0, "errqueue: flags 0x%x\n", msg.msg_flags);
		msg.msg_flags = 0;
	}
}

static int send_tcp(int fd, char *data)
{
	int ret, done = 0, count = 0;

	while (done < cfg_payload_len) {
		ret = send(fd, data + done, cfg_payload_len - done,
			   cfg_zerocopy ? MSG_ZEROCOPY : 0);
		if (ret == -1)
			error(1, errno, "write");

		done += ret;
		count++;
	}

	return count;
}

static int send_udp(int fd, char *data)
{
	int ret, total_len, len, count = 0;

	total_len = cfg_payload_len;

	while (total_len) {
		len = total_len < cfg_mss ? total_len : cfg_mss;

		ret = sendto(fd, data, len, cfg_zerocopy ? MSG_ZEROCOPY : 0,
			     cfg_connected ? NULL : (void *)&cfg_dst_addr,
			     cfg_connected ? 0 : cfg_alen);
		if (ret == -1)
			error(1, errno, "write");
		if (ret != len)
			error(1, errno, "write: %uB != %uB\n", ret, len);

		total_len -= len;
		count++;
	}

	return count;
}

static int send_udp_sendmmsg(int fd, char *data)
{
	const int max_nr_msg = ETH_MAX_MTU / ETH_DATA_LEN;
	struct mmsghdr mmsgs[max_nr_msg];
	struct iovec iov[max_nr_msg];
	unsigned int off = 0, left;
	int i = 0, ret;

	memset(mmsgs, 0, sizeof(mmsgs));

	left = cfg_payload_len;
	while (left) {
		if (i == max_nr_msg)
			error(1, 0, "sendmmsg: exceeds max_nr_msg");

		iov[i].iov_base = data + off;
		iov[i].iov_len = cfg_mss < left ? cfg_mss : left;

		mmsgs[i].msg_hdr.msg_iov = iov + i;
		mmsgs[i].msg_hdr.msg_iovlen = 1;

		off += iov[i].iov_len;
		left -= iov[i].iov_len;
		i++;
	}

	ret = sendmmsg(fd, mmsgs, i, cfg_zerocopy ? MSG_ZEROCOPY : 0);
	if (ret == -1)
		error(1, errno, "sendmmsg");

	return ret;
}

static void send_udp_segment_cmsg(struct cmsghdr *cm)
{
	uint16_t *valp;

	cm->cmsg_level = SOL_UDP;
	cm->cmsg_type = UDP_SEGMENT;
	cm->cmsg_len = CMSG_LEN(sizeof(cfg_gso_size));
	valp = (void *)CMSG_DATA(cm);
	*valp = cfg_gso_size;
}

static int send_udp_segment(int fd, char *data)
{
	char control[CMSG_SPACE(sizeof(cfg_gso_size))] = {0};
	struct msghdr msg = {0};
	struct iovec iov = {0};
	int ret;

	iov.iov_base = data;
	iov.iov_len = cfg_payload_len;

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	msg.msg_control = control;
	msg.msg_controllen = sizeof(control);
	send_udp_segment_cmsg(CMSG_FIRSTHDR(&msg));

	msg.msg_name = (void *)&cfg_dst_addr;
	msg.msg_namelen = cfg_alen;

	ret = sendmsg(fd, &msg, cfg_zerocopy ? MSG_ZEROCOPY : 0);
	if (ret == -1)
		error(1, errno, "sendmsg");
	if (ret != iov.iov_len)
		error(1, 0, "sendmsg: %u != %lu\n", ret, iov.iov_len);

	return 1;
}

static void usage(const char *filepath)
{
	error(1, 0, "Usage: %s [-46cmtuz] [-C cpu] [-D dst ip] [-l secs] [-m messagenr] [-p port] [-s sendsize] [-S gsosize]",
		    filepath);
}

static void parse_opts(int argc, char **argv)
{
	int max_len, hdrlen;
	int c;

	while ((c = getopt(argc, argv, "46cC:D:l:mM:p:s:S:tuz")) != -1) {
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
			cfg_cache_trash = true;
			break;
		case 'C':
			cfg_cpu = strtol(optarg, NULL, 0);
			break;
		case 'D':
			setup_sockaddr(cfg_family, optarg, &cfg_dst_addr);
			break;
		case 'l':
			cfg_runtime_ms = strtoul(optarg, NULL, 10) * 1000;
			break;
		case 'm':
			cfg_sendmmsg = true;
			break;
		case 'M':
			cfg_msg_nr = strtoul(optarg, NULL, 10);
			break;
		case 'p':
			cfg_port = strtoul(optarg, NULL, 0);
			break;
		case 's':
			cfg_payload_len = strtoul(optarg, NULL, 0);
			break;
		case 'S':
			cfg_gso_size = strtoul(optarg, NULL, 0);
			cfg_segment = true;
			break;
		case 't':
			cfg_tcp = true;
			break;
		case 'u':
			cfg_connected = false;
			break;
		case 'z':
			cfg_zerocopy = true;
			break;
		}
	}

	if (optind != argc)
		usage(argv[0]);

	if (cfg_family == PF_UNSPEC)
		error(1, 0, "must pass one of -4 or -6");
	if (cfg_tcp && !cfg_connected)
		error(1, 0, "connectionless tcp makes no sense");
	if (cfg_segment && cfg_sendmmsg)
		error(1, 0, "cannot combine segment offload and sendmmsg");

	if (cfg_family == PF_INET)
		hdrlen = sizeof(struct iphdr) + sizeof(struct udphdr);
	else
		hdrlen = sizeof(struct ip6_hdr) + sizeof(struct udphdr);

	cfg_mss = ETH_DATA_LEN - hdrlen;
	max_len = ETH_MAX_MTU - hdrlen;
	if (!cfg_gso_size)
		cfg_gso_size = cfg_mss;

	if (cfg_payload_len > max_len)
		error(1, 0, "payload length %u exceeds max %u",
		      cfg_payload_len, max_len);
}

static void set_pmtu_discover(int fd, bool is_ipv4)
{
	int level, name, val;

	if (is_ipv4) {
		level	= SOL_IP;
		name	= IP_MTU_DISCOVER;
		val	= IP_PMTUDISC_DO;
	} else {
		level	= SOL_IPV6;
		name	= IPV6_MTU_DISCOVER;
		val	= IPV6_PMTUDISC_DO;
	}

	if (setsockopt(fd, level, name, &val, sizeof(val)))
		error(1, errno, "setsockopt path mtu");
}

int main(int argc, char **argv)
{
	unsigned long num_msgs, num_sends;
	unsigned long tnow, treport, tstop;
	int fd, i, val;

	parse_opts(argc, argv);

	if (cfg_cpu > 0)
		set_cpu(cfg_cpu);

	for (i = 0; i < sizeof(buf[0]); i++)
		buf[0][i] = 'a' + (i % 26);
	for (i = 1; i < NUM_PKT; i++)
		memcpy(buf[i], buf[0], sizeof(buf[0]));

	signal(SIGINT, sigint_handler);

	fd = socket(cfg_family, cfg_tcp ? SOCK_STREAM : SOCK_DGRAM, 0);
	if (fd == -1)
		error(1, errno, "socket");

	if (cfg_zerocopy) {
		val = 1;
		if (setsockopt(fd, SOL_SOCKET, SO_ZEROCOPY, &val, sizeof(val)))
			error(1, errno, "setsockopt zerocopy");
	}

	if (cfg_connected &&
	    connect(fd, (void *)&cfg_dst_addr, cfg_alen))
		error(1, errno, "connect");

	if (cfg_segment)
		set_pmtu_discover(fd, cfg_family == PF_INET);

	num_msgs = num_sends = 0;
	tnow = gettimeofday_ms();
	tstop = tnow + cfg_runtime_ms;
	treport = tnow + 1000;

	i = 0;
	do {
		if (cfg_tcp)
			num_sends += send_tcp(fd, buf[i]);
		else if (cfg_segment)
			num_sends += send_udp_segment(fd, buf[i]);
		else if (cfg_sendmmsg)
			num_sends += send_udp_sendmmsg(fd, buf[i]);
		else
			num_sends += send_udp(fd, buf[i]);
		num_msgs++;
		if (cfg_zerocopy && ((num_msgs & 0xF) == 0))
			flush_zerocopy(fd);

		if (cfg_msg_nr && num_msgs >= cfg_msg_nr)
			break;

		tnow = gettimeofday_ms();
		if (tnow > treport) {
			fprintf(stderr,
				"%s tx: %6lu MB/s %8lu calls/s %6lu msg/s\n",
				cfg_tcp ? "tcp" : "udp",
				(num_msgs * cfg_payload_len) >> 20,
				num_sends, num_msgs);
			num_msgs = num_sends = 0;
			treport = tnow + 1000;
		}

		/* cold cache when writing buffer */
		if (cfg_cache_trash)
			i = ++i < NUM_PKT ? i : 0;

	} while (!interrupted && (cfg_runtime_ms == -1 || tnow < tstop));

	if (close(fd))
		error(1, errno, "close");

	return 0;
}
