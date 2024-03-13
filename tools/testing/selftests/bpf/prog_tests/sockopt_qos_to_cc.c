// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include <test_progs.h>
#include <netinet/tcp.h>
#include "sockopt_qos_to_cc.skel.h"

static void run_setsockopt_test(int cg_fd, int sock_fd)
{
	socklen_t optlen;
	char cc[16]; /* TCP_CA_NAME_MAX */
	int buf;
	int err = -1;

	buf = 0x2D;
	err = setsockopt(sock_fd, SOL_IPV6, IPV6_TCLASS, &buf, sizeof(buf));
	if (!ASSERT_OK(err, "setsockopt(sock_fd, IPV6_TCLASS)"))
		return;

	/* Verify the setsockopt cc change */
	optlen = sizeof(cc);
	err = getsockopt(sock_fd, SOL_TCP, TCP_CONGESTION, cc, &optlen);
	if (!ASSERT_OK(err, "getsockopt(sock_fd, TCP_CONGESTION)"))
		return;

	if (!ASSERT_STREQ(cc, "reno", "getsockopt(sock_fd, TCP_CONGESTION)"))
		return;
}

void test_sockopt_qos_to_cc(void)
{
	struct sockopt_qos_to_cc *skel;
	char cc_cubic[16] = "cubic"; /* TCP_CA_NAME_MAX */
	int cg_fd = -1;
	int sock_fd = -1;
	int err;

	cg_fd = test__join_cgroup("/sockopt_qos_to_cc");
	if (!ASSERT_GE(cg_fd, 0, "cg-join(sockopt_qos_to_cc)"))
		return;

	skel = sockopt_qos_to_cc__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel"))
		goto done;

	sock_fd = socket(AF_INET6, SOCK_STREAM, 0);
	if (!ASSERT_GE(sock_fd, 0, "v6 socket open"))
		goto done;

	err = setsockopt(sock_fd, SOL_TCP, TCP_CONGESTION, &cc_cubic,
			 sizeof(cc_cubic));
	if (!ASSERT_OK(err, "setsockopt(sock_fd, TCP_CONGESTION)"))
		goto done;

	skel->links.sockopt_qos_to_cc =
		bpf_program__attach_cgroup(skel->progs.sockopt_qos_to_cc,
					   cg_fd);
	if (!ASSERT_OK_PTR(skel->links.sockopt_qos_to_cc,
			   "prog_attach(sockopt_qos_to_cc)"))
		goto done;

	run_setsockopt_test(cg_fd, sock_fd);

done:
	if (sock_fd != -1)
		close(sock_fd);
	if (cg_fd != -1)
		close(cg_fd);
	/* destroy can take null and error pointer */
	sockopt_qos_to_cc__destroy(skel);
}
