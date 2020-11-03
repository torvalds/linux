// SPDX-License-Identifier: GPL-2.0
#include <inttypes.h>
#include <test_progs.h>
#include <network_helpers.h>

#include "test_tcpbpf.h"

#define LO_ADDR6 "::1"
#define CG_NAME "/tcpbpf-user-test"

static __u32 duration;

#define EXPECT_EQ(expected, actual, fmt)			\
	do {							\
		if ((expected) != (actual)) {			\
			printf("  Value of: " #actual "\n"	\
			       "    Actual: %" fmt "\n"		\
			       "  Expected: %" fmt "\n",	\
			       (actual), (expected));		\
			ret--;					\
		}						\
	} while (0)

int verify_result(const struct tcpbpf_globals *result)
{
	__u32 expected_events;
	int ret = 0;

	expected_events = ((1 << BPF_SOCK_OPS_TIMEOUT_INIT) |
			   (1 << BPF_SOCK_OPS_RWND_INIT) |
			   (1 << BPF_SOCK_OPS_TCP_CONNECT_CB) |
			   (1 << BPF_SOCK_OPS_ACTIVE_ESTABLISHED_CB) |
			   (1 << BPF_SOCK_OPS_PASSIVE_ESTABLISHED_CB) |
			   (1 << BPF_SOCK_OPS_NEEDS_ECN) |
			   (1 << BPF_SOCK_OPS_STATE_CB) |
			   (1 << BPF_SOCK_OPS_TCP_LISTEN_CB));

	EXPECT_EQ(expected_events, result->event_map, "#" PRIx32);
	EXPECT_EQ(501ULL, result->bytes_received, "llu");
	EXPECT_EQ(1002ULL, result->bytes_acked, "llu");
	EXPECT_EQ(1, result->data_segs_in, PRIu32);
	EXPECT_EQ(1, result->data_segs_out, PRIu32);
	EXPECT_EQ(0x80, result->bad_cb_test_rv, PRIu32);
	EXPECT_EQ(0, result->good_cb_test_rv, PRIu32);
	EXPECT_EQ(1, result->num_listen, PRIu32);

	/* 3 comes from one listening socket + both ends of the connection */
	EXPECT_EQ(3, result->num_close_events, PRIu32);

	return ret;
}

int verify_sockopt_result(int sock_map_fd)
{
	__u32 key = 0;
	int ret = 0;
	int res;
	int rv;

	/* check setsockopt for SAVE_SYN */
	rv = bpf_map_lookup_elem(sock_map_fd, &key, &res);
	EXPECT_EQ(0, rv, "d");
	EXPECT_EQ(0, res, "d");
	key = 1;
	/* check getsockopt for SAVED_SYN */
	rv = bpf_map_lookup_elem(sock_map_fd, &key, &res);
	EXPECT_EQ(0, rv, "d");
	EXPECT_EQ(1, res, "d");
	return ret;
}

static int run_test(void)
{
	int listen_fd = -1, cli_fd = -1, accept_fd = -1;
	char buf[1000];
	int err = -1;
	int i, rv;

	listen_fd = start_server(AF_INET6, SOCK_STREAM, LO_ADDR6, 0, 0);
	if (CHECK(listen_fd == -1, "start_server", "listen_fd:%d errno:%d\n",
		  listen_fd, errno))
		goto done;

	cli_fd = connect_to_fd(listen_fd, 0);
	if (CHECK(cli_fd == -1, "connect_to_fd(listen_fd)",
		  "cli_fd:%d errno:%d\n", cli_fd, errno))
		goto done;

	accept_fd = accept(listen_fd, NULL, NULL);
	if (CHECK(accept_fd == -1, "accept(listen_fd)",
		  "accept_fd:%d errno:%d\n", accept_fd, errno))
		goto done;

	/* Send 1000B of '+'s from cli_fd -> accept_fd */
	for (i = 0; i < 1000; i++)
		buf[i] = '+';

	rv = send(cli_fd, buf, 1000, 0);
	if (CHECK(rv != 1000, "send(cli_fd)", "rv:%d errno:%d\n", rv, errno))
		goto done;

	rv = recv(accept_fd, buf, 1000, 0);
	if (CHECK(rv != 1000, "recv(accept_fd)", "rv:%d errno:%d\n", rv, errno))
		goto done;

	/* Send 500B of '.'s from accept_fd ->cli_fd */
	for (i = 0; i < 500; i++)
		buf[i] = '.';

	rv = send(accept_fd, buf, 500, 0);
	if (CHECK(rv != 500, "send(accept_fd)", "rv:%d errno:%d\n", rv, errno))
		goto done;

	rv = recv(cli_fd, buf, 500, 0);
	if (CHECK(rv != 500, "recv(cli_fd)", "rv:%d errno:%d\n", rv, errno))
		goto done;

	/*
	 * shutdown accept first to guarantee correct ordering for
	 * bytes_received and bytes_acked when we go to verify the results.
	 */
	shutdown(accept_fd, SHUT_WR);
	err = recv(cli_fd, buf, 1, 0);
	if (CHECK(err, "recv(cli_fd) for fin", "err:%d errno:%d\n", err, errno))
		goto done;

	shutdown(cli_fd, SHUT_WR);
	err = recv(accept_fd, buf, 1, 0);
	CHECK(err, "recv(accept_fd) for fin", "err:%d errno:%d\n", err, errno);
done:
	if (accept_fd != -1)
		close(accept_fd);
	if (cli_fd != -1)
		close(cli_fd);
	if (listen_fd != -1)
		close(listen_fd);

	return err;
}

void test_tcpbpf_user(void)
{
	const char *file = "test_tcpbpf_kern.o";
	int prog_fd, map_fd, sock_map_fd;
	struct tcpbpf_globals g = {0};
	int error = EXIT_FAILURE;
	struct bpf_object *obj;
	int cg_fd = -1;
	__u32 key = 0;
	int rv;

	cg_fd = test__join_cgroup(CG_NAME);
	if (cg_fd < 0)
		goto err;

	if (bpf_prog_load(file, BPF_PROG_TYPE_SOCK_OPS, &obj, &prog_fd)) {
		printf("FAILED: load_bpf_file failed for: %s\n", file);
		goto err;
	}

	rv = bpf_prog_attach(prog_fd, cg_fd, BPF_CGROUP_SOCK_OPS, 0);
	if (rv) {
		printf("FAILED: bpf_prog_attach: %d (%s)\n",
		       error, strerror(errno));
		goto err;
	}

	map_fd = bpf_find_map(__func__, obj, "global_map");
	if (map_fd < 0)
		goto err;

	sock_map_fd = bpf_find_map(__func__, obj, "sockopt_results");
	if (sock_map_fd < 0)
		goto err;

	if (run_test())
		goto err;

	rv = bpf_map_lookup_elem(map_fd, &key, &g);
	if (rv != 0) {
		printf("FAILED: bpf_map_lookup_elem returns %d\n", rv);
		goto err;
	}

	if (verify_result(&g)) {
		printf("FAILED: Wrong stats\n");
		goto err;
	}

	if (verify_sockopt_result(sock_map_fd)) {
		printf("FAILED: Wrong sockopt stats\n");
		goto err;
	}

	error = 0;
err:
	bpf_prog_detach(cg_fd, BPF_CGROUP_SOCK_OPS);
	if (cg_fd != -1)
		close(cg_fd);

	CHECK_FAIL(error);
}
