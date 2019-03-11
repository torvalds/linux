// SPDX-License-Identifier: GPL-2.0

/* Test that sockets listening on a specific address are preferred
 * over sockets listening on addr_any.
 */

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <error.h>
#include <linux/dccp.h>
#include <linux/in.h>
#include <linux/unistd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

static const char *IP4_ADDR = "127.0.0.1";
static const char *IP6_ADDR = "::1";
static const char *IP4_MAPPED6 = "::ffff:127.0.0.1";

static const int PORT = 8888;

static void build_rcv_fd(int family, int proto, int *rcv_fds, int count,
			 const char *addr_str)
{
	struct sockaddr_in  addr4 = {0};
	struct sockaddr_in6 addr6 = {0};
	struct sockaddr *addr;
	int opt, i, sz;

	memset(&addr, 0, sizeof(addr));

	switch (family) {
	case AF_INET:
		addr4.sin_family = family;
		if (!addr_str)
			addr4.sin_addr.s_addr = htonl(INADDR_ANY);
		else if (!inet_pton(family, addr_str, &addr4.sin_addr.s_addr))
			error(1, errno, "inet_pton failed: %s", addr_str);
		addr4.sin_port = htons(PORT);
		sz = sizeof(addr4);
		addr = (struct sockaddr *)&addr4;
		break;
	case AF_INET6:
		addr6.sin6_family = AF_INET6;
		if (!addr_str)
			addr6.sin6_addr = in6addr_any;
		else if (!inet_pton(family, addr_str, &addr6.sin6_addr))
			error(1, errno, "inet_pton failed: %s", addr_str);
		addr6.sin6_port = htons(PORT);
		sz = sizeof(addr6);
		addr = (struct sockaddr *)&addr6;
		break;
	default:
		error(1, 0, "Unsupported family %d", family);
		/* clang does not recognize error() above as terminating
		 * the program, so it complains that saddr, sz are
		 * not initialized when this code path is taken. Silence it.
		 */
		return;
	}

	for (i = 0; i < count; ++i) {
		rcv_fds[i] = socket(family, proto, 0);
		if (rcv_fds[i] < 0)
			error(1, errno, "failed to create receive socket");

		opt = 1;
		if (setsockopt(rcv_fds[i], SOL_SOCKET, SO_REUSEPORT, &opt,
			       sizeof(opt)))
			error(1, errno, "failed to set SO_REUSEPORT");

		if (bind(rcv_fds[i], addr, sz))
			error(1, errno, "failed to bind receive socket");

		if (proto == SOCK_STREAM && listen(rcv_fds[i], 10))
			error(1, errno, "tcp: failed to listen on receive port");
		else if (proto == SOCK_DCCP) {
			if (setsockopt(rcv_fds[i], SOL_DCCP,
					DCCP_SOCKOPT_SERVICE,
					&(int) {htonl(42)}, sizeof(int)))
				error(1, errno, "failed to setsockopt");

			if (listen(rcv_fds[i], 10))
				error(1, errno, "dccp: failed to listen on receive port");
		}
	}
}

static int connect_and_send(int family, int proto)
{
	struct sockaddr_in  saddr4 = {0};
	struct sockaddr_in  daddr4 = {0};
	struct sockaddr_in6 saddr6 = {0};
	struct sockaddr_in6 daddr6 = {0};
	struct sockaddr *saddr, *daddr;
	int fd, sz;

	switch (family) {
	case AF_INET:
		saddr4.sin_family = AF_INET;
		saddr4.sin_addr.s_addr = htonl(INADDR_ANY);
		saddr4.sin_port = 0;

		daddr4.sin_family = AF_INET;
		if (!inet_pton(family, IP4_ADDR, &daddr4.sin_addr.s_addr))
			error(1, errno, "inet_pton failed: %s", IP4_ADDR);
		daddr4.sin_port = htons(PORT);

		sz = sizeof(saddr4);
		saddr = (struct sockaddr *)&saddr4;
		daddr = (struct sockaddr *)&daddr4;
	break;
	case AF_INET6:
		saddr6.sin6_family = AF_INET6;
		saddr6.sin6_addr = in6addr_any;

		daddr6.sin6_family = AF_INET6;
		if (!inet_pton(family, IP6_ADDR, &daddr6.sin6_addr))
			error(1, errno, "inet_pton failed: %s", IP6_ADDR);
		daddr6.sin6_port = htons(PORT);

		sz = sizeof(saddr6);
		saddr = (struct sockaddr *)&saddr6;
		daddr = (struct sockaddr *)&daddr6;
	break;
	default:
		error(1, 0, "Unsupported family %d", family);
		/* clang does not recognize error() above as terminating
		 * the program, so it complains that saddr, daddr, sz are
		 * not initialized when this code path is taken. Silence it.
		 */
		return -1;
	}

	fd = socket(family, proto, 0);
	if (fd < 0)
		error(1, errno, "failed to create send socket");

	if (proto == SOCK_DCCP &&
		setsockopt(fd, SOL_DCCP, DCCP_SOCKOPT_SERVICE,
				&(int){htonl(42)}, sizeof(int)))
		error(1, errno, "failed to setsockopt");

	if (bind(fd, saddr, sz))
		error(1, errno, "failed to bind send socket");

	if (connect(fd, daddr, sz))
		error(1, errno, "failed to connect send socket");

	if (send(fd, "a", 1, 0) < 0)
		error(1, errno, "failed to send message");

	return fd;
}

