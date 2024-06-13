// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE

#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/random.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/mman.h>

#include <netdb.h>
#include <netinet/in.h>

#include <linux/tcp.h>
#include <linux/time_types.h>
#include <linux/sockios.h>

extern int optind;

#ifndef IPPROTO_MPTCP
#define IPPROTO_MPTCP 262
#endif
#ifndef TCP_ULP
#define TCP_ULP 31
#endif

static int  poll_timeout = 10 * 1000;
static bool listen_mode;
static bool quit;

enum cfg_mode {
	CFG_MODE_POLL,
	CFG_MODE_MMAP,
	CFG_MODE_SENDFILE,
};

enum cfg_peek {
	CFG_NONE_PEEK,
	CFG_WITH_PEEK,
	CFG_AFTER_PEEK,
};

static enum cfg_mode cfg_mode = CFG_MODE_POLL;
static enum cfg_peek cfg_peek = CFG_NONE_PEEK;
static const char *cfg_host;
static const char *cfg_port	= "12000";
static int cfg_sock_proto	= IPPROTO_MPTCP;
static int pf = AF_INET;
static int cfg_sndbuf;
static int cfg_rcvbuf;
static bool cfg_join;
static bool cfg_remove;
static unsigned int cfg_time;
static unsigned int cfg_do_w;
static int cfg_wait;
static uint32_t cfg_mark;
static char *cfg_input;
static int cfg_repeat = 1;
static int cfg_truncate;
static int cfg_rcv_trunc;

struct cfg_cmsg_types {
	unsigned int cmsg_enabled:1;
	unsigned int timestampns:1;
	unsigned int tcp_inq:1;
};

struct cfg_sockopt_types {
	unsigned int transparent:1;
};

struct tcp_inq_state {
	unsigned int last;
	bool expect_eof;
};

static struct tcp_inq_state tcp_inq;

static struct cfg_cmsg_types cfg_cmsg_types;
static struct cfg_sockopt_types cfg_sockopt_types;

static void die_usage(void)
{
	fprintf(stderr, "Usage: mptcp_connect [-6] [-c cmsg] [-f offset] [-i file] [-I num] [-j] [-l] "
		"[-m mode] [-M mark] [-o option] [-p port] [-P mode] [-r num] [-R num] "
		"[-s MPTCP|TCP] [-S num] [-t num] [-T num] [-w sec] connect_address\n");
	fprintf(stderr, "\t-6 use ipv6\n");
	fprintf(stderr, "\t-c cmsg -- test cmsg type <cmsg>\n");
	fprintf(stderr, "\t-f offset -- stop the I/O after receiving and sending the specified amount "
		"of bytes. If there are unread bytes in the receive queue, that will cause a MPTCP "
		"fastclose at close/shutdown. If offset is negative, expect the peer to close before "
		"all the local data as been sent, thus toleration errors on write and EPIPE signals\n");
	fprintf(stderr, "\t-i file -- read the data to send from the given file instead of stdin");
	fprintf(stderr, "\t-I num -- repeat the transfer 'num' times. In listen mode accepts num "
		"incoming connections, in client mode, disconnect and reconnect to the server\n");
	fprintf(stderr, "\t-j     -- add additional sleep at connection start and tear down "
		"-- for MPJ tests\n");
	fprintf(stderr, "\t-l     -- listens mode, accepts incoming connection\n");
	fprintf(stderr, "\t-m [poll|mmap|sendfile] -- use poll(default)/mmap+write/sendfile\n");
	fprintf(stderr, "\t-M mark -- set socket packet mark\n");
	fprintf(stderr, "\t-o option -- test sockopt <option>\n");
	fprintf(stderr, "\t-p num -- use port num\n");
	fprintf(stderr,
		"\t-P [saveWithPeek|saveAfterPeek] -- save data with/after MSG_PEEK form tcp socket\n");
	fprintf(stderr, "\t-r num -- enable slow mode, limiting each write to num bytes "
		"-- for remove addr tests\n");
	fprintf(stderr, "\t-R num -- set SO_RCVBUF to num\n");
	fprintf(stderr, "\t-s [MPTCP|TCP] -- use mptcp(default) or tcp sockets\n");
	fprintf(stderr, "\t-S num -- set SO_SNDBUF to num\n");
	fprintf(stderr, "\t-t num -- set poll timeout to num\n");
	fprintf(stderr, "\t-T num -- set expected runtime to num ms\n");
	fprintf(stderr, "\t-w num -- wait num sec before closing the socket\n");
	exit(1);
}

