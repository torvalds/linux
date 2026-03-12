// SPDX-License-Identifier: GPL-2.0
/* Copyright 2025 Google LLC */

#include <test_progs.h>
#include "sk_bypass_prot_mem.skel.h"
#include "network_helpers.h"

#ifndef PAGE_SIZE
#include <unistd.h>
#define PAGE_SIZE getpagesize()
#endif

#define NR_PAGES	32
#define NR_SOCKETS	2
#define BUF_TOTAL	(NR_PAGES * PAGE_SIZE / NR_SOCKETS)
#define BUF_SINGLE	1024
#define NR_SEND		(BUF_TOTAL / BUF_SINGLE)

struct test_case {
	char name[8];
	int family;
	int type;
	int (*create_sockets)(struct test_case *test_case, int sk[], int len);
	long (*get_memory_allocated)(struct test_case *test_case, struct sk_bypass_prot_mem *skel);
};

static int tcp_create_sockets(struct test_case *test_case, int sk[], int len)
{
	int server, i, err = 0;

	server = start_server(test_case->family, test_case->type, NULL, 0, 0);
	if (!ASSERT_GE(server, 0, "start_server_str"))
		return server;

	/* Keep for-loop so we can change NR_SOCKETS easily. */
	for (i = 0; i < len; i += 2) {
		sk[i] = connect_to_fd(server, 0);
		if (sk[i] < 0) {
			ASSERT_GE(sk[i], 0, "connect_to_fd");
			err = sk[i];
			break;
		}

		sk[i + 1] = accept(server, NULL, NULL);
		if (sk[i + 1] < 0) {
			ASSERT_GE(sk[i + 1], 0, "accept");
			err = sk[i + 1];
			break;
		}
	}

	close(server);

	return err;
}

static int udp_create_sockets(struct test_case *test_case, int sk[], int len)
{
	int i, j, err, rcvbuf = BUF_TOTAL;

	/* Keep for-loop so we can change NR_SOCKETS easily. */
	for (i = 0; i < len; i += 2) {
		sk[i] = start_server(test_case->family, test_case->type, NULL, 0, 0);
		if (sk[i] < 0) {
			ASSERT_GE(sk[i], 0, "start_server");
			return sk[i];
		}

		sk[i + 1] = connect_to_fd(sk[i], 0);
		if (sk[i + 1] < 0) {
			ASSERT_GE(sk[i + 1], 0, "connect_to_fd");
			return sk[i + 1];
		}

		err = connect_fd_to_fd(sk[i], sk[i + 1], 0);
		if (err) {
			ASSERT_EQ(err, 0, "connect_fd_to_fd");
			return err;
		}

		for (j = 0; j < 2; j++) {
			err = setsockopt(sk[i + j], SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(int));
			if (err) {
				ASSERT_EQ(err, 0, "setsockopt(SO_RCVBUF)");
				return err;
			}
		}
	}

	return 0;
}

static long get_memory_allocated(struct test_case *test_case,
				 bool *activated, long *memory_allocated)
{
	int sk;

	*activated = true;

	/* AF_INET and AF_INET6 share the same memory_allocated.
	 * tcp_init_sock() is called by AF_INET and AF_INET6,
	 * but udp_lib_init_sock() is inline.
	 */
	sk = socket(AF_INET, test_case->type, 0);
	if (!ASSERT_GE(sk, 0, "get_memory_allocated"))
		return -1;

	close(sk);

	return *memory_allocated;
}

static long tcp_get_memory_allocated(struct test_case *test_case, struct sk_bypass_prot_mem *skel)
{
	return get_memory_allocated(test_case,
				    &skel->bss->tcp_activated,
				    &skel->bss->tcp_memory_allocated);
}

static long udp_get_memory_allocated(struct test_case *test_case, struct sk_bypass_prot_mem *skel)
{
	return get_memory_allocated(test_case,
				    &skel->bss->udp_activated,
				    &skel->bss->udp_memory_allocated);
}

static int check_bypass(struct test_case *test_case,
			struct sk_bypass_prot_mem *skel, bool bypass)
{
	char buf[BUF_SINGLE] = {};
	long memory_allocated[2];
	int sk[NR_SOCKETS];
	int err, i, j;

	for (i = 0; i < ARRAY_SIZE(sk); i++)
		sk[i] = -1;

	err = test_case->create_sockets(test_case, sk, ARRAY_SIZE(sk));
	if (err)
		goto close;

	memory_allocated[0] = test_case->get_memory_allocated(test_case, skel);

	/* allocate pages >= NR_PAGES */
	for (i = 0; i < ARRAY_SIZE(sk); i++) {
		for (j = 0; j < NR_SEND; j++) {
			int bytes = send(sk[i], buf, sizeof(buf), 0);

			/* Avoid too noisy logs when something failed. */
			if (bytes != sizeof(buf)) {
				ASSERT_EQ(bytes, sizeof(buf), "send");
				if (bytes < 0) {
					err = bytes;
					goto drain;
				}
			}
		}
	}