static int receive_once(int epfd, int proto)
{
	struct epoll_event ev;
	int i, fd;
	char buf[8];

	i = epoll_wait(epfd, &ev, 1, 3);
	if (i < 0)
		error(1, errno, "epoll_wait failed");

	if (proto == SOCK_STREAM || proto == SOCK_DCCP) {
		fd = accept(ev.data.fd, NULL, NULL);
		if (fd < 0)
			error(1, errno, "failed to accept");
		i = recv(fd, buf, sizeof(buf), 0);
		close(fd);
	} else {
		i = recv(ev.data.fd, buf, sizeof(buf), 0);
	}

	if (i < 0)
		error(1, errno, "failed to recv");

	return ev.data.fd;
}

static void test(int *rcv_fds, int count, int family, int proto, int fd)
{
	struct epoll_event ev;
	int epfd, i, send_fd, recv_fd;

	epfd = epoll_create(1);
	if (epfd < 0)
		error(1, errno, "failed to create epoll");

	ev.events = EPOLLIN;
	for (i = 0; i < count; ++i) {
		ev.data.fd = rcv_fds[i];
		if (epoll_ctl(epfd, EPOLL_CTL_ADD, rcv_fds[i], &ev))
			error(1, errno, "failed to register sock epoll");
	}

	send_fd = connect_and_send(family, proto);

	recv_fd = receive_once(epfd, proto);
	if (recv_fd != fd)
		error(1, 0, "received on an unexpected socket");

	close(send_fd);
	close(epfd);
}


static void run_one_test(int fam_send, int fam_rcv, int proto,
			 const char *addr_str)
{
	/* Below we test that a socket listening on a specific address
	 * is always selected in preference over a socket listening
	 * on addr_any. Bugs where this is not the case often result
	 * in sockets created first or last to get picked. So below
	 * we make sure that there are always addr_any sockets created
	 * before and after a specific socket is created.
	 */
	int rcv_fds[10], i;

	build_rcv_fd(AF_INET, proto, rcv_fds, 2, NULL);
	build_rcv_fd(AF_INET6, proto, rcv_fds + 2, 2, NULL);
	build_rcv_fd(fam_rcv, proto, rcv_fds + 4, 1, addr_str);
	build_rcv_fd(AF_INET, proto, rcv_fds + 5, 2, NULL);
	build_rcv_fd(AF_INET6, proto, rcv_fds + 7, 2, NULL);
	test(rcv_fds, 9, fam_send, proto, rcv_fds[4]);
	for (i = 0; i < 9; ++i)
		close(rcv_fds[i]);
	fprintf(stderr, "pass\n");
}

static void test_proto(int proto, const char *proto_str)
{
	if (proto == SOCK_DCCP) {
		int test_fd;

		test_fd = socket(AF_INET, proto, 0);
		if (test_fd < 0) {
			if (errno == ESOCKTNOSUPPORT) {
				fprintf(stderr, "DCCP not supported: skipping DCCP tests\n");
				return;
			} else
				error(1, errno, "failed to create a DCCP socket");
		}
		close(test_fd);
	}

	fprintf(stderr, "%s IPv4 ... ", proto_str);
	run_one_test(AF_INET, AF_INET, proto, IP4_ADDR);

	fprintf(stderr, "%s IPv6 ... ", proto_str);
	run_one_test(AF_INET6, AF_INET6, proto, IP6_ADDR);

	fprintf(stderr, "%s IPv4 mapped to IPv6 ... ", proto_str);
	run_one_test(AF_INET, AF_INET6, proto, IP4_MAPPED6);
}

int main(void)
{
	test_proto(SOCK_DGRAM, "UDP");
	test_proto(SOCK_STREAM, "TCP");
	test_proto(SOCK_DCCP, "DCCP");

	fprintf(stderr, "SUCCESS\n");
	return 0;
}
