// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include "cgroup_helpers.h"
#include "network_helpers.h"
#include "sock_ops_get_sk.skel.h"

/* See progs/sock_ops_get_sk.c for the bug description. */
static void run_sock_ops_test(int cgroup_fd, int prog_fd)
{
	int server_fd, client_fd, err;

	err = bpf_prog_attach(prog_fd, cgroup_fd, BPF_CGROUP_SOCK_OPS, 0);
	if (!ASSERT_OK(err, "prog_attach"))
		return;

	server_fd = start_server(AF_INET, SOCK_STREAM, NULL, 0, 0);
	if (!ASSERT_OK_FD(server_fd, "start_server"))
		goto detach;

	/* Trigger TCP handshake which causes TCP_NEW_SYN_RECV state where
	 * is_fullsock == 0 and is_locked_tcp_sock == 0.
	 */
	client_fd = connect_to_fd(server_fd, 0);
	if (!ASSERT_OK_FD(client_fd, "connect_to_fd"))
		goto close_server;

	close(client_fd);

close_server:
	close(server_fd);
detach:
	bpf_prog_detach(cgroup_fd, BPF_CGROUP_SOCK_OPS);
}

void test_ns_sock_ops_get_sk(void)
{
	struct sock_ops_get_sk *skel;
	int cgroup_fd;

	cgroup_fd = test__join_cgroup("/sock_ops_get_sk");
	if (!ASSERT_OK_FD(cgroup_fd, "join_cgroup"))
		return;

	skel = sock_ops_get_sk__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_load"))
		goto close_cgroup;

	/* Test SOCK_OPS_GET_SK with same src/dst register */
	if (test__start_subtest("get_sk")) {
		run_sock_ops_test(cgroup_fd,
				  bpf_program__fd(skel->progs.sock_ops_get_sk_same_reg));
		ASSERT_EQ(skel->bss->null_seen, 1, "null_seen");
		ASSERT_EQ(skel->bss->bug_detected, 0, "bug_not_detected");
	}

	/* Test SOCK_OPS_GET_FIELD with same src/dst register */
	if (test__start_subtest("get_field")) {
		run_sock_ops_test(cgroup_fd,
				  bpf_program__fd(skel->progs.sock_ops_get_field_same_reg));
		ASSERT_EQ(skel->bss->field_null_seen, 1, "field_null_seen");
		ASSERT_EQ(skel->bss->field_bug_detected, 0, "field_bug_not_detected");
	}

	/* Test SOCK_OPS_GET_SK with different src/dst register */
	if (test__start_subtest("get_sk_diff_reg")) {
		run_sock_ops_test(cgroup_fd,
				  bpf_program__fd(skel->progs.sock_ops_get_sk_diff_reg));
		ASSERT_EQ(skel->bss->diff_reg_null_seen, 1, "diff_reg_null_seen");
		ASSERT_EQ(skel->bss->diff_reg_bug_detected, 0, "diff_reg_bug_not_detected");
	}

	sock_ops_get_sk__destroy(skel);
close_cgroup:
	close(cgroup_fd);
}
