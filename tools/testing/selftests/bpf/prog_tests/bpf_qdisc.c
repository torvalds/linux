// SPDX-License-Identifier: GPL-2.0

#include <linux/pkt_sched.h>
#include <linux/rtnetlink.h>
#include <test_progs.h>

#include "network_helpers.h"
#include "bpf_qdisc_fifo.skel.h"
#include "bpf_qdisc_fq.skel.h"

#define LO_IFINDEX 1

static const unsigned int total_bytes = 10 * 1024 * 1024;

static void do_test(char *qdisc)
{
	DECLARE_LIBBPF_OPTS(bpf_tc_hook, hook, .ifindex = LO_IFINDEX,
			    .attach_point = BPF_TC_QDISC,
			    .parent = TC_H_ROOT,
			    .handle = 0x8000000,
			    .qdisc = qdisc);
	int srv_fd = -1, cli_fd = -1;
	int err;

	err = bpf_tc_hook_create(&hook);
	if (!ASSERT_OK(err, "attach qdisc"))
		return;

	srv_fd = start_server(AF_INET6, SOCK_STREAM, NULL, 0, 0);
	if (!ASSERT_OK_FD(srv_fd, "start server"))
		goto done;

	cli_fd = connect_to_fd(srv_fd, 0);
	if (!ASSERT_OK_FD(cli_fd, "connect to client"))
		goto done;

	err = send_recv_data(srv_fd, cli_fd, total_bytes);
	ASSERT_OK(err, "send_recv_data");

done:
	if (srv_fd != -1)
		close(srv_fd);
	if (cli_fd != -1)
		close(cli_fd);

	bpf_tc_hook_destroy(&hook);
}

static void test_fifo(void)
{
	struct bpf_qdisc_fifo *fifo_skel;
	struct bpf_link *link;

	fifo_skel = bpf_qdisc_fifo__open_and_load();
	if (!ASSERT_OK_PTR(fifo_skel, "bpf_qdisc_fifo__open_and_load"))
		return;

	link = bpf_map__attach_struct_ops(fifo_skel->maps.fifo);
	if (!ASSERT_OK_PTR(link, "bpf_map__attach_struct_ops")) {
		bpf_qdisc_fifo__destroy(fifo_skel);
		return;
	}

	do_test("bpf_fifo");

	bpf_link__destroy(link);
	bpf_qdisc_fifo__destroy(fifo_skel);
}

static void test_fq(void)
{
	struct bpf_qdisc_fq *fq_skel;
	struct bpf_link *link;

	fq_skel = bpf_qdisc_fq__open_and_load();
	if (!ASSERT_OK_PTR(fq_skel, "bpf_qdisc_fq__open_and_load"))
		return;

	link = bpf_map__attach_struct_ops(fq_skel->maps.fq);
	if (!ASSERT_OK_PTR(link, "bpf_map__attach_struct_ops")) {
		bpf_qdisc_fq__destroy(fq_skel);
		return;
	}

	do_test("bpf_fq");

	bpf_link__destroy(link);
	bpf_qdisc_fq__destroy(fq_skel);
}

void test_bpf_qdisc(void)
{
	struct netns_obj *netns;

	netns = netns_new("bpf_qdisc_ns", true);
	if (!ASSERT_OK_PTR(netns, "netns_new"))
		return;

	if (test__start_subtest("fifo"))
		test_fifo();
	if (test__start_subtest("fq"))
		test_fq();

	netns_free(netns);
}
