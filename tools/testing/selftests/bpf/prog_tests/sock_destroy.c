// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <bpf/bpf_endian.h>

#include "sock_destroy_prog.skel.h"
#include "sock_destroy_prog_fail.skel.h"
#include "network_helpers.h"

#define TEST_NS "sock_destroy_netns"

static void start_iter_sockets(struct bpf_program *prog)
{
	struct bpf_link *link;
	char buf[50] = {};
	int iter_fd, len;

	link = bpf_program__attach_iter(prog, NULL);
	if (!ASSERT_OK_PTR(link, "attach_iter"))
		return;

	iter_fd = bpf_iter_create(bpf_link__fd(link));
	if (!ASSERT_GE(iter_fd, 0, "create_iter"))
		goto free_link;

	while ((len = read(iter_fd, buf, sizeof(buf))) > 0)
		;
	ASSERT_GE(len, 0, "read");

	close(iter_fd);

free_link:
	bpf_link__destroy(link);
}

static void test_tcp_client(struct sock_destroy_prog *skel)
{
	int serv = -1, clien = -1, accept_serv = -1, n;

	serv = start_server(AF_INET6, SOCK_STREAM, NULL, 0, 0);
	if (!ASSERT_GE(serv, 0, "start_server"))
		goto cleanup;

	clien = connect_to_fd(serv, 0);
	if (!ASSERT_GE(clien, 0, "connect_to_fd"))
		goto cleanup;

	accept_serv = accept(serv, NULL, NULL);
	if (!ASSERT_GE(accept_serv, 0, "serv accept"))
		goto cleanup;

	n = send(clien, "t", 1, 0);
	if (!ASSERT_EQ(n, 1, "client send"))
		goto cleanup;

	/* Run iterator program that destroys connected client sockets. */
	start_iter_sockets(skel->progs.iter_tcp6_client);

	n = send(clien, "t", 1, 0);
	if (!ASSERT_LT(n, 0, "client_send on destroyed socket"))
		goto cleanup;
	ASSERT_EQ(errno, ECONNABORTED, "error code on destroyed socket");

cleanup:
	if (clien != -1)
		close(clien);
	if (accept_serv != -1)
		close(accept_serv);
	if (serv != -1)
		close(serv);
}

static void test_tcp_server(struct sock_destroy_prog *skel)
{
	int serv = -1, clien = -1, accept_serv = -1, n, serv_port;

	serv = start_server(AF_INET6, SOCK_STREAM, NULL, 0, 0);
	if (!ASSERT_GE(serv, 0, "start_server"))
		goto cleanup;
	serv_port = get_socket_local_port(serv);
	if (!ASSERT_GE(serv_port, 0, "get_sock_local_port"))
		goto cleanup;
	skel->bss->serv_port = (__be16) serv_port;

	clien = connect_to_fd(serv, 0);
	if (!ASSERT_GE(clien, 0, "connect_to_fd"))
		goto cleanup;

	accept_serv = accept(serv, NULL, NULL);
	if (!ASSERT_GE(accept_serv, 0, "serv accept"))
		goto cleanup;

	n = send(clien, "t", 1, 0);
	if (!ASSERT_EQ(n, 1, "client send"))
		goto cleanup;

	/* Run iterator program that destroys server sockets. */
	start_iter_sockets(skel->progs.iter_tcp6_server);

	n = send(clien, "t", 1, 0);
	if (!ASSERT_LT(n, 0, "client_send on destroyed socket"))
		goto cleanup;
	ASSERT_EQ(errno, ECONNRESET, "error code on destroyed socket");

cleanup:
	if (clien != -1)
		close(clien);
	if (accept_serv != -1)
		close(accept_serv);
	if (serv != -1)
		close(serv);
}

static void test_udp_client(struct sock_destroy_prog *skel)
{
	int serv = -1, clien = -1, n = 0;

	serv = start_server(AF_INET6, SOCK_DGRAM, NULL, 0, 0);
	if (!ASSERT_GE(serv, 0, "start_server"))
		goto cleanup;

	clien = connect_to_fd(serv, 0);
	if (!ASSERT_GE(clien, 0, "connect_to_fd"))
		goto cleanup;

	n = send(clien, "t", 1, 0);
	if (!ASSERT_EQ(n, 1, "client send"))
		goto cleanup;

	/* Run iterator program that destroys sockets. */
	start_iter_sockets(skel->progs.iter_udp6_client);

	n = send(clien, "t", 1, 0);
	if (!ASSERT_LT(n, 0, "client_send on destroyed socket"))
		goto cleanup;
	/* UDP sockets have an overriding error code after they are disconnected,
	 * so we don't check for ECONNABORTED error code.
	 */

cleanup:
	if (clien != -1)
		close(clien);
	if (serv != -1)
		close(serv);
}

static void test_udp_server(struct sock_destroy_prog *skel)
{
	int *listen_fds = NULL, n, i, serv_port;
	unsigned int num_listens = 5;
	char buf[1];

	/* Start reuseport servers. */
	listen_fds = start_reuseport_server(AF_INET6, SOCK_DGRAM,
					    "::1", 0, 0, num_listens);
	if (!ASSERT_OK_PTR(listen_fds, "start_reuseport_server"))
		goto cleanup;
	serv_port = get_socket_local_port(listen_fds[0]);
	if (!ASSERT_GE(serv_port, 0, "get_sock_local_port"))
		goto cleanup;
	skel->bss->serv_port = (__be16) serv_port;

	/* Run iterator program that destroys server sockets. */
	start_iter_sockets(skel->progs.iter_udp6_server);

	for (i = 0; i < num_listens; ++i) {
		n = read(listen_fds[i], buf, sizeof(buf));
		if (!ASSERT_EQ(n, -1, "read") ||
		    !ASSERT_EQ(errno, ECONNABORTED, "error code on destroyed socket"))
			break;
	}
	ASSERT_EQ(i, num_listens, "server socket");

cleanup:
	free_fds(listen_fds, num_listens);
}

void test_sock_destroy(void)
{
	struct sock_destroy_prog *skel;
	struct nstoken *nstoken = NULL;
	int cgroup_fd;

	skel = sock_destroy_prog__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	cgroup_fd = test__join_cgroup("/sock_destroy");
	if (!ASSERT_GE(cgroup_fd, 0, "join_cgroup"))
		goto cleanup;

	skel->links.sock_connect = bpf_program__attach_cgroup(
		skel->progs.sock_connect, cgroup_fd);
	if (!ASSERT_OK_PTR(skel->links.sock_connect, "prog_attach"))
		goto cleanup;

	SYS(cleanup, "ip netns add %s", TEST_NS);
	SYS(cleanup, "ip -net %s link set dev lo up", TEST_NS);

	nstoken = open_netns(TEST_NS);
	if (!ASSERT_OK_PTR(nstoken, "open_netns"))
		goto cleanup;

	if (test__start_subtest("tcp_client"))
		test_tcp_client(skel);
	if (test__start_subtest("tcp_server"))
		test_tcp_server(skel);
	if (test__start_subtest("udp_client"))
		test_udp_client(skel);
	if (test__start_subtest("udp_server"))
		test_udp_server(skel);

	RUN_TESTS(sock_destroy_prog_fail);

cleanup:
	if (nstoken)
		close_netns(nstoken);
	SYS_NOFAIL("ip netns del " TEST_NS " &> /dev/null");
	if (cgroup_fd >= 0)
		close(cgroup_fd);
	sock_destroy_prog__destroy(skel);
}
