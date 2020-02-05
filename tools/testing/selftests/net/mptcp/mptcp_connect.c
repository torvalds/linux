// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE

#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>

#include <sys/poll.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/mman.h>

#include <netdb.h>
#include <netinet/in.h>

#include <linux/tcp.h>

extern int optind;

#ifndef IPPROTO_MPTCP
#define IPPROTO_MPTCP 262
#endif
#ifndef TCP_ULP
#define TCP_ULP 31
#endif

static bool listen_mode;
static int  poll_timeout;

enum cfg_mode {
	CFG_MODE_POLL,
	CFG_MODE_MMAP,
	CFG_MODE_SENDFILE,
};

static enum cfg_mode cfg_mode = CFG_MODE_POLL;
static const char *cfg_host;
static const char *cfg_port	= "12000";
static int cfg_sock_proto	= IPPROTO_MPTCP;
static bool tcpulp_audit;
static int pf = AF_INET;
static int cfg_sndbuf;

static void die_usage(void)
{
	fprintf(stderr, "Usage: mptcp_connect [-6] [-u] [-s MPTCP|TCP] [-p port] -m mode]"
		"[ -l ] [ -t timeout ] connect_address\n");
	exit(1);
}

static const char *getxinfo_strerr(int err)
{
	if (err == EAI_SYSTEM)
		return strerror(errno);

	return gai_strerror(err);
}

static void xgetnameinfo(const struct sockaddr *addr, socklen_t addrlen,
			 char *host, socklen_t hostlen,
			 char *serv, socklen_t servlen)
{
	int flags = NI_NUMERICHOST | NI_NUMERICSERV;
	int err = getnameinfo(addr, addrlen, host, hostlen, serv, servlen,
			      flags);

	if (err) {
		const char *errstr = getxinfo_strerr(err);

		fprintf(stderr, "Fatal: getnameinfo: %s\n", errstr);
		exit(1);
	}
}

static void xgetaddrinfo(const char *node, const char *service,
			 const struct addrinfo *hints,
			 struct addrinfo **res)
{
	int err = getaddrinfo(node, service, hints, res);

	if (err) {
		const char *errstr = getxinfo_strerr(err);

		fprintf(stderr, "Fatal: getaddrinfo(%s:%s): %s\n",
			node ? node : "", service ? service : "", errstr);
		exit(1);
	}
}

static void set_sndbuf(int fd, unsigned int size)
{
	int err;

	err = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
	if (err) {
		perror("set SO_SNDBUF");
		exit(1);
	}
}

static int sock_listen_mptcp(const char * const listenaddr,
			     const char * const port)
{
	int sock;
	struct addrinfo hints = {
		.ai_protocol = IPPROTO_TCP,
		.ai_socktype = SOCK_STREAM,
		.ai_flags = AI_PASSIVE | AI_NUMERICHOST
	};

	hints.ai_family = pf;

	struct addrinfo *a, *addr;
	int one = 1;

	xgetaddrinfo(listenaddr, port, &hints, &addr);
	hints.ai_family = pf;

	for (a = addr; a; a = a->ai_next) {
		sock = socket(a->ai_family, a->ai_socktype, cfg_sock_proto);
		if (sock < 0)
			continue;

		if (-1 == setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one,
				     sizeof(one)))
			perror("setsockopt");

		if (bind(sock, a->ai_addr, a->ai_addrlen) == 0)
			break; /* success */

		perror("bind");
		close(sock);
		sock = -1;
	}

	freeaddrinfo(addr);

	if (sock < 0) {
		fprintf(stderr, "Could not create listen socket\n");
		return sock;
	}

	if (listen(sock, 20)) {
		perror("listen");
		close(sock);
		return -1;
	}

	return sock;
}

