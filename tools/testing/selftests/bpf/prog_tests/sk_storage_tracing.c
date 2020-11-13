// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */

#include <sys/types.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include "test_progs.h"
#include "network_helpers.h"
#include "test_sk_storage_trace_itself.skel.h"
#include "test_sk_storage_tracing.skel.h"

#define LO_ADDR6 "::1"
#define TEST_COMM "test_progs"

struct sk_stg {
	__u32 pid;
	__u32 last_notclose_state;
	char comm[16];
};

static struct test_sk_storage_tracing *skel;
static __u32 duration;
static pid_t my_pid;

static int check_sk_stg(int sk_fd, __u32 expected_state)
{
	struct sk_stg sk_stg;
	int err;

	err = bpf_map_lookup_elem(bpf_map__fd(skel->maps.sk_stg_map), &sk_fd,
				  &sk_stg);
	if (!ASSERT_OK(err, "map_lookup(sk_stg_map)"))
		return -1;

	if (!ASSERT_EQ(sk_stg.last_notclose_state, expected_state,
		       "last_notclose_state"))
		return -1;

	if (!ASSERT_EQ(sk_stg.pid, my_pid, "pid"))
		return -1;

	if (!ASSERT_STREQ(sk_stg.comm, skel->bss->task_comm, "task_comm"))
		return -1;

	return 0;
}

static void do_test(void)
{
	int listen_fd = -1, passive_fd = -1, active_fd = -1, value = 1, err;
	char abyte;

	listen_fd = start_server(AF_INET6, SOCK_STREAM, LO_ADDR6, 0, 0);
	if (CHECK(listen_fd == -1, "start_server",
		  "listen_fd:%d errno:%d\n", listen_fd, errno))
		return;

	active_fd = connect_to_fd(listen_fd, 0);
	if (CHECK(active_fd == -1, "connect_to_fd", "active_fd:%d errno:%d\n",
		  active_fd, errno))
		goto out;

	err = bpf_map_update_elem(bpf_map__fd(skel->maps.del_sk_stg_map),
				  &active_fd, &value, 0);
	if (!ASSERT_OK(err, "map_update(del_sk_stg_map)"))
		goto out;

	passive_fd = accept(listen_fd, NULL, 0);
	if (CHECK(passive_fd == -1, "accept", "passive_fd:%d errno:%d\n",
		  passive_fd, errno))
		goto out;

	shutdown(active_fd, SHUT_WR);
	err = read(passive_fd, &abyte, 1);
	if (!ASSERT_OK(err, "read(passive_fd)"))
		goto out;

	shutdown(passive_fd, SHUT_WR);
	err = read(active_fd, &abyte, 1);
	if (!ASSERT_OK(err, "read(active_fd)"))
		goto out;

	err = bpf_map_lookup_elem(bpf_map__fd(skel->maps.del_sk_stg_map),
				  &active_fd, &value);
	if (!ASSERT_ERR(err, "map_lookup(del_sk_stg_map)"))
		goto out;

	err = check_sk_stg(listen_fd, BPF_TCP_LISTEN);
	if (!ASSERT_OK(err, "listen_fd sk_stg"))
		goto out;

	err = check_sk_stg(active_fd, BPF_TCP_FIN_WAIT2);
	if (!ASSERT_OK(err, "active_fd sk_stg"))
		goto out;

	err = check_sk_stg(passive_fd, BPF_TCP_LAST_ACK);
	ASSERT_OK(err, "passive_fd sk_stg");

out:
	if (active_fd != -1)
		close(active_fd);
	if (passive_fd != -1)
		close(passive_fd);
	if (listen_fd != -1)
		close(listen_fd);
}

void test_sk_storage_tracing(void)
{
	struct test_sk_storage_trace_itself *skel_itself;
	int err;

	my_pid = getpid();

	skel_itself = test_sk_storage_trace_itself__open_and_load();

	if (!ASSERT_NULL(skel_itself, "test_sk_storage_trace_itself")) {
		test_sk_storage_trace_itself__destroy(skel_itself);
		return;
	}

	skel = test_sk_storage_tracing__open_and_load();
	if (!ASSERT_OK_PTR(skel, "test_sk_storage_tracing"))
		return;

	err = test_sk_storage_tracing__attach(skel);
	if (!ASSERT_OK(err, "test_sk_storage_tracing__attach")) {
		test_sk_storage_tracing__destroy(skel);
		return;
	}

	do_test();

	test_sk_storage_tracing__destroy(skel);
}
