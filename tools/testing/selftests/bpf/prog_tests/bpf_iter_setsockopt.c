// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#define _GNU_SOURCE
#include <sched.h>
#include <test_progs.h>
#include "network_helpers.h"
#include "bpf_dctcp.skel.h"
#include "bpf_cubic.skel.h"
#include "bpf_iter_setsockopt.skel.h"

static int create_netns(void)
{
	if (!ASSERT_OK(unshare(CLONE_NEWNET), "create netns"))
		return -1;

	if (!ASSERT_OK(system("ip link set dev lo up"), "bring up lo"))
		return -1;

	return 0;
}

static unsigned int set_bpf_cubic(int *fds, unsigned int nr_fds)
{
	unsigned int i;

	for (i = 0; i < nr_fds; i++) {
		if (setsockopt(fds[i], SOL_TCP, TCP_CONGESTION, "bpf_cubic",
			       sizeof("bpf_cubic")))
			return i;
	}

	return nr_fds;
}

static unsigned int check_bpf_dctcp(int *fds, unsigned int nr_fds)
{
	char tcp_cc[16];
	socklen_t optlen = sizeof(tcp_cc);
	unsigned int i;

	for (i = 0; i < nr_fds; i++) {
		if (getsockopt(fds[i], SOL_TCP, TCP_CONGESTION,
			       tcp_cc, &optlen) ||
		    strcmp(tcp_cc, "bpf_dctcp"))
			return i;
	}

	return nr_fds;
}

static int *make_established(int listen_fd, unsigned int nr_est,
			     int **paccepted_fds)
{
	int *est_fds, *accepted_fds;
	unsigned int i;

	est_fds = malloc(sizeof(*est_fds) * nr_est);
	if (!est_fds)
		return NULL;

	accepted_fds = malloc(sizeof(*accepted_fds) * nr_est);
	if (!accepted_fds) {
		free(est_fds);
		return NULL;
	}

	for (i = 0; i < nr_est; i++) {
		est_fds[i] = connect_to_fd(listen_fd, 0);
		if (est_fds[i] == -1)
			break;
		if (set_bpf_cubic(&est_fds[i], 1) != 1) {
			close(est_fds[i]);
			break;
		}

		accepted_fds[i] = accept(listen_fd, NULL, 0);
		if (accepted_fds[i] == -1) {
			close(est_fds[i]);
			break;
		}
	}

	if (!ASSERT_EQ(i, nr_est, "create established fds")) {
		free_fds(accepted_fds, i);
		free_fds(est_fds, i);
		return NULL;
	}

	*paccepted_fds = accepted_fds;
	return est_fds;
}

static unsigned short get_local_port(int fd)
{
	struct sockaddr_in6 addr;
	socklen_t addrlen = sizeof(addr);

	if (!getsockname(fd, &addr, &addrlen))
		return ntohs(addr.sin6_port);

	return 0;
}

