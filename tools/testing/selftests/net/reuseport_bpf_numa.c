// SPDX-License-Identifier: GPL-2.0
/*
 * Test functionality of BPF filters with SO_REUSEPORT. Same test as
 * in reuseport_bpf_cpu, only as one socket per NUMA node.
 */

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <error.h>
#include <linux/filter.h>
#include <linux/bpf.h>
#include <linux/in.h>
#include <linux/unistd.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <numa.h>

#include "../kselftest.h"

static const int PORT = 8888;

static void build_rcv_group(int *rcv_fd, size_t len, int family, int proto)
{
	struct sockaddr_storage addr;
	struct sockaddr_in  *addr4;
	struct sockaddr_in6 *addr6;
	size_t i;
	int opt;

	switch (family) {
	case AF_INET:
		addr4 = (struct sockaddr_in *)&addr;
		addr4->sin_family = AF_INET;
		addr4->sin_addr.s_addr = htonl(INADDR_ANY);
		addr4->sin_port = htons(PORT);
		break;
	case AF_INET6:
		addr6 = (struct sockaddr_in6 *)&addr;
		addr6->sin6_family = AF_INET6;
		addr6->sin6_addr = in6addr_any;
		addr6->sin6_port = htons(PORT);
		break;
	default:
		error(1, 0, "Unsupported family %d", family);
	}

	for (i = 0; i < len; ++i) {
		rcv_fd[i] = socket(family, proto, 0);
		if (rcv_fd[i] < 0)
			error(1, errno, "failed to create receive socket");

		opt = 1;
		if (setsockopt(rcv_fd[i], SOL_SOCKET, SO_REUSEPORT, &opt,
			       sizeof(opt)))
			error(1, errno, "failed to set SO_REUSEPORT");

		if (bind(rcv_fd[i], (struct sockaddr *)&addr, sizeof(addr)))
			error(1, errno, "failed to bind receive socket");

		if (proto == SOCK_STREAM && listen(rcv_fd[i], len * 10))
			error(1, errno, "failed to listen on receive port");
	}
}

static void attach_bpf(int fd)
{
	static char bpf_log_buf[65536];
	static const char bpf_license[] = "";

	int bpf_fd;
	const struct bpf_insn prog[] = {
		/* R0 = bpf_get_numa_node_id() */
		{ BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_get_numa_node_id },
		/* return R0 */
		{ BPF_JMP | BPF_EXIT, 0, 0, 0, 0 }
	};
	union bpf_attr attr;

	memset(&attr, 0, sizeof(attr));
	attr.prog_type = BPF_PROG_TYPE_SOCKET_FILTER;
	attr.insn_cnt = sizeof(prog) / sizeof(prog[0]);
	attr.insns = (unsigned long) &prog;
	attr.license = (unsigned long) &bpf_license;
	attr.log_buf = (unsigned long) &bpf_log_buf;
	attr.log_size = sizeof(bpf_log_buf);
	attr.log_level = 1;

	bpf_fd = syscall(__NR_bpf, BPF_PROG_LOAD, &attr, sizeof(attr));
	if (bpf_fd < 0)
		error(1, errno, "ebpf error. log:\n%s\n", bpf_log_buf);

	if (setsockopt(fd, SOL_SOCKET, SO_ATTACH_REUSEPORT_EBPF, &bpf_fd,
			sizeof(bpf_fd)))
		error(1, errno, "failed to set SO_ATTACH_REUSEPORT_EBPF");

	close(bpf_fd);
}

