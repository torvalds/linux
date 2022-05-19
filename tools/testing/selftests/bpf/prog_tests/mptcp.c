// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020, Tessares SA. */
/* Copyright (c) 2022, SUSE. */

#include <test_progs.h>
#include "cgroup_helpers.h"
#include "network_helpers.h"
#include "mptcp_sock.skel.h"

struct mptcp_storage {
	__u32 invoked;
	__u32 is_mptcp;
};

static int verify_sk(int map_fd, int client_fd, __u32 is_mptcp)
{
	int err, cfd = client_fd;
	struct mptcp_storage val;

	if (is_mptcp == 1)
		return 0;

	err = bpf_map_lookup_elem(map_fd, &cfd, &val);
	if (!ASSERT_OK(err, "bpf_map_lookup_elem"))
		return err;

	if (!ASSERT_EQ(val.invoked, 1, "unexpected invoked count"))
		err++;

	if (!ASSERT_EQ(val.is_mptcp, 0, "unexpected is_mptcp"))
		err++;

	return err;
}

static int run_test(int cgroup_fd, int server_fd, bool is_mptcp)
{
	int client_fd, prog_fd, map_fd, err;
	struct mptcp_sock *sock_skel;

	sock_skel = mptcp_sock__open_and_load();
	if (!ASSERT_OK_PTR(sock_skel, "skel_open_load"))
		return -EIO;

	prog_fd = bpf_program__fd(sock_skel->progs._sockops);
	if (!ASSERT_GE(prog_fd, 0, "bpf_program__fd")) {
		err = -EIO;
		goto out;
	}

	map_fd = bpf_map__fd(sock_skel->maps.socket_storage_map);
	if (!ASSERT_GE(map_fd, 0, "bpf_map__fd")) {
		err = -EIO;
		goto out;
	}

	err = bpf_prog_attach(prog_fd, cgroup_fd, BPF_CGROUP_SOCK_OPS, 0);
	if (!ASSERT_OK(err, "bpf_prog_attach"))
		goto out;

	client_fd = connect_to_fd(server_fd, 0);
	if (!ASSERT_GE(client_fd, 0, "connect to fd")) {
		err = -EIO;
		goto out;
	}

	err += is_mptcp ? verify_sk(map_fd, client_fd, 1) :
			  verify_sk(map_fd, client_fd, 0);

	close(client_fd);

out:
	mptcp_sock__destroy(sock_skel);
	return err;
}

static void test_base(void)
{
	int server_fd, cgroup_fd;

	cgroup_fd = test__join_cgroup("/mptcp");
	if (!ASSERT_GE(cgroup_fd, 0, "test__join_cgroup"))
		return;

	/* without MPTCP */
	server_fd = start_server(AF_INET, SOCK_STREAM, NULL, 0, 0);
	if (!ASSERT_GE(server_fd, 0, "start_server"))
		goto with_mptcp;

	ASSERT_OK(run_test(cgroup_fd, server_fd, false), "run_test tcp");

	close(server_fd);

with_mptcp:
	/* with MPTCP */
	server_fd = start_mptcp_server(AF_INET, NULL, 0, 0);
	if (!ASSERT_GE(server_fd, 0, "start_mptcp_server"))
		goto close_cgroup_fd;

	ASSERT_OK(run_test(cgroup_fd, server_fd, true), "run_test mptcp");

	close(server_fd);

close_cgroup_fd:
	close(cgroup_fd);
}

void test_mptcp(void)
{
	if (test__start_subtest("base"))
		test_base();
}