static bool sock_test_tcpulp(const char * const remoteaddr,
			     const char * const port)
{
	struct addrinfo hints = {
		.ai_protocol = IPPROTO_TCP,
		.ai_socktype = SOCK_STREAM,
	};
	struct addrinfo *a, *addr;
	int sock = -1, ret = 0;
	bool test_pass = false;

	hints.ai_family = AF_INET;

	xgetaddrinfo(remoteaddr, port, &hints, &addr);
	for (a = addr; a; a = a->ai_next) {
		sock = socket(a->ai_family, a->ai_socktype, IPPROTO_TCP);
		if (sock < 0) {
			perror("socket");
			continue;
		}
		ret = setsockopt(sock, IPPROTO_TCP, TCP_ULP, "mptcp",
				 sizeof("mptcp"));
		if (ret == -1 && errno == EOPNOTSUPP)
			test_pass = true;
		close(sock);

		if (test_pass)
			break;
		if (!ret)
			fprintf(stderr,
				"setsockopt(TCP_ULP) returned 0\n");
		else
			perror("setsockopt(TCP_ULP)");
	}
	return test_pass;
}

static int sock_connect_mptcp(const char * const remoteaddr,
			      const char * const port, int proto)
{
	struct addrinfo hints = {
		.ai_protocol = IPPROTO_TCP,
		.ai_socktype = SOCK_STREAM,
	};
	struct addrinfo *a, *addr;
	int sock = -1;

	hints.ai_family = pf;

	xgetaddrinfo(remoteaddr, port, &hints, &addr);
	for (a = addr; a; a = a->ai_next) {
		sock = socket(a->ai_family, a->ai_socktype, proto);
		if (sock < 0) {
			perror("socket");
			continue;
		}

		if (connect(sock, a->ai_addr, a->ai_addrlen) == 0)
			break; /* success */

		perror("connect()");
		close(sock);
		sock = -1;
	}

	freeaddrinfo(addr);
	return sock;
}

static size_t do_rnd_write(const int fd, char *buf, const size_t len)
{
	unsigned int do_w;
	ssize_t bw;

	do_w = rand() & 0xffff;
	if (do_w == 0 || do_w > len)
		do_w = len;

	bw = write(fd, buf, do_w);
	if (bw < 0)
		perror("write");

	return bw;
}

static size_t do_write(const int fd, char *buf, const size_t len)
{
	size_t offset = 0;

	while (offset < len) {
		size_t written;
		ssize_t bw;

		bw = write(fd, buf + offset, len - offset);
		if (bw < 0) {
			perror("write");
			return 0;
		}

		written = (size_t)bw;
		offset += written;
	}

	return offset;
}

static ssize_t do_rnd_read(const int fd, char *buf, const size_t len)
{
	size_t cap = rand();

	cap &= 0xffff;

	if (cap == 0)
		cap = 1;
	else if (cap > len)
		cap = len;

	return read(fd, buf, cap);
}

