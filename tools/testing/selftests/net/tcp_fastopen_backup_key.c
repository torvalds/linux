// SPDX-License-Identifier: GPL-2.0

/*
 * Test key rotation for TFO.
 * New keys are 'rotated' in two steps:
 * 1) Add new key as the 'backup' key 'behind' the primary key
 * 2) Make new key the primary by swapping the backup and primary keys
 *
 * The rotation is done in stages using multiple sockets bound
 * to the same port via SO_REUSEPORT. This simulates key rotation
 * behind say a load balancer. We verify that across the rotation
 * there are no cases in which a cookie is not accepted by verifying
 * that TcpExtTCPFastOpenPassiveFail remains 0.
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <error.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <time.h>

#ifndef TCP_FASTOPEN_KEY
#define TCP_FASTOPEN_KEY 33
#endif

#define N_LISTEN 10
#define PROC_FASTOPEN_KEY "/proc/sys/net/ipv4/tcp_fastopen_key"
#define KEY_LENGTH 16

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

static bool do_ipv6;
static bool do_sockopt;
static bool do_rotate;
static int key_len = KEY_LENGTH;
static int rcv_fds[N_LISTEN];
static int proc_fd;
static const char *IP4_ADDR = "127.0.0.1";
static const char *IP6_ADDR = "::1";
static const int PORT = 8891;

static void get_keys(int fd, uint32_t *keys)
{
	char buf[128];
	socklen_t len = KEY_LENGTH * 2;

	if (do_sockopt) {
		if (getsockopt(fd, SOL_TCP, TCP_FASTOPEN_KEY, keys, &len))
			error(1, errno, "Unable to get key");
		return;
	}
	lseek(proc_fd, 0, SEEK_SET);
	if (read(proc_fd, buf, sizeof(buf)) <= 0)
		error(1, errno, "Unable to read %s", PROC_FASTOPEN_KEY);
	if (sscanf(buf, "%x-%x-%x-%x,%x-%x-%x-%x", keys, keys + 1, keys + 2,
	    keys + 3, keys + 4, keys + 5, keys + 6, keys + 7) != 8)
		error(1, 0, "Unable to parse %s", PROC_FASTOPEN_KEY);
}

static void set_keys(int fd, uint32_t *keys)
{
	char buf[128];

	if (do_sockopt) {
		if (setsockopt(fd, SOL_TCP, TCP_FASTOPEN_KEY, keys,
		    key_len))
			error(1, errno, "Unable to set key");
		return;
	}
	if (do_rotate)
		snprintf(buf, 128, "%08x-%08x-%08x-%08x,%08x-%08x-%08x-%08x",
			 keys[0], keys[1], keys[2], keys[3], keys[4], keys[5],
			 keys[6], keys[7]);
	else
		snprintf(buf, 128, "%08x-%08x-%08x-%08x",
			 keys[0], keys[1], keys[2], keys[3]);
	lseek(proc_fd, 0, SEEK_SET);
	if (write(proc_fd, buf, sizeof(buf)) <= 0)
		error(1, errno, "Unable to write %s", PROC_FASTOPEN_KEY);
}

static void build_rcv_fd(int family, int proto, int *rcv_fds)
{
	struct sockaddr_in  addr4 = {0};
	struct sockaddr_in6 addr6 = {0};
	struct sockaddr *addr;
	int opt = 1, i, sz;
	int qlen = 100;
	uint32_t keys[8];

	switch (family) {
	case AF_INET:
		addr4.sin_family = family;
		addr4.sin_addr.s_addr = htonl(INADDR_ANY);
		addr4.sin_port = htons(PORT);
		sz = sizeof(addr4);
		addr = (struct sockaddr *)&addr4;
		break;
	case AF_INET6:
		addr6.sin6_family = AF_INET6;
		addr6.sin6_addr = in6addr_any;
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
	for (i = 0; i < ARRAY_SIZE(keys); i++)
		keys[i] = rand();
	for (i = 0; i < N_LISTEN; i++) {
		rcv_fds[i] = socket(family, proto, 0);
		if (rcv_fds[i] < 0)
			error(1, errno, "failed to create receive socket");
		if (setsockopt(rcv_fds[i], SOL_SOCKET, SO_REUSEPORT, &opt,
			       sizeof(opt)))
			error(1, errno, "failed to set SO_REUSEPORT");
		if (bind(rcv_fds[i], addr, sz))
			error(1, errno, "failed to bind receive socket");
		if (setsockopt(rcv_fds[i], SOL_TCP, TCP_FASTOPEN, &qlen,
			       sizeof(qlen)))
			error(1, errno, "failed to set TCP_FASTOPEN");
		set_keys(rcv_fds[i], keys);
		if (proto == SOCK_STREAM && listen(rcv_fds[i], 10))
			error(1, errno, "failed to listen on receive port");
	}
}

static int connect_and_send(int family, int proto)
{
	struct sockaddr_in  saddr4 = {0};
	struct sockaddr_in  daddr4 = {0};
	struct sockaddr_in6 saddr6 = {0};
	struct sockaddr_in6 daddr6 = {0};
	struct sockaddr *saddr, *daddr;
	int fd, sz, ret;
	char data[1];

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
	if (bind(fd, saddr, sz))
		error(1, errno, "failed to bind send socket");
	data[0] = 'a';
	ret = sendto(fd, data, 1, MSG_FASTOPEN, daddr, sz);
	if (ret != 1)
		error(1, errno, "failed to sendto");

	return fd;
}

static bool is_listen_fd(int fd)
{
	int i;

	for (i = 0; i < N_LISTEN; i++) {
		if (rcv_fds[i] == fd)
			return true;
	}
	return false;
}

static void rotate_key(int fd)
{
	static int iter;
	static uint32_t new_key[4];
	uint32_t keys[8];
	uint32_t tmp_key[4];
	int i;

	if (iter < N_LISTEN) {
		/* first set new key as backups */
		if (iter == 0) {
			for (i = 0; i < ARRAY_SIZE(new_key); i++)
				new_key[i] = rand();
		}
		get_keys(fd, keys);
		memcpy(keys + 4, new_key, KEY_LENGTH);
		set_keys(fd, keys);
	} else {
		/* swap the keys */
		get_keys(fd, keys);
		memcpy(tmp_key, keys + 4, KEY_LENGTH);
		memcpy(keys + 4, keys, KEY_LENGTH);
		memcpy(keys, tmp_key, KEY_LENGTH);
		set_keys(fd, keys);
	}
	if (++iter >= (N_LISTEN * 2))
		iter = 0;
}