static void xerror(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(1);
}

static void handle_signal(int nr)
{
	quit = true;
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

static void set_rcvbuf(int fd, unsigned int size)
{
	int err;

	err = setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
	if (err) {
		perror("set SO_RCVBUF");
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

static void set_mark(int fd, uint32_t mark)
{
	int err;

	err = setsockopt(fd, SOL_SOCKET, SO_MARK, &mark, sizeof(mark));
	if (err) {
		perror("set SO_MARK");
		exit(1);
	}
}

static void set_transparent(int fd, int pf)
{
	int one = 1;

	switch (pf) {
	case AF_INET:
		if (-1 == setsockopt(fd, SOL_IP, IP_TRANSPARENT, &one, sizeof(one)))
			perror("IP_TRANSPARENT");
		break;
	case AF_INET6:
		if (-1 == setsockopt(fd, IPPROTO_IPV6, IPV6_TRANSPARENT, &one, sizeof(one)))
			perror("IPV6_TRANSPARENT");
		break;
	}
}

static int do_ulp_so(int sock, const char *name)
{
	return setsockopt(sock, IPPROTO_TCP, TCP_ULP, name, strlen(name));
}

#define X(m)	xerror("%s:%u: %s: failed for proto %d at line %u", __FILE__, __LINE__, (m), proto, line)
static void sock_test_tcpulp(int sock, int proto, unsigned int line)
{
	socklen_t buflen = 8;
	char buf[8] = "";
	int ret = getsockopt(sock, IPPROTO_TCP, TCP_ULP, buf, &buflen);

	if (ret != 0)
		X("getsockopt");

	if (buflen > 0) {
		if (strcmp(buf, "mptcp") != 0)
			xerror("unexpected ULP '%s' for proto %d at line %u", buf, proto, line);
		ret = do_ulp_so(sock, "tls");
		if (ret == 0)
			X("setsockopt");
	} else if (proto == IPPROTO_MPTCP) {
		ret = do_ulp_so(sock, "tls");
		if (ret != -1)
			X("setsockopt");
	}

	ret = do_ulp_so(sock, "mptcp");
	if (ret != -1)
		X("setsockopt");

#undef X
}

#define SOCK_TEST_TCPULP(s, p) sock_test_tcpulp((s), (p), __LINE__)

static int sock_listen_mptcp(const char * const listenaddr,
			     const char * const port)
{
	int sock = -1;
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

		SOCK_TEST_TCPULP(sock, cfg_sock_proto);

		if (-1 == setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one,
				     sizeof(one)))
			perror("setsockopt");

		if (cfg_sockopt_types.transparent)
			set_transparent(sock, pf);

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

	SOCK_TEST_TCPULP(sock, cfg_sock_proto);

	if (listen(sock, 20)) {
		perror("listen");
		close(sock);
		return -1;
	}

	SOCK_TEST_TCPULP(sock, cfg_sock_proto);

	return sock;
}

static int sock_connect_mptcp(const char * const remoteaddr,
			      const char * const port, int proto,
			      struct addrinfo **peer)
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

		SOCK_TEST_TCPULP(sock, proto);

		if (cfg_mark)
			set_mark(sock, cfg_mark);

		if (connect(sock, a->ai_addr, a->ai_addrlen) == 0) {
			*peer = a;
			break; /* success */
		}

		perror("connect()");
		close(sock);
		sock = -1;
	}

	freeaddrinfo(addr);
	if (sock != -1)
		SOCK_TEST_TCPULP(sock, proto);
	return sock;
}