static void set_nonblock(int fd)
{
	int flags = fcntl(fd, F_GETFL);

	if (flags == -1)
		return;

	fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int copyfd_io_poll(int infd, int peerfd, int outfd)
{
	struct pollfd fds = {
		.fd = peerfd,
		.events = POLLIN | POLLOUT,
	};
	unsigned int woff = 0, wlen = 0;
	char wbuf[8192];

	set_nonblock(peerfd);

	for (;;) {
		char rbuf[8192];
		ssize_t len;

		if (fds.events == 0)
			break;

		switch (poll(&fds, 1, poll_timeout)) {
		case -1:
			if (errno == EINTR)
				continue;
			perror("poll");
			return 1;
		case 0:
			fprintf(stderr, "%s: poll timed out (events: "
				"POLLIN %u, POLLOUT %u)\n", __func__,
				fds.events & POLLIN, fds.events & POLLOUT);
			return 2;
		}

		if (fds.revents & POLLIN) {
			len = do_rnd_read(peerfd, rbuf, sizeof(rbuf));
			if (len == 0) {
				/* no more data to receive:
				 * peer has closed its write side
				 */
				fds.events &= ~POLLIN;

				if ((fds.events & POLLOUT) == 0)
					/* and nothing more to send */
					break;

			/* Else, still have data to transmit */
			} else if (len < 0) {
				perror("read");
				return 3;
			}

			do_write(outfd, rbuf, len);
		}

		if (fds.revents & POLLOUT) {
			if (wlen == 0) {
				woff = 0;
				wlen = read(infd, wbuf, sizeof(wbuf));
			}

			if (wlen > 0) {
				ssize_t bw;

				bw = do_rnd_write(peerfd, wbuf + woff, wlen);
				if (bw < 0)
					return 111;

				woff += bw;
				wlen -= bw;
			} else if (wlen == 0) {
				/* We have no more data to send. */
				fds.events &= ~POLLOUT;

				if ((fds.events & POLLIN) == 0)
					/* ... and peer also closed already */
					break;

				/* ... but we still receive.
				 * Close our write side.
				 */
				shutdown(peerfd, SHUT_WR);
			} else {
				if (errno == EINTR)
					continue;
				perror("read");
				return 4;
			}
		}

		if (fds.revents & (POLLERR | POLLNVAL)) {
			fprintf(stderr, "Unexpected revents: "
				"POLLERR/POLLNVAL(%x)\n", fds.revents);
			return 5;
		}
	}

	close(peerfd);
	return 0;
}

static int do_recvfile(int infd, int outfd)
{
	ssize_t r;

	do {
		char buf[16384];

		r = do_rnd_read(infd, buf, sizeof(buf));
		if (r > 0) {
			if (write(outfd, buf, r) != r)
				break;
		} else if (r < 0) {
			perror("read");
		}
	} while (r > 0);

	return (int)r;
}

static int do_mmap(int infd, int outfd, unsigned int size)
{
	char *inbuf = mmap(NULL, size, PROT_READ, MAP_SHARED, infd, 0);
	ssize_t ret = 0, off = 0;
	size_t rem;

	if (inbuf == MAP_FAILED) {
		perror("mmap");
		return 1;
	}

	rem = size;

	while (rem > 0) {
		ret = write(outfd, inbuf + off, rem);

		if (ret < 0) {
			perror("write");
			break;
		}

		off += ret;
		rem -= ret;
	}

	munmap(inbuf, size);
	return rem;
}

static int get_infd_size(int fd)
{
	struct stat sb;
	ssize_t count;
	int err;

	err = fstat(fd, &sb);
	if (err < 0) {
		perror("fstat");
		return -1;
	}

	if ((sb.st_mode & S_IFMT) != S_IFREG) {
		fprintf(stderr, "%s: stdin is not a regular file\n", __func__);
		return -2;
	}

	count = sb.st_size;
	if (count > INT_MAX) {
		fprintf(stderr, "File too large: %zu\n", count);
		return -3;
	}

	return (int)count;
}

static int do_sendfile(int infd, int outfd, unsigned int count)
{
	while (count > 0) {
		ssize_t r;

		r = sendfile(outfd, infd, NULL, count);
		if (r < 0) {
			perror("sendfile");
			return 3;
		}

		count -= r;
	}

	return 0;
}

static int copyfd_io_mmap(int infd, int peerfd, int outfd,
			  unsigned int size)
{
	int err;

	if (listen_mode) {
		err = do_recvfile(peerfd, outfd);
		if (err)
			return err;

		err = do_mmap(infd, peerfd, size);
	} else {
		err = do_mmap(infd, peerfd, size);
		if (err)
			return err;

		shutdown(peerfd, SHUT_WR);

		err = do_recvfile(peerfd, outfd);
	}

	return err;
}

static int copyfd_io_sendfile(int infd, int peerfd, int outfd,
			      unsigned int size)
{
	int err;

	if (listen_mode) {
		err = do_recvfile(peerfd, outfd);
		if (err)
			return err;

		err = do_sendfile(infd, peerfd, size);
	} else {
		err = do_sendfile(infd, peerfd, size);
		if (err)
			return err;
		err = do_recvfile(peerfd, outfd);
	}

	return err;
}

static int copyfd_io(int infd, int peerfd, int outfd)
{
	int file_size;

	switch (cfg_mode) {
	case CFG_MODE_POLL:
		return copyfd_io_poll(infd, peerfd, outfd);
	case CFG_MODE_MMAP:
		file_size = get_infd_size(infd);
		if (file_size < 0)
			return file_size;
		return copyfd_io_mmap(infd, peerfd, outfd, file_size);
	case CFG_MODE_SENDFILE:
		file_size = get_infd_size(infd);
		if (file_size < 0)
			return file_size;
		return copyfd_io_sendfile(infd, peerfd, outfd, file_size);
	}

	fprintf(stderr, "Invalid mode %d\n", cfg_mode);

	die_usage();
	return 1;
}

static void check_sockaddr(int pf, struct sockaddr_storage *ss,
			   socklen_t salen)
{
	struct sockaddr_in6 *sin6;
	struct sockaddr_in *sin;
	socklen_t wanted_size = 0;

	switch (pf) {
	case AF_INET:
		wanted_size = sizeof(*sin);
		sin = (void *)ss;
		if (!sin->sin_port)
			fprintf(stderr, "accept: something wrong: ip connection from port 0");
		break;
	case AF_INET6:
		wanted_size = sizeof(*sin6);
		sin6 = (void *)ss;
		if (!sin6->sin6_port)
			fprintf(stderr, "accept: something wrong: ipv6 connection from port 0");
		break;
	default:
		fprintf(stderr, "accept: Unknown pf %d, salen %u\n", pf, salen);
		return;
	}

	if (salen != wanted_size)
		fprintf(stderr, "accept: size mismatch, got %d expected %d\n",
			(int)salen, wanted_size);

	if (ss->ss_family != pf)
		fprintf(stderr, "accept: pf mismatch, expect %d, ss_family is %d\n",
			(int)ss->ss_family, pf);
}

static void check_getpeername(int fd, struct sockaddr_storage *ss, socklen_t salen)
{
	struct sockaddr_storage peerss;
	socklen_t peersalen = sizeof(peerss);

	if (getpeername(fd, (struct sockaddr *)&peerss, &peersalen) < 0) {
		perror("getpeername");
		return;
	}

	if (peersalen != salen) {
		fprintf(stderr, "%s: %d vs %d\n", __func__, peersalen, salen);
		return;
	}

	if (memcmp(ss, &peerss, peersalen)) {
		char a[INET6_ADDRSTRLEN];
		char b[INET6_ADDRSTRLEN];
		char c[INET6_ADDRSTRLEN];
		char d[INET6_ADDRSTRLEN];

		xgetnameinfo((struct sockaddr *)ss, salen,
			     a, sizeof(a), b, sizeof(b));

		xgetnameinfo((struct sockaddr *)&peerss, peersalen,
			     c, sizeof(c), d, sizeof(d));

		fprintf(stderr, "%s: memcmp failure: accept %s vs peername %s, %s vs %s salen %d vs %d\n",
			__func__, a, c, b, d, peersalen, salen);
	}
}

static void check_getpeername_connect(int fd)
{
	struct sockaddr_storage ss;
	socklen_t salen = sizeof(ss);
	char a[INET6_ADDRSTRLEN];
	char b[INET6_ADDRSTRLEN];

	if (getpeername(fd, (struct sockaddr *)&ss, &salen) < 0) {
		perror("getpeername");
		return;
	}

	xgetnameinfo((struct sockaddr *)&ss, salen,
		     a, sizeof(a), b, sizeof(b));

	if (strcmp(cfg_host, a) || strcmp(cfg_port, b))
		fprintf(stderr, "%s: %s vs %s, %s vs %s\n", __func__,
			cfg_host, a, cfg_port, b);
}

int main_loop_s(int listensock)
{
	struct sockaddr_storage ss;
	struct pollfd polls;
	socklen_t salen;
	int remotesock;

	polls.fd = listensock;
	polls.events = POLLIN;

	switch (poll(&polls, 1, poll_timeout)) {
	case -1:
		perror("poll");
		return 1;
	case 0:
		fprintf(stderr, "%s: timed out\n", __func__);
		close(listensock);
		return 2;
	}

	salen = sizeof(ss);
	remotesock = accept(listensock, (struct sockaddr *)&ss, &salen);
	if (remotesock >= 0) {
		check_sockaddr(pf, &ss, salen);
		check_getpeername(remotesock, &ss, salen);

		return copyfd_io(0, remotesock, 1);
	}

	perror("accept");

	return 1;
}

static void init_rng(void)
{
	int fd = open("/dev/urandom", O_RDONLY);
	unsigned int foo;

	if (fd > 0) {
		int ret = read(fd, &foo, sizeof(foo));

		if (ret < 0)
			srand(fd + foo);
		close(fd);
	}

	srand(foo);
}

int main_loop(void)
{
	int fd;

	/* listener is ready. */
	fd = sock_connect_mptcp(cfg_host, cfg_port, cfg_sock_proto);
	if (fd < 0)
		return 2;

	check_getpeername_connect(fd);

	if (cfg_sndbuf)
		set_sndbuf(fd, cfg_sndbuf);

	return copyfd_io(0, fd, 1);
}

int parse_proto(const char *proto)
{
	if (!strcasecmp(proto, "MPTCP"))
		return IPPROTO_MPTCP;
	if (!strcasecmp(proto, "TCP"))
		return IPPROTO_TCP;

	fprintf(stderr, "Unknown protocol: %s\n.", proto);
	die_usage();

	/* silence compiler warning */
	return 0;
}

int parse_mode(const char *mode)
{
	if (!strcasecmp(mode, "poll"))
		return CFG_MODE_POLL;
	if (!strcasecmp(mode, "mmap"))
		return CFG_MODE_MMAP;
	if (!strcasecmp(mode, "sendfile"))
		return CFG_MODE_SENDFILE;

	fprintf(stderr, "Unknown test mode: %s\n", mode);
	fprintf(stderr, "Supported modes are:\n");
	fprintf(stderr, "\t\t\"poll\" - interleaved read/write using poll()\n");
	fprintf(stderr, "\t\t\"mmap\" - send entire input file (mmap+write), then read response (-l will read input first)\n");
	fprintf(stderr, "\t\t\"sendfile\" - send entire input file (sendfile), then read response (-l will read input first)\n");

	die_usage();

	/* silence compiler warning */
	return 0;
}

int parse_sndbuf(const char *size)
{
	unsigned long s;

	errno = 0;

	s = strtoul(size, NULL, 0);

	if (errno) {
		fprintf(stderr, "Invalid sndbuf size %s (%s)\n",
			size, strerror(errno));
		die_usage();
	}

	if (s > INT_MAX) {
		fprintf(stderr, "Invalid sndbuf size %s (%s)\n",
			size, strerror(ERANGE));
		die_usage();
	}

	cfg_sndbuf = s;

	return 0;
}

static void parse_opts(int argc, char **argv)
{
	int c;

	while ((c = getopt(argc, argv, "6lp:s:hut:m:b:")) != -1) {
		switch (c) {
		case 'l':
			listen_mode = true;
			break;
		case 'p':
			cfg_port = optarg;
			break;
		case 's':
			cfg_sock_proto = parse_proto(optarg);
			break;
		case 'h':
			die_usage();
			break;
		case 'u':
			tcpulp_audit = true;
			break;
		case '6':
			pf = AF_INET6;
			break;
		case 't':
			poll_timeout = atoi(optarg) * 1000;
			if (poll_timeout <= 0)
				poll_timeout = -1;
			break;
		case 'm':
			cfg_mode = parse_mode(optarg);
			break;
		case 'b':
			cfg_sndbuf = parse_sndbuf(optarg);
			break;
		}
	}

	if (optind + 1 != argc)
		die_usage();
	cfg_host = argv[optind];

	if (strchr(cfg_host, ':'))
		pf = AF_INET6;
}

int main(int argc, char *argv[])
{
	init_rng();

	parse_opts(argc, argv);

	if (tcpulp_audit)
		return sock_test_tcpulp(cfg_host, cfg_port) ? 0 : 1;

	if (listen_mode) {
		int fd = sock_listen_mptcp(cfg_host, cfg_port);

		if (fd < 0)
			return 1;

		if (cfg_sndbuf)
			set_sndbuf(fd, cfg_sndbuf);

		return main_loop_s(fd);
	}

	return main_loop();
}