	memory_allocated[1] = test_case->get_memory_allocated(test_case, skel);

	if (bypass)
		ASSERT_LE(memory_allocated[1], memory_allocated[0] + 10, "bypass");
	else
		ASSERT_GT(memory_allocated[1], memory_allocated[0] + NR_PAGES, "no bypass");

drain:
	if (test_case->type == SOCK_DGRAM) {
		/* UDP starts purging sk->sk_receive_queue after one RCU
		 * grace period, then udp_memory_allocated goes down,
		 * so drain the queue before close().
		 */
		for (i = 0; i < ARRAY_SIZE(sk); i++) {
			for (j = 0; j < NR_SEND; j++) {
				int bytes = recv(sk[i], buf, 1, MSG_DONTWAIT | MSG_TRUNC);

				if (bytes == sizeof(buf))
					continue;
				if (bytes != -1 || errno != EAGAIN)
					PRINT_FAIL("bytes: %d, errno: %s\n", bytes, strerror(errno));
				break;
			}
		}
	}

close:
	for (i = 0; i < ARRAY_SIZE(sk); i++) {
		if (sk[i] < 0)
			break;

		close(sk[i]);
	}

	return err;
}

static void run_test(struct test_case *test_case)
{
	struct sk_bypass_prot_mem *skel;
	struct nstoken *nstoken;
	int cgroup, err;

	skel = sk_bypass_prot_mem__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		return;

	skel->bss->nr_cpus = libbpf_num_possible_cpus();

	err = sk_bypass_prot_mem__attach(skel);
	if (!ASSERT_OK(err, "attach"))
		goto destroy_skel;

	cgroup = test__join_cgroup("/sk_bypass_prot_mem");
	if (!ASSERT_GE(cgroup, 0, "join_cgroup"))
		goto destroy_skel;

	err = make_netns("sk_bypass_prot_mem");
	if (!ASSERT_EQ(err, 0, "make_netns"))
		goto close_cgroup;

	nstoken = open_netns("sk_bypass_prot_mem");
	if (!ASSERT_OK_PTR(nstoken, "open_netns"))
		goto remove_netns;

	err = check_bypass(test_case, skel, false);
	if (!ASSERT_EQ(err, 0, "test_bypass(false)"))
		goto close_netns;

	err = write_sysctl("/proc/sys/net/core/bypass_prot_mem", "1");
	if (!ASSERT_EQ(err, 0, "write_sysctl(1)"))
		goto close_netns;

	err = check_bypass(test_case, skel, true);
	if (!ASSERT_EQ(err, 0, "test_bypass(true by sysctl)"))
		goto close_netns;

	err = write_sysctl("/proc/sys/net/core/bypass_prot_mem", "0");
	if (!ASSERT_EQ(err, 0, "write_sysctl(0)"))
		goto close_netns;

	skel->links.sock_create = bpf_program__attach_cgroup(skel->progs.sock_create, cgroup);
	if (!ASSERT_OK_PTR(skel->links.sock_create, "attach_cgroup(sock_create)"))
		goto close_netns;

	err = check_bypass(test_case, skel, true);
	ASSERT_EQ(err, 0, "test_bypass(true by bpf)");

close_netns:
	close_netns(nstoken);
remove_netns:
	remove_netns("sk_bypass_prot_mem");
close_cgroup:
	close(cgroup);
destroy_skel:
	sk_bypass_prot_mem__destroy(skel);
}

static struct test_case test_cases[] = {
	{
		.name = "TCP  ",
		.family = AF_INET,
		.type = SOCK_STREAM,
		.create_sockets = tcp_create_sockets,
		.get_memory_allocated = tcp_get_memory_allocated,
	},
	{
		.name = "UDP  ",
		.family = AF_INET,
		.type = SOCK_DGRAM,
		.create_sockets = udp_create_sockets,
		.get_memory_allocated = udp_get_memory_allocated,
	},
	{
		.name = "TCPv6",
		.family = AF_INET6,
		.type = SOCK_STREAM,
		.create_sockets = tcp_create_sockets,
		.get_memory_allocated = tcp_get_memory_allocated,
	},
	{
		.name = "UDPv6",
		.family = AF_INET6,
		.type = SOCK_DGRAM,
		.create_sockets = udp_create_sockets,
		.get_memory_allocated = udp_get_memory_allocated,
	},
};

void serial_test_sk_bypass_prot_mem(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(test_cases); i++) {
		if (test__start_subtest(test_cases[i].name))
			run_test(&test_cases[i]);
	}
}