static size_t do_rnd_write(const int fd, char *buf, const size_t len)
{
	static bool first = true;
	unsigned int do_w;
	ssize_t bw;

	do_w = rand() & 0xffff;
	if (do_w == 0 || do_w > len)
		do_w = len;

	if (cfg_join && first && do_w > 100)
		do_w = 100;

	if (cfg_remove && do_w > cfg_do_w)
		do_w = cfg_do_w;

	bw = write(fd, buf, do_w);
	if (bw < 0)
		return bw;

	/* let the join handshake complete, before going on */
	if (cfg_join && first) {
		usleep(200000);
		first = false;
	}

	if (cfg_remove)
		usleep(200000);

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

static void process_cmsg(struct msghdr *msgh)
{
	struct __kernel_timespec ts;
	bool inq_found = false;
	bool ts_found = false;
	unsigned int inq = 0;
	struct cmsghdr *cmsg;

	for (cmsg = CMSG_FIRSTHDR(msgh); cmsg ; cmsg = CMSG_NXTHDR(msgh, cmsg)) {
		if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SO_TIMESTAMPNS_NEW) {
			memcpy(&ts, CMSG_DATA(cmsg), sizeof(ts));
			ts_found = true;
			continue;
		}
		if (cmsg->cmsg_level == IPPROTO_TCP && cmsg->cmsg_type == TCP_CM_INQ) {
			memcpy(&inq, CMSG_DATA(cmsg), sizeof(inq));
			inq_found = true;
			continue;
		}

	}

	if (cfg_cmsg_types.timestampns) {
		if (!ts_found)
			xerror("TIMESTAMPNS not present\n");
	}

	if (cfg_cmsg_types.tcp_inq) {
		if (!inq_found)
			xerror("TCP_INQ not present\n");

		if (inq > 1024)
			xerror("tcp_inq %u is larger than one kbyte\n", inq);
		tcp_inq.last = inq;
	}
}

static ssize_t do_recvmsg_cmsg(const int fd, char *buf, const size_t len)
{
	char msg_buf[8192];
	struct iovec iov = {
		.iov_base = buf,
		.iov_len = len,
	};
	struct msghdr msg = {
		.msg_iov = &iov,
		.msg_iovlen = 1,
		.msg_control = msg_buf,
		.msg_controllen = sizeof(msg_buf),
	};
	int flags = 0;
	unsigned int last_hint = tcp_inq.last;
	int ret = recvmsg(fd, &msg, flags);

	if (ret <= 0) {
		if (ret == 0 && tcp_inq.expect_eof)
			return ret;

		if (ret == 0 && cfg_cmsg_types.tcp_inq)
			if (last_hint != 1 && last_hint != 0)
				xerror("EOF but last tcp_inq hint was %u\n", last_hint);

		return ret;
	}

	if (tcp_inq.expect_eof)
		xerror("expected EOF, last_hint %u, now %u\n",
		       last_hint, tcp_inq.last);

	if (msg.msg_controllen && !cfg_cmsg_types.cmsg_enabled)
		xerror("got %lu bytes of cmsg data, expected 0\n",
		       (unsigned long)msg.msg_controllen);

	if (msg.msg_controllen == 0 && cfg_cmsg_types.cmsg_enabled)
		xerror("%s\n", "got no cmsg data");

	if (msg.msg_controllen)
		process_cmsg(&msg);

	if (cfg_cmsg_types.tcp_inq) {
		if ((size_t)ret < len && last_hint > (unsigned int)ret) {
			if (ret + 1 != (int)last_hint) {
				int next = read(fd, msg_buf, sizeof(msg_buf));

				xerror("read %u of %u, last_hint was %u tcp_inq hint now %u next_read returned %d/%m\n",
				       ret, (unsigned int)len, last_hint, tcp_inq.last, next);
			} else {
				tcp_inq.expect_eof = true;
			}
		}
	}

	return ret;
}

