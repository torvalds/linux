// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <error.h>
#include <errno.h>
#include <limits.h>
#include <linux/errqueue.h>
#include <linux/if_packet.h>
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

#ifndef UDP_GRO
#define UDP_GRO		104
#endif

static int  cfg_port		= 8000;
static bool cfg_tcp;
static bool cfg_verify;
static bool cfg_read_all;
static bool cfg_gro_segment;
static int  cfg_family		= PF_INET6;
static int  cfg_alen 		= sizeof(struct sockaddr_in6);
static int  cfg_expected_pkt_nr;
static int  cfg_expected_pkt_len;
static int  cfg_expected_gso_size;
static int  cfg_connect_timeout_ms;
static int  cfg_rcv_timeout_ms;
static struct sockaddr_storage cfg_bind_addr;

static bool interrupted;
static unsigned long packets, bytes;

static void sigint_handler(int signum)
{
	if (signum == SIGINT)
		interrupted = true;
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

static unsigned long gettimeofday_ms(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}

static void do_poll(int fd, int timeout_ms)
{
	struct pollfd pfd;
	int ret;

	pfd.events = POLLIN;
	pfd.revents = 0;
	pfd.fd = fd;

	do {
		ret = poll(&pfd, 1, 10);
		if (interrupted)
			break;
		if (ret == -1)
			error(1, errno, "poll");
		if (ret == 0) {
			if (!timeout_ms)
				continue;

			timeout_ms -= 10;
			if (timeout_ms <= 0) {
				interrupted = true;
				break;
			}

			/* no events and more time to wait, do poll again */
			continue;
		}
		if (pfd.revents != POLLIN)
			error(1, errno, "poll: 0x%x expected 0x%x\n",
					pfd.revents, POLLIN);
	} while (!ret);
}

static int do_socket(bool do_tcp)
{
	int fd, val;

	fd = socket(cfg_family, cfg_tcp ? SOCK_STREAM : SOCK_DGRAM, 0);
	if (fd == -1)
		error(1, errno, "socket");

	val = 1 << 21;
	if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &val, sizeof(val)))
		error(1, errno, "setsockopt rcvbuf");
	val = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &val, sizeof(val)))
		error(1, errno, "setsockopt reuseport");

	if (bind(fd, (void *)&cfg_bind_addr, cfg_alen))
		error(1, errno, "bind");

	if (do_tcp) {
		int accept_fd = fd;

		if (listen(accept_fd, 1))
			error(1, errno, "listen");

		do_poll(accept_fd, cfg_connect_timeout_ms);
		if (interrupted)
			exit(0);

		fd = accept(accept_fd, NULL, NULL);
		if (fd == -1)
			error(1, errno, "accept");
		if (close(accept_fd))
			error(1, errno, "close accept fd");
	}

	return fd;
}

/* Flush all outstanding bytes for the tcp receive queue */
static void do_flush_tcp(int fd)
{
	int ret;

	while (true) {
		/* MSG_TRUNC flushes up to len bytes */
		ret = recv(fd, NULL, 1 << 21, MSG_TRUNC | MSG_DONTWAIT);
		if (ret == -1 && errno == EAGAIN)
			return;
		if (ret == -1)
			error(1, errno, "flush");
		if (ret == 0) {
			/* client detached */
			exit(0);
		}

		packets++;
		bytes += ret;
	}

}

static char sanitized_char(char val)
{
	return (val >= 'a' && val <= 'z') ? val : '.';
}

static void do_verify_udp(const char *data, int len)
{
	char cur = data[0];
	int i;

	/* verify contents */
	if (cur < 'a' || cur > 'z')
		error(1, 0, "data initial byte out of range");

	for (i = 1; i < len; i++) {
		if (cur == 'z')
			cur = 'a';
		else
			cur++;

		if (data[i] != cur)
			error(1, 0, "data[%d]: len %d, %c(%hhu) != %c(%hhu)\n",
			      i, len,
			      sanitized_char(data[i]), data[i],
			      sanitized_char(cur), cur);
	}
}

static int recv_msg(int fd, char *buf, int len, int *gso_size)
{
	char control[CMSG_SPACE(sizeof(int))] = {0};
	struct msghdr msg = {0};
	struct iovec iov = {0};
	struct cmsghdr *cmsg;
	int ret;

	iov.iov_base = buf;
	iov.iov_len = len;

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	msg.msg_control = control;
	msg.msg_controllen = sizeof(control);

	*gso_size = -1;
	ret = recvmsg(fd, &msg, MSG_TRUNC | MSG_DONTWAIT);
	if (ret != -1) {
		for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL;
		     cmsg = CMSG_NXTHDR(&msg, cmsg)) {
			if (cmsg->cmsg_level == SOL_UDP
			    && cmsg->cmsg_type == UDP_GRO) {
				*gso_size = *(int *)CMSG_DATA(cmsg);
				break;
			}
		}
	}
	return ret;
}

