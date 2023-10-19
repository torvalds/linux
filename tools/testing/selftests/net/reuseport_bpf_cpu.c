// SPDX-License-Identifier: GPL-2.0
/*
 * Test functionality of BPF filters with SO_REUSEPORT.  This program creates
 * an SO_REUSEPORT receiver group containing one socket per CPU core. It then
 * creates a BPF program that will select a socket from this group based
 * on the core id that receives the packet.  The sending code artificially
 * moves itself to run on different core ids and sends one message from
 * each core.  Since these packets are delivered over loopback, they should
 * arrive on the same core that sent them.  The receiving code then ensures
 * that the packet was received on the socket for the corresponding core id.
 * This entire process is done for several different core id permutations
 * and for each IPv4/IPv6 and TCP/UDP combination.
 */

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <error.h>
#include <linux/filter.h>
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
	struct sock_filter code[] = {
		/* A = raw_smp_processor_id() */
		{ BPF_LD  | BPF_W | BPF_ABS, 0, 0, SKF_AD_OFF + SKF_AD_CPU },
		/* return A */
		{ BPF_RET | BPF_A, 0, 0, 0 },
	};
	struct sock_fprog p = {
		.len = 2,
		.filter = code,
	};

	if (setsockopt(fd, SOL_SOCKET, SO_ATTACH_REUSEPORT_CBPF, &p, sizeof(p)))
		error(1, errno, "failed to set SO_ATTACH_REUSEPORT_CBPF");
}

static void send_from_cpu(int cpu_id, int family, int proto)
{
	struct sockaddr_storage saddr, daddr;
	struct sockaddr_in  *saddr4, *daddr4;
	struct sockaddr_in6 *saddr6, *daddr6;
	cpu_set_t cpu_set;
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

	memset(&cpu_set, 0, sizeof(cpu_set));
	CPU_SET(cpu_id, &cpu_set);
	if (sched_setaffinity(0, sizeof(cpu_set), &cpu_set) < 0)
		error(1, errno, "failed to pin to cpu");

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
void receive_on_cpu(int *rcv_fd, int len, int epfd, int cpu_id, int proto)
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
	fprintf(stderr, "send cpu %d, receive socket %d\n", cpu_id, i);
	if (cpu_id != i)
		error(1, 0, "cpu id/receive socket mismatch");
}

static void test(int *rcv_fd, int len, int family, int proto)
{
	struct epoll_event ev;
	int epfd, cpu;

	build_rcv_group(rcv_fd, len, family, proto);
	attach_bpf(rcv_fd[0]);

	epfd = epoll_create(1);
	if (epfd < 0)
		error(1, errno, "failed to create epoll");
	for (cpu = 0; cpu < len; ++cpu) {
		ev.events = EPOLLIN;
		ev.data.fd = rcv_fd[cpu];
		if (epoll_ctl(epfd, EPOLL_CTL_ADD, rcv_fd[cpu], &ev))
			error(1, errno, "failed to register sock epoll");
	}

	/* Forward iterate */
	for (cpu = 0; cpu < len; ++cpu) {
		send_from_cpu(cpu, family, proto);
		receive_on_cpu(rcv_fd, len, epfd, cpu, proto);
	}

	/* Reverse iterate */
	for (cpu = len - 1; cpu >= 0; --cpu) {
		send_from_cpu(cpu, family, proto);
		receive_on_cpu(rcv_fd, len, epfd, cpu, proto);
	}

	/* Even cores */
	for (cpu = 0; cpu < len; cpu += 2) {
		send_from_cpu(cpu, family, proto);
		receive_on_cpu(rcv_fd, len, epfd, cpu, proto);
	}

	/* Odd cores */
	for (cpu = 1; cpu < len; cpu += 2) {
		send_from_cpu(cpu, family, proto);
		receive_on_cpu(rcv_fd, len, epfd, cpu, proto);
	}

	close(epfd);
	for (cpu = 0; cpu < len; ++cpu)
		close(rcv_fd[cpu]);
}

int main(void)
{
	int *rcv_fd, cpus;

	cpus = sysconf(_SC_NPROCESSORS_ONLN);
	if (cpus <= 0)
		error(1, errno, "failed counting cpus");

	rcv_fd = calloc(cpus, sizeof(int));
	if (!rcv_fd)
		error(1, 0, "failed to allocate array");

	fprintf(stderr, "---- IPv4 UDP ----\n");
	test(rcv_fd, cpus, AF_INET, SOCK_DGRAM);

	fprintf(stderr, "---- IPv6 UDP ----\n");
	test(rcv_fd, cpus, AF_INET6, SOCK_DGRAM);

	fprintf(stderr, "---- IPv4 TCP ----\n");
	test(rcv_fd, cpus, AF_INET, SOCK_STREAM);

	fprintf(stderr, "---- IPv6 TCP ----\n");
	test(rcv_fd, cpus, AF_INET6, SOCK_STREAM);

	free(rcv_fd);

	fprintf(stderr, "SUCCESS\n");
	return 0;
}