static ssize_t do_rnd_read(const int fd, char *buf, const size_t len)
{
	int ret = 0;
	char tmp[16384];
	size_t cap = rand();

	cap &= 0xffff;

	if (cap == 0)
		cap = 1;
	else if (cap > len)
		cap = len;

	if (cfg_peek == CFG_WITH_PEEK) {
		ret = recv(fd, buf, cap, MSG_PEEK);
		ret = (ret < 0) ? ret : read(fd, tmp, ret);
	} else if (cfg_peek == CFG_AFTER_PEEK) {
		ret = recv(fd, buf, cap, MSG_PEEK);
		ret = (ret < 0) ? ret : read(fd, buf, cap);
	} else if (cfg_cmsg_types.cmsg_enabled) {
		ret = do_recvmsg_cmsg(fd, buf, cap);
	} else {
		ret = read(fd, buf, cap);
	}

	return ret;
}

static void set_nonblock(int fd, bool nonblock)
{
	int flags = fcntl(fd, F_GETFL);

	if (flags == -1)
		return;

	if (nonblock)
		fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	else
		fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
}

static void shut_wr(int fd)
{
	/* Close our write side, ev. give some time
	 * for address notification and/or checking
	 * the current status
	 */
	if (cfg_wait)
		usleep(cfg_wait);

	shutdown(fd, SHUT_WR);
}

static int copyfd_io_poll(int infd, int peerfd, int outfd, bool *in_closed_after_out)
{
	struct pollfd fds = {
		.fd = peerfd,
		.events = POLLIN | POLLOUT,
	};
	unsigned int woff = 0, wlen = 0, total_wlen = 0, total_rlen = 0;
	char wbuf[8192];

	set_nonblock(peerfd, true);

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
			ssize_t rb = sizeof(rbuf);

			/* limit the total amount of read data to the trunc value*/
			if (cfg_truncate > 0) {
				if (rb + total_rlen > cfg_truncate)
					rb = cfg_truncate - total_rlen;
				len = read(peerfd, rbuf, rb);
			} else {
				len = do_rnd_read(peerfd, rbuf, sizeof(rbuf));
			}
			if (len == 0) {
				/* no more data to receive:
				 * peer has closed its write side
				 */
				fds.events &= ~POLLIN;

				if ((fds.events & POLLOUT) == 0) {
					*in_closed_after_out = true;
					/* and nothing more to send */
					break;
				}

			/* Else, still have data to transmit */
			} else if (len < 0) {
				if (cfg_rcv_trunc)
					return 0;
				perror("read");
				return 3;
			}

			total_rlen += len;
			do_write(outfd, rbuf, len);
		}

		if (fds.revents & POLLOUT) {
			if (wlen == 0) {
				woff = 0;
				wlen = read(infd, wbuf, sizeof(wbuf));
			}

			if (wlen > 0) {
				ssize_t bw;

				/* limit the total amount of written data to the trunc value */
				if (cfg_truncate > 0 && wlen + total_wlen > cfg_truncate)
					wlen = cfg_truncate - total_wlen;

				bw = do_rnd_write(peerfd, wbuf + woff, wlen);
				if (bw < 0) {
					if (cfg_rcv_trunc)
						return 0;
					perror("write");
					return 111;
				}

				woff += bw;
				wlen -= bw;
				total_wlen += bw;
			} else if (wlen == 0) {
				/* We have no more data to send. */
				fds.events &= ~POLLOUT;

				if ((fds.events & POLLIN) == 0)
					/* ... and peer also closed already */
					break;

				shut_wr(peerfd);
			} else {
				if (errno == EINTR)
					continue;
				perror("read");
				return 4;
			}
		}

		if (fds.revents & (POLLERR | POLLNVAL)) {
			if (cfg_rcv_trunc)
				return 0;
			fprintf(stderr, "Unexpected revents: "
				"POLLERR/POLLNVAL(%x)\n", fds.revents);
			return 5;
		}

		if (cfg_truncate > 0 && total_wlen >= cfg_truncate &&
		    total_rlen >= cfg_truncate)
			break;
	}

	/* leave some time for late join/announce */
	if (cfg_remove)
		usleep(cfg_wait);

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
			  unsigned int size, bool *in_closed_after_out)
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

		shut_wr(peerfd);

		err = do_recvfile(peerfd, outfd);
		*in_closed_after_out = true;
	}

	return err;
}