static void do_bpf_iter_setsockopt(struct bpf_iter_setsockopt *iter_skel,
				   bool random_retry)
{
	int *reuse_listen_fds = NULL, *accepted_fds = NULL, *est_fds = NULL;
	unsigned int nr_reuse_listens = 256, nr_est = 256;
	int err, iter_fd = -1, listen_fd = -1;
	char buf;

	/* Prepare non-reuseport listen_fd */
	listen_fd = start_server(AF_INET6, SOCK_STREAM, "::1", 0, 0);
	if (!ASSERT_GE(listen_fd, 0, "start_server"))
		return;
	if (!ASSERT_EQ(set_bpf_cubic(&listen_fd, 1), 1,
		       "set listen_fd to cubic"))
		goto done;
	iter_skel->bss->listen_hport = get_local_port(listen_fd);
	if (!ASSERT_NEQ(iter_skel->bss->listen_hport, 0,
			"get_local_port(listen_fd)"))
		goto done;

	/* Connect to non-reuseport listen_fd */
	est_fds = make_established(listen_fd, nr_est, &accepted_fds);
	if (!ASSERT_OK_PTR(est_fds, "create established"))
		goto done;

	/* Prepare reuseport listen fds */
	reuse_listen_fds = start_reuseport_server(AF_INET6, SOCK_STREAM,
						  "::1", 0, 0,
						  nr_reuse_listens);
	if (!ASSERT_OK_PTR(reuse_listen_fds, "start_reuseport_server"))
		goto done;
	if (!ASSERT_EQ(set_bpf_cubic(reuse_listen_fds, nr_reuse_listens),
		       nr_reuse_listens, "set reuse_listen_fds to cubic"))
		goto done;
	iter_skel->bss->reuse_listen_hport = get_local_port(reuse_listen_fds[0]);
	if (!ASSERT_NEQ(iter_skel->bss->reuse_listen_hport, 0,
			"get_local_port(reuse_listen_fds[0])"))
		goto done;

	/* Run bpf tcp iter to switch from bpf_cubic to bpf_dctcp */
	iter_skel->bss->random_retry = random_retry;
	iter_fd = bpf_iter_create(bpf_link__fd(iter_skel->links.change_tcp_cc));
	if (!ASSERT_GE(iter_fd, 0, "create iter_fd"))
		goto done;

	while ((err = read(iter_fd, &buf, sizeof(buf))) == -1 &&
	       errno == EAGAIN)
		;
	if (!ASSERT_OK(err, "read iter error"))
		goto done;

	/* Check reuseport listen fds for dctcp */
	ASSERT_EQ(check_bpf_dctcp(reuse_listen_fds, nr_reuse_listens),
		  nr_reuse_listens,
		  "check reuse_listen_fds dctcp");

	/* Check non reuseport listen fd for dctcp */
	ASSERT_EQ(check_bpf_dctcp(&listen_fd, 1), 1,
		  "check listen_fd dctcp");

	/* Check established fds for dctcp */
	ASSERT_EQ(check_bpf_dctcp(est_fds, nr_est), nr_est,
		  "check est_fds dctcp");

	/* Check accepted fds for dctcp */
	ASSERT_EQ(check_bpf_dctcp(accepted_fds, nr_est), nr_est,
		  "check accepted_fds dctcp");

done:
	if (iter_fd != -1)
		close(iter_fd);
	if (listen_fd != -1)
		close(listen_fd);
	free_fds(reuse_listen_fds, nr_reuse_listens);
	free_fds(accepted_fds, nr_est);
	free_fds(est_fds, nr_est);
}

void test_bpf_iter_setsockopt(void)
{
	struct bpf_iter_setsockopt *iter_skel = NULL;
	struct bpf_cubic *cubic_skel = NULL;
	struct bpf_dctcp *dctcp_skel = NULL;
	struct bpf_link *cubic_link = NULL;
	struct bpf_link *dctcp_link = NULL;

	if (create_netns())
		return;

	/* Load iter_skel */
	iter_skel = bpf_iter_setsockopt__open_and_load();
	if (!ASSERT_OK_PTR(iter_skel, "iter_skel"))
		return;
	iter_skel->links.change_tcp_cc = bpf_program__attach_iter(iter_skel->progs.change_tcp_cc, NULL);
	if (!ASSERT_OK_PTR(iter_skel->links.change_tcp_cc, "attach iter"))
		goto done;

	/* Load bpf_cubic */
	cubic_skel = bpf_cubic__open_and_load();
	if (!ASSERT_OK_PTR(cubic_skel, "cubic_skel"))
		goto done;
	cubic_link = bpf_map__attach_struct_ops(cubic_skel->maps.cubic);
	if (!ASSERT_OK_PTR(cubic_link, "cubic_link"))
		goto done;

	/* Load bpf_dctcp */
	dctcp_skel = bpf_dctcp__open_and_load();
	if (!ASSERT_OK_PTR(dctcp_skel, "dctcp_skel"))
		goto done;
	dctcp_link = bpf_map__attach_struct_ops(dctcp_skel->maps.dctcp);
	if (!ASSERT_OK_PTR(dctcp_link, "dctcp_link"))
		goto done;

	do_bpf_iter_setsockopt(iter_skel, true);
	do_bpf_iter_setsockopt(iter_skel, false);

done:
	bpf_link__destroy(cubic_link);
	bpf_link__destroy(dctcp_link);
	bpf_cubic__destroy(cubic_skel);
	bpf_dctcp__destroy(dctcp_skel);
	bpf_iter_setsockopt__destroy(iter_skel);
}