static void run_one_test(int family)
{
	struct epoll_event ev;
	int i, send_fd;
	int n_loops = 10000;
	int rotate_key_fd = 0;
	int key_rotate_interval = 50;
	int fd, epfd;
	char buf[1];

	build_rcv_fd(family, SOCK_STREAM, rcv_fds);
	epfd = epoll_create(1);
	if (epfd < 0)
		error(1, errno, "failed to create epoll");
	ev.events = EPOLLIN;
	for (i = 0; i < N_LISTEN; i++) {
		ev.data.fd = rcv_fds[i];
		if (epoll_ctl(epfd, EPOLL_CTL_ADD, rcv_fds[i], &ev))
			error(1, errno, "failed to register sock epoll");
	}
	while (n_loops--) {
		send_fd = connect_and_send(family, SOCK_STREAM);
		if (do_rotate && ((n_loops % key_rotate_interval) == 0)) {
			rotate_key(rcv_fds[rotate_key_fd]);
			if (++rotate_key_fd >= N_LISTEN)
				rotate_key_fd = 0;
		}
		while (1) {
			i = epoll_wait(epfd, &ev, 1, -1);
			if (i < 0)
				error(1, errno, "epoll_wait failed");
			if (is_listen_fd(ev.data.fd)) {
				fd = accept(ev.data.fd, NULL, NULL);
				if (fd < 0)
					error(1, errno, "failed to accept");
				ev.data.fd = fd;
				if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev))
					error(1, errno, "failed epoll add");
				continue;
			}
			i = recv(ev.data.fd, buf, sizeof(buf), 0);
			if (i != 1)
				error(1, errno, "failed recv data");
			if (epoll_ctl(epfd, EPOLL_CTL_DEL, ev.data.fd, NULL))
				error(1, errno, "failed epoll del");
			close(ev.data.fd);
			break;
		}
		close(send_fd);
	}
	for (i = 0; i < N_LISTEN; i++)
		close(rcv_fds[i]);
}

static void parse_opts(int argc, char **argv)
{
	int c;

	while ((c = getopt(argc, argv, "46sr")) != -1) {
		switch (c) {
		case '4':
			do_ipv6 = false;
			break;
		case '6':
			do_ipv6 = true;
			break;
		case 's':
			do_sockopt = true;
			break;
		case 'r':
			do_rotate = true;
			key_len = KEY_LENGTH * 2;
			break;
		default:
			error(1, 0, "%s: parse error", argv[0]);
		}
	}
}

int main(int argc, char **argv)
{
	parse_opts(argc, argv);
	proc_fd = open(PROC_FASTOPEN_KEY, O_RDWR);
	if (proc_fd < 0)
		error(1, errno, "Unable to open %s", PROC_FASTOPEN_KEY);
	srand(time(NULL));
	if (do_ipv6)
		run_one_test(AF_INET6);
	else
		run_one_test(AF_INET);
	close(proc_fd);
	fprintf(stderr, "PASS\n");
	return 0;
}