static int copyfd_io_sendfile(int infd, int peerfd, int outfd,
			      unsigned int size, bool *in_closed_after_out)
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

		shut_wr(peerfd);

		err = do_recvfile(peerfd, outfd);
		*in_closed_after_out = true;
	}

	return err;
}

static int copyfd_io(int infd, int peerfd, int outfd, bool close_peerfd)
{
	bool in_closed_after_out = false;
	struct timespec start, end;
	int file_size;
	int ret;

	if (cfg_time && (clock_gettime(CLOCK_MONOTONIC, &start) < 0))
		xerror("can not fetch start time %d", errno);

	switch (cfg_mode) {
	case CFG_MODE_POLL:
		ret = copyfd_io_poll(infd, peerfd, outfd, &in_closed_after_out);
		break;

	case CFG_MODE_MMAP:
		file_size = get_infd_size(infd);
		if (file_size < 0)
			return file_size;
		ret = copyfd_io_mmap(infd, peerfd, outfd, file_size, &in_closed_after_out);
		break;

	case CFG_MODE_SENDFILE:
		file_size = get_infd_size(infd);
		if (file_size < 0)
			return file_size;
		ret = copyfd_io_sendfile(infd, peerfd, outfd, file_size, &in_closed_after_out);
		break;

	default:
		fprintf(stderr, "Invalid mode %d\n", cfg_mode);

		die_usage();
		return 1;
	}

	if (ret)
		return ret;

	if (close_peerfd)
		close(peerfd);

	if (cfg_time) {
		unsigned int delta_ms;

		if (clock_gettime(CLOCK_MONOTONIC, &end) < 0)
			xerror("can not fetch end time %d", errno);
		delta_ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;
		if (delta_ms > cfg_time) {
			xerror("transfer slower than expected! runtime %d ms, expected %d ms",
			       delta_ms, cfg_time);
		}

		/* show the runtime only if this end shutdown(wr) before receiving the EOF,
		 * (that is, if this end got the longer runtime)
		 */
		if (in_closed_after_out)
			fprintf(stderr, "%d", delta_ms);
	}

	return 0;
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

static void maybe_close(int fd)
{
	unsigned int r = rand();

	if (!(cfg_join || cfg_remove || cfg_repeat > 1) && (r & 1))
		close(fd);
}

int main_loop_s(int listensock)
{
	struct sockaddr_storage ss;
	struct pollfd polls;
	socklen_t salen;
	int remotesock;
	int fd = 0;

again:
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
		maybe_close(listensock);
		check_sockaddr(pf, &ss, salen);
		check_getpeername(remotesock, &ss, salen);

		if (cfg_input) {
			fd = open(cfg_input, O_RDONLY);
			if (fd < 0)
				xerror("can't open %s: %d", cfg_input, errno);
		}

		SOCK_TEST_TCPULP(remotesock, 0);

		copyfd_io(fd, remotesock, 1, true);
	} else {
		perror("accept");
		return 1;
	}

	if (--cfg_repeat > 0) {
		if (cfg_input)
			close(fd);
		goto again;
	}

	return 0;
}

static void init_rng(void)
{
	unsigned int foo;

	if (getrandom(&foo, sizeof(foo), 0) == -1) {
		perror("getrandom");
		exit(1);
	}

	srand(foo);
}

