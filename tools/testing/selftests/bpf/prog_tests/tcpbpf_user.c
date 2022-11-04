// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <network_helpers.h>

#include "test_tcpbpf.h"
#include "test_tcpbpf_kern.skel.h"

#define LO_ADDR6 "::1"
#define CG_NAME "/tcpbpf-user-test"

static void verify_result(struct tcpbpf_globals *result)
{
	__u32 expected_events = ((1 << BPF_SOCK_OPS_TIMEOUT_INIT) |
				 (1 << BPF_SOCK_OPS_RWND_INIT) |
				 (1 << BPF_SOCK_OPS_TCP_CONNECT_CB) |
				 (1 << BPF_SOCK_OPS_ACTIVE_ESTABLISHED_CB) |
				 (1 << BPF_SOCK_OPS_PASSIVE_ESTABLISHED_CB) |
				 (1 << BPF_SOCK_OPS_NEEDS_ECN) |
				 (1 << BPF_SOCK_OPS_STATE_CB) |
				 (1 << BPF_SOCK_OPS_TCP_LISTEN_CB));

	/* check global map */
	ASSERT_EQ(expected_events, result->event_map, "event_map");

	ASSERT_EQ(result->bytes_received, 501, "bytes_received");
	ASSERT_EQ(result->bytes_acked, 1002, "bytes_acked");
	ASSERT_EQ(result->data_segs_in, 1, "data_segs_in");
	ASSERT_EQ(result->data_segs_out, 1, "data_segs_out");
	ASSERT_EQ(result->bad_cb_test_rv, 0x80, "bad_cb_test_rv");
	ASSERT_EQ(result->good_cb_test_rv, 0, "good_cb_test_rv");
	ASSERT_EQ(result->num_listen, 1, "num_listen");

	/* 3 comes from one listening socket + both ends of the connection */
	ASSERT_EQ(result->num_close_events, 3, "num_close_events");

	/* check setsockopt for SAVE_SYN */
	ASSERT_EQ(result->tcp_save_syn, 0, "tcp_save_syn");

	/* check getsockopt for SAVED_SYN */
	ASSERT_EQ(result->tcp_saved_syn, 1, "tcp_saved_syn");

	/* check getsockopt for window_clamp */
	ASSERT_EQ(result->window_clamp_client, 9216, "window_clamp_client");
	ASSERT_EQ(result->window_clamp_server, 9216, "window_clamp_server");
}

static void run_test(struct tcpbpf_globals *result)
{
	int listen_fd = -1, cli_fd = -1, accept_fd = -1;
	char buf[1000];
	int err = -1;
	int i, rv;

	listen_fd = start_server(AF_INET6, SOCK_STREAM, LO_ADDR6, 0, 0);
	if (!ASSERT_NEQ(listen_fd, -1, "start_server"))
		goto done;

	cli_fd = connect_to_fd(listen_fd, 0);
	if (!ASSERT_NEQ(cli_fd, -1, "connect_to_fd(listen_fd)"))
		goto done;

	accept_fd = accept(listen_fd, NULL, NULL);
	if (!ASSERT_NEQ(accept_fd, -1, "accept(listen_fd)"))
		goto done;

	/* Send 1000B of '+'s from cli_fd -> accept_fd */
	for (i = 0; i < 1000; i++)
		buf[i] = '+';

	rv = send(cli_fd, buf, 1000, 0);
	if (!ASSERT_EQ(rv, 1000, "send(cli_fd)"))
		goto done;

	rv = recv(accept_fd, buf, 1000, 0);
	if (!ASSERT_EQ(rv, 1000, "recv(accept_fd)"))
		goto done;

	/* Send 500B of '.'s from accept_fd ->cli_fd */
	for (i = 0; i < 500; i++)
		buf[i] = '.';

	rv = send(accept_fd, buf, 500, 0);
	if (!ASSERT_EQ(rv, 500, "send(accept_fd)"))
		goto done;

	rv = recv(cli_fd, buf, 500, 0);
	if (!ASSERT_EQ(rv, 500, "recv(cli_fd)"))
		goto done;

	/*
	 * shutdown accept first to guarantee correct ordering for
	 * bytes_received and bytes_acked when we go to verify the results.
	 */
	shutdown(accept_fd, SHUT_WR);
	err = recv(cli_fd, buf, 1, 0);
	if (!ASSERT_OK(err, "recv(cli_fd) for fin"))
		goto done;

	shutdown(cli_fd, SHUT_WR);
	err = recv(accept_fd, buf, 1, 0);
	ASSERT_OK(err, "recv(accept_fd) for fin");
done:
	if (accept_fd != -1)
		close(accept_fd);
	if (cli_fd != -1)
		close(cli_fd);
	if (listen_fd != -1)
		close(listen_fd);

	if (!err)
		verify_result(result);
}

void test_tcpbpf_user(void)
{
	struct test_tcpbpf_kern *skel;
	int cg_fd = -1;

	skel = test_tcpbpf_kern__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open and load skel"))
		return;

	cg_fd = test__join_cgroup(CG_NAME);
	if (!ASSERT_GE(cg_fd, 0, "test__join_cgroup(" CG_NAME ")"))
		goto err;

	skel->links.bpf_testcb = bpf_program__attach_cgroup(skel->progs.bpf_testcb, cg_fd);
	if (!ASSERT_OK_PTR(skel->links.bpf_testcb, "attach_cgroup(bpf_testcb)"))
		goto err;

	run_test(&skel->bss->global);

err:
	if (cg_fd != -1)
		close(cg_fd);
	test_tcpbpf_kern__destroy(skel);
}