static void send_from_node(int node_id, int family, int proto)
{
	struct sockaddr_storage saddr, daddr;
	struct sockaddr_in  *saddr4, *daddr4;
	struct sockaddr_in6 *saddr6, *daddr6;
	int fd;

	switch (family) {
	case AF_INET:
		saddr4 = (struct sockaddr_in *)&saddr;
		saddr4->sin_family = AF_INET;
		saddr4->sin_addr.s_addr = htonl(INADDR_ANY);
		saddr4->sin_port = 0;

		daddr4 = (struct sockaddr_in *)&daddr;
		daddr4->sin_family = AF_INET;
		daddr4->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		daddr4->sin_port = htons(PORT);
		break;
	case AF_INET6:
		saddr6 = (struct sockaddr_in6 *)&saddr;
		saddr6->sin6_family = AF_INET6;
		saddr6->sin6_addr = in6addr_any;
		saddr6->sin6_port = 0;

		daddr6 = (struct sockaddr_in6 *)&daddr;
		daddr6->sin6_family = AF_INET6;
		daddr6->sin6_addr = in6addr_loopback;
		daddr6->sin6_port = htons(PORT);
		break;
	default:
		error(1, 0, "Unsupported family %d", family);
	}

	if (numa_run_on_node(node_id) < 0)
		error(1, errno, "failed to pin to node");

	fd = socket(family, proto, 0);
	if (fd < 0)
		error(1, errno, "failed to create send socket");

	if (bind(fd, (struct sockaddr *)&saddr, sizeof(saddr)))
		error(1, errno, "failed to bind send socket");

	if (connect(fd, (struct sockaddr *)&daddr, sizeof(daddr)))
		error(1, errno, "failed to connect send socket");

	if (send(fd, "a", 1, 0) < 0)
		error(1, errno, "failed to send message");

	close(fd);
}

static
void receive_on_node(int *rcv_fd, int len, int epfd, int node_id, int proto)
{
	struct epoll_event ev;
	int i, fd;
	char buf[8];

	i = epoll_wait(epfd, &ev, 1, -1);
	if (i < 0)
		error(1, errno, "epoll_wait failed");

	if (proto == SOCK_STREAM) {
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

	for (i = 0; i < len; ++i)
		if (ev.data.fd == rcv_fd[i])
			break;
	if (i == len)
		error(1, 0, "failed to find socket");
	fprintf(stderr, "send node %d, receive socket %d\n", node_id, i);
	if (node_id != i)
		error(1, 0, "node id/receive socket mismatch");
}

static void test(int *rcv_fd, int len, int family, int proto)
{
	struct epoll_event ev;
	int epfd, node;

	build_rcv_group(rcv_fd, len, family, proto);
	attach_bpf(rcv_fd[0]);

	epfd = epoll_create(1);
	if (epfd < 0)
		error(1, errno, "failed to create epoll");
	for (node = 0; node < len; ++node) {
		ev.events = EPOLLIN;
		ev.data.fd = rcv_fd[node];
		if (epoll_ctl(epfd, EPOLL_CTL_ADD, rcv_fd[node], &ev))
			error(1, errno, "failed to register sock epoll");
	}

	/* Forward iterate */
	for (node = 0; node < len; ++node) {
		if (!numa_bitmask_isbitset(numa_nodes_ptr, node))
			continue;
		send_from_node(node, family, proto);
		receive_on_node(rcv_fd, len, epfd, node, proto);
	}

	/* Reverse iterate */
	for (node = len - 1; node >= 0; --node) {
		if (!numa_bitmask_isbitset(numa_nodes_ptr, node))
			continue;
		send_from_node(node, family, proto);
		receive_on_node(rcv_fd, len, epfd, node, proto);
	}

	close(epfd);
	for (node = 0; node < len; ++node)
		close(rcv_fd[node]);
}

int main(void)
{
	int *rcv_fd, nodes;

	if (numa_available() < 0)
		ksft_exit_skip("no numa api support\n");

	nodes = numa_max_node() + 1;

	rcv_fd = calloc(nodes, sizeof(int));
	if (!rcv_fd)
		error(1, 0, "failed to allocate array");

	fprintf(stderr, "---- IPv4 UDP ----\n");
	test(rcv_fd, nodes, AF_INET, SOCK_DGRAM);

	fprintf(stderr, "---- IPv6 UDP ----\n");
	test(rcv_fd, nodes, AF_INET6, SOCK_DGRAM);

	fprintf(stderr, "---- IPv4 TCP ----\n");
	test(rcv_fd, nodes, AF_INET, SOCK_STREAM);

	fprintf(stderr, "---- IPv6 TCP ----\n");
	test(rcv_fd, nodes, AF_INET6, SOCK_STREAM);

	free(rcv_fd);

	fprintf(stderr, "SUCCESS\n");
	return 0;
}