static void xsetsockopt(int fd, int level, int optname, const void *optval, socklen_t optlen)
{
	int err;

	err = setsockopt(fd, level, optname, optval, optlen);
	if (err) {
		perror("setsockopt");
		exit(1);
	}
}

static void apply_cmsg_types(int fd, const struct cfg_cmsg_types *cmsg)
{
	static const unsigned int on = 1;

	if (cmsg->timestampns)
		xsetsockopt(fd, SOL_SOCKET, SO_TIMESTAMPNS_NEW, &on, sizeof(on));
	if (cmsg->tcp_inq)
		xsetsockopt(fd, IPPROTO_TCP, TCP_INQ, &on, sizeof(on));
}

static void parse_cmsg_types(const char *type)
{
	char *next = strchr(type, ',');
	unsigned int len = 0;

	cfg_cmsg_types.cmsg_enabled = 1;

	if (next) {
		parse_cmsg_types(next + 1);
		len = next - type;
	} else {
		len = strlen(type);
	}

	if (strncmp(type, "TIMESTAMPNS", len) == 0) {
		cfg_cmsg_types.timestampns = 1;
		return;
	}

	if (strncmp(type, "TCPINQ", len) == 0) {
		cfg_cmsg_types.tcp_inq = 1;
		return;
	}

	fprintf(stderr, "Unrecognized cmsg option %s\n", type);
	exit(1);
}

static void parse_setsock_options(const char *name)
{
	char *next = strchr(name, ',');
	unsigned int len = 0;

	if (next) {
		parse_setsock_options(next + 1);
		len = next - name;
	} else {
		len = strlen(name);
	}

	if (strncmp(name, "TRANSPARENT", len) == 0) {
		cfg_sockopt_types.transparent = 1;
		return;
	}

	fprintf(stderr, "Unrecognized setsockopt option %s\n", name);
	exit(1);
}

void xdisconnect(int fd, int addrlen)
{
	struct sockaddr_storage empty;
	int msec_sleep = 10;
	int queued = 1;
	int i;

	shutdown(fd, SHUT_WR);

	/* while until the pending data is completely flushed, the later
	 * disconnect will bypass/ignore/drop any pending data.
	 */
	for (i = 0; ; i += msec_sleep) {
		if (ioctl(fd, SIOCOUTQ, &queued) < 0)
			xerror("can't query out socket queue: %d", errno);

		if (!queued)
			break;

		if (i > poll_timeout)
			xerror("timeout while waiting for spool to complete");
		usleep(msec_sleep * 1000);
	}

	memset(&empty, 0, sizeof(empty));
	empty.ss_family = AF_UNSPEC;
	if (connect(fd, (struct sockaddr *)&empty, addrlen) < 0)
		xerror("can't disconnect: %d", errno);
}

