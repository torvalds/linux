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

static int  cfg_port		= 8000;
static bool cfg_tcp;
static bool cfg_verify;

static bool interrupted;
static unsigned long packets, bytes;

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

static void do_poll(int fd)
{
	struct pollfd pfd;
	int ret;

	pfd.events = POLLIN;
	pfd.revents = 0;
	pfd.fd = fd;

	do {
		ret = poll(&pfd, 1, 10);
		if (ret == -1)
			error(1, errno, "poll");
		if (ret == 0)
			continue;
		if (pfd.revents != POLLIN)
			error(1, errno, "poll: 0x%x expected 0x%x\n",
					pfd.revents, POLLIN);
	} while (!ret && !interrupted);
}

static int do_socket(bool do_tcp)
{
	struct sockaddr_in6 addr = {0};
	int fd, val;

	fd = socket(PF_INET6, cfg_tcp ? SOCK_STREAM : SOCK_DGRAM, 0);
	if (fd == -1)
		error(1, errno, "socket");

	val = 1 << 21;
	if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &val, sizeof(val)))
		error(1, errno, "setsockopt rcvbuf");
	val = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &val, sizeof(val)))
		error(1, errno, "setsockopt reuseport");

	addr.sin6_family =	PF_INET6;
	addr.sin6_port =	htons(cfg_port);
	addr.sin6_addr =	in6addr_any;
	if (bind(fd, (void *) &addr, sizeof(addr)))
		error(1, errno, "bind");

	if (do_tcp) {
		int accept_fd = fd;

		if (listen(accept_fd, 1))
			error(1, errno, "listen");

		do_poll(accept_fd);

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

/* Flush all outstanding datagrams. Verify first few bytes of each. */
static void do_flush_udp(int fd)
{
	static char rbuf[ETH_DATA_LEN];
	int ret, len, budget = 256;

	len = cfg_verify ? sizeof(rbuf) : 0;
	while (budget--) {
		/* MSG_TRUNC will make return value full datagram length */
		ret = recv(fd, rbuf, len, MSG_TRUNC | MSG_DONTWAIT);
		if (ret == -1 && errno == EAGAIN)
			return;
		if (ret == -1)
			error(1, errno, "recv");
		if (len) {
			if (ret == 0)
				error(1, errno, "recv: 0 byte datagram\n");

			do_verify_udp(rbuf, ret);
		}

		packets++;
		bytes += ret;
	}
}

static void usage(const char *filepath)
{
	error(1, 0, "Usage: %s [-tv] [-p port]", filepath);
}

static void parse_opts(int argc, char **argv)
{
	int c;

	while ((c = getopt(argc, argv, "ptv")) != -1) {
		switch (c) {
		case 'p':
			cfg_port = htons(strtoul(optarg, NULL, 0));
			break;
		case 't':
			cfg_tcp = true;
			break;
		case 'v':
			cfg_verify = true;
			break;
		}
	}

	if (optind != argc)
		usage(argv[0]);

	if (cfg_tcp && cfg_verify)
		error(1, 0, "TODO: implement verify mode for tcp");
}

static void do_recv(void)
{
	unsigned long tnow, treport;
	int fd;

	fd = do_socket(cfg_tcp);

	treport = gettimeofday_ms() + 1000;
	do {
		do_poll(fd);

		if (cfg_tcp)
			do_flush_tcp(fd);
		else
			do_flush_udp(fd);

		tnow = gettimeofday_ms();
		if (tnow > treport) {
			if (packets)
				fprintf(stderr,
					"%s rx: %6lu MB/s %8lu calls/s\n",
					cfg_tcp ? "tcp" : "udp",
					bytes >> 20, packets);
			bytes = packets = 0;
			treport = tnow + 1000;
		}

	} while (!interrupted);

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