/* Flush all outstanding datagrams. Verify first few bytes of each. */
static void do_flush_udp(int fd)
{
	static char rbuf[ETH_MAX_MTU];
	int ret, len, gso_size = 0, budget = 256;

	len = cfg_read_all ? sizeof(rbuf) : 0;
	while (budget--) {
		/* MSG_TRUNC will make return value full datagram length */
		if (!cfg_expected_gso_size)
			ret = recv(fd, rbuf, len, MSG_TRUNC | MSG_DONTWAIT);
		else
			ret = recv_msg(fd, rbuf, len, &gso_size);
		if (ret == -1 && errno == EAGAIN)
			break;
		if (ret == -1)
			error(1, errno, "recv");
		if (cfg_expected_pkt_len && ret != cfg_expected_pkt_len)
			error(1, 0, "recv: bad packet len, got %d,"
			      " expected %d\n", ret, cfg_expected_pkt_len);
		if (len && cfg_verify) {
			if (ret == 0)
				error(1, errno, "recv: 0 byte datagram\n");

			do_verify_udp(rbuf, ret);
		}
		if (cfg_expected_gso_size && cfg_expected_gso_size != gso_size)
			error(1, 0, "recv: bad gso size, got %d, expected %d "
			      "(-1 == no gso cmsg))\n", gso_size,
			      cfg_expected_gso_size);

		packets++;
		bytes += ret;
		if (cfg_expected_pkt_nr && packets >= cfg_expected_pkt_nr)
			break;
	}
}

static void usage(const char *filepath)
{
	error(1, 0, "Usage: %s [-C connect_timeout] [-Grtv] [-b addr] [-p port]"
	      " [-l pktlen] [-n packetnr] [-R rcv_timeout] [-S gsosize]",
	      filepath);
}

static void parse_opts(int argc, char **argv)
{
	const char *bind_addr = NULL;
	int c;

	while ((c = getopt(argc, argv, "4b:C:Gl:n:p:rR:S:tv")) != -1) {
		switch (c) {
		case '4':
			cfg_family = PF_INET;
			cfg_alen = sizeof(struct sockaddr_in);
			break;
		case 'b':
			bind_addr = optarg;
			break;
		case 'C':
			cfg_connect_timeout_ms = strtoul(optarg, NULL, 0);
			break;
		case 'G':
			cfg_gro_segment = true;
			break;
		case 'l':
			cfg_expected_pkt_len = strtoul(optarg, NULL, 0);
			break;
		case 'n':
			cfg_expected_pkt_nr = strtoul(optarg, NULL, 0);
			break;
		case 'p':
			cfg_port = strtoul(optarg, NULL, 0);
			break;
		case 'r':
			cfg_read_all = true;
			break;
		case 'R':
			cfg_rcv_timeout_ms = strtoul(optarg, NULL, 0);
			break;
		case 'S':
			cfg_expected_gso_size = strtol(optarg, NULL, 0);
			break;
		case 't':
			cfg_tcp = true;
			break;
		case 'v':
			cfg_verify = true;
			cfg_read_all = true;
			break;
		default:
			exit(1);
		}
	}

	if (!bind_addr)
		bind_addr = cfg_family == PF_INET6 ? "::" : "0.0.0.0";

	setup_sockaddr(cfg_family, bind_addr, &cfg_bind_addr);

	if (optind != argc)
		usage(argv[0]);

	if (cfg_tcp && cfg_verify)
		error(1, 0, "TODO: implement verify mode for tcp");
}

static void do_recv(void)
{
	int timeout_ms = cfg_tcp ? cfg_rcv_timeout_ms : cfg_connect_timeout_ms;
	unsigned long tnow, treport;
	int fd;

	fd = do_socket(cfg_tcp);

	if (cfg_gro_segment && !cfg_tcp) {
		int val = 1;
		if (setsockopt(fd, IPPROTO_UDP, UDP_GRO, &val, sizeof(val)))
			error(1, errno, "setsockopt UDP_GRO");
	}

	treport = gettimeofday_ms() + 1000;
	do {
		do_poll(fd, timeout_ms);

		if (cfg_tcp)
			do_flush_tcp(fd);
		else
			do_flush_udp(fd);

		tnow = gettimeofday_ms();
		if (!cfg_expected_pkt_nr && tnow > treport) {
			if (packets)
				fprintf(stderr,
					"%s rx: %6lu MB/s %8lu calls/s\n",
					cfg_tcp ? "tcp" : "udp",
					bytes >> 20, packets);
			bytes = packets = 0;
			treport = tnow + 1000;
		}

		timeout_ms = cfg_rcv_timeout_ms;

	} while (!interrupted);

	if (cfg_expected_pkt_nr && (packets != cfg_expected_pkt_nr))
		error(1, 0, "wrong packet number! got %ld, expected %d\n",
		      packets, cfg_expected_pkt_nr);

	if (close(fd))
		error(1, errno, "close");
}

int main(int argc, char **argv)
{
	parse_opts(argc, argv);

	signal(SIGINT, sigint_handler);

	do_recv();

	return 0;
}