int main_loop(void)
{
	int fd, ret, fd_in = 0;
	struct addrinfo *peer;

	/* listener is ready. */
	fd = sock_connect_mptcp(cfg_host, cfg_port, cfg_sock_proto, &peer);
	if (fd < 0)
		return 2;

again:
	check_getpeername_connect(fd);

	SOCK_TEST_TCPULP(fd, cfg_sock_proto);

	if (cfg_rcvbuf)
		set_rcvbuf(fd, cfg_rcvbuf);
	if (cfg_sndbuf)
		set_sndbuf(fd, cfg_sndbuf);
	if (cfg_cmsg_types.cmsg_enabled)
		apply_cmsg_types(fd, &cfg_cmsg_types);

	if (cfg_input) {
		fd_in = open(cfg_input, O_RDONLY);
		if (fd < 0)
			xerror("can't open %s:%d", cfg_input, errno);
	}

	/* close the client socket open only if we are not going to reconnect */
	ret = copyfd_io(fd_in, fd, 1, 0);
	if (ret)
		return ret;

	if (cfg_truncate > 0) {
		xdisconnect(fd, peer->ai_addrlen);
	} else if (--cfg_repeat > 0) {
		xdisconnect(fd, peer->ai_addrlen);

		/* the socket could be unblocking at this point, we need the
		 * connect to be blocking
		 */
		set_nonblock(fd, false);
		if (connect(fd, peer->ai_addr, peer->ai_addrlen))
			xerror("can't reconnect: %d", errno);
		if (cfg_input)
			close(fd_in);
		goto again;
	} else {
		close(fd);
	}

	return 0;
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

int parse_peek(const char *mode)
{
	if (!strcasecmp(mode, "saveWithPeek"))
		return CFG_WITH_PEEK;
	if (!strcasecmp(mode, "saveAfterPeek"))
		return CFG_AFTER_PEEK;

	fprintf(stderr, "Unknown: %s\n", mode);
	fprintf(stderr, "Supported MSG_PEEK mode are:\n");
	fprintf(stderr,
		"\t\t\"saveWithPeek\" - recv data with flags 'MSG_PEEK' and save the peek data into file\n");
	fprintf(stderr,
		"\t\t\"saveAfterPeek\" - read and save data into file after recv with flags 'MSG_PEEK'\n");

	die_usage();

	/* silence compiler warning */
	return 0;
}

static int parse_int(const char *size)
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

	return (int)s;
}

static void parse_opts(int argc, char **argv)
{
	int c;

	while ((c = getopt(argc, argv, "6c:f:hi:I:jlm:M:o:p:P:r:R:s:S:t:T:w:")) != -1) {
		switch (c) {
		case 'f':
			cfg_truncate = atoi(optarg);

			/* when receiving a fastclose, ignore PIPE signals and
			 * all the I/O errors later in the code
			 */
			if (cfg_truncate < 0) {
				cfg_rcv_trunc = true;
				signal(SIGPIPE, handle_signal);
			}
			break;
		case 'j':
			cfg_join = true;
			cfg_mode = CFG_MODE_POLL;
			break;
		case 'r':
			cfg_remove = true;
			cfg_mode = CFG_MODE_POLL;
			cfg_wait = 400000;
			cfg_do_w = atoi(optarg);
			if (cfg_do_w <= 0)
				cfg_do_w = 50;
			break;
		case 'i':
			cfg_input = optarg;
			break;
		case 'I':
			cfg_repeat = atoi(optarg);
			break;
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
		case '6':
			pf = AF_INET6;
			break;
		case 't':
			poll_timeout = atoi(optarg) * 1000;
			if (poll_timeout <= 0)
				poll_timeout = -1;
			break;
		case 'T':
			cfg_time = atoi(optarg);
			break;
		case 'm':
			cfg_mode = parse_mode(optarg);
			break;
		case 'S':
			cfg_sndbuf = parse_int(optarg);
			break;
		case 'R':
			cfg_rcvbuf = parse_int(optarg);
			break;
		case 'w':
			cfg_wait = atoi(optarg)*1000000;
			break;
		case 'M':
			cfg_mark = strtol(optarg, NULL, 0);
			break;
		case 'P':
			cfg_peek = parse_peek(optarg);
			break;
		case 'c':
			parse_cmsg_types(optarg);
			break;
		case 'o':
			parse_setsock_options(optarg);
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

	signal(SIGUSR1, handle_signal);
	parse_opts(argc, argv);

	if (listen_mode) {
		int fd = sock_listen_mptcp(cfg_host, cfg_port);

		if (fd < 0)
			return 1;

		if (cfg_rcvbuf)
			set_rcvbuf(fd, cfg_rcvbuf);
		if (cfg_sndbuf)
			set_sndbuf(fd, cfg_sndbuf);
		if (cfg_mark)
			set_mark(fd, cfg_mark);
		if (cfg_cmsg_types.cmsg_enabled)
			apply_cmsg_types(fd, &cfg_cmsg_types);

		return main_loop_s(fd);
	}

	return main_loop();
}
