// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Isovalent */

#include "test_progs.h"
#include "network_helpers.h"
#include "ksock_sendto.skel.h"

struct sendto_test_case {
	const char *name;
	bool connected;
	bool use_sendto_addr;
};

static bool recv_payload(int fd, const void *expected, size_t len)
{
	char buf[64];
	ssize_t n;

	n = recv(fd, buf, sizeof(buf), MSG_TRUNC);
	if (!ASSERT_EQ(n, len, "recv length"))
		return false;
	return ASSERT_MEMEQ(buf, expected, len, "recv payload");
}

static void test_sendto_case(const struct sendto_test_case *test)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	struct ksock_sendto *skel;
	int receivers[2] = { -1, -1 };
	int err, i, port, sendto_fd;
	char buf[64];
	ssize_t n;

	skel = ksock_sendto__open_and_load();
	if (!ASSERT_OK_PTR(skel, "ksock_sendto open and load"))
		return;

	/* Use separate receivers for the default peer and explicit destination. */
	for (i = 0; i < ARRAY_SIZE(receivers); i++) {
		receivers[i] = start_server(AF_INET, SOCK_DGRAM, "127.0.0.1", 0, 1000);
		if (!ASSERT_OK_FD(receivers[i], "receiver socket"))
			goto out;
	}

	port = get_socket_local_port(receivers[0]);
	if (!ASSERT_GE(port, 0, "connected port"))
		goto out;
	skel->bss->connected_port = port;
	port = get_socket_local_port(receivers[1]);
	if (!ASSERT_GE(port, 0, "sendto port"))
		goto out;
	skel->bss->sendto_port = port;
	skel->bss->connected = test->connected;
	skel->bss->use_sendto_addr = test->use_sendto_addr;

	err = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.test_sendto), &opts);
	if (!ASSERT_OK(err, "run test_sendto") || !ASSERT_OK(opts.retval, "test_sendto retval"))
		goto out;

	/* Check sendto() outcome */
	if (!ASSERT_EQ(skel->data->sendto_ret, sizeof(skel->data->sendto_data), "sendto bytes"))
		goto out;
	sendto_fd = receivers[test->use_sendto_addr ? 1 : 0];
	if (!recv_payload(sendto_fd, skel->data->sendto_data, sizeof(skel->data->sendto_data)))
		goto out;

	/* Check send() outcome */
	if (test->connected) {
		if (!ASSERT_EQ(skel->data->send_ret, sizeof(skel->data->send_data), "send bytes"))
			goto out;
		if (!recv_payload(receivers[0], skel->data->send_data,
				  sizeof(skel->data->send_data)))
			goto out;
	} else if (!ASSERT_EQ(skel->data->send_ret, -EDESTADDRREQ,
			      "send after sendto remains unconnected")) {
		goto out;
	}

	/* Catch unexpected deliveries to either destination. */
	for (i = 0; i < ARRAY_SIZE(receivers); i++) {
		n = recv(receivers[i], buf, sizeof(buf), MSG_DONTWAIT);
		if (!ASSERT_EQ(n, -1, "no extra datagram") ||
		    !ASSERT_EQ(errno, EAGAIN, "recv errno"))
			goto out;
	}

out:
	for (i = 0; i < ARRAY_SIZE(receivers); i++) {
		if (receivers[i] >= 0)
			close(receivers[i]);
	}
	ksock_sendto__destroy(skel);
}

void test_ksock_sendto(void)
{
	static const struct sendto_test_case tests[] = {
		{ .name = "unconnected", .use_sendto_addr = true },
		{ .name = "connected_null_addr", .connected = true },
		{ .name = "connected_explicit_addr", .connected = true, .use_sendto_addr = true },
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		if (test__start_subtest(tests[i].name))
			test_sendto_case(&tests[i]);
	}
}
