// SPDX-License-Identifier: GPL-2.0

#include <linux/pkt_sched.h>
#include <linux/rtnetlink.h>
#include <test_progs.h>

#include "network_helpers.h"
#include "bpf_qdisc_fifo.skel.h"
#include "bpf_qdisc_fq.skel.h"
#include "bpf_qdisc_fail__incompl_ops.skel.h"

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

	fifo_skel = bpf_qdisc_fifo__open_and_load();
	if (!ASSERT_OK_PTR(fifo_skel, "bpf_qdisc_fifo__open_and_load"))
		return;

	if (!ASSERT_OK(bpf_qdisc_fifo__attach(fifo_skel), "bpf_qdisc_fifo__attach"))
		goto out;

	do_test("bpf_fifo");
out:
	bpf_qdisc_fifo__destroy(fifo_skel);
}

static void test_fq(void)
{
	struct bpf_qdisc_fq *fq_skel;

	fq_skel = bpf_qdisc_fq__open_and_load();
	if (!ASSERT_OK_PTR(fq_skel, "bpf_qdisc_fq__open_and_load"))
		return;

	if (!ASSERT_OK(bpf_qdisc_fq__attach(fq_skel), "bpf_qdisc_fq__attach"))
		goto out;

	do_test("bpf_fq");
out:
	bpf_qdisc_fq__destroy(fq_skel);
}

static void test_qdisc_attach_to_mq(void)
{
	DECLARE_LIBBPF_OPTS(bpf_tc_hook, hook,
			    .attach_point = BPF_TC_QDISC,
			    .parent = TC_H_MAKE(1 << 16, 1),
			    .handle = 0x11 << 16,
			    .qdisc = "bpf_fifo");
	struct bpf_qdisc_fifo *fifo_skel;
	int err;

	fifo_skel = bpf_qdisc_fifo__open_and_load();
	if (!ASSERT_OK_PTR(fifo_skel, "bpf_qdisc_fifo__open_and_load"))
		return;

	if (!ASSERT_OK(bpf_qdisc_fifo__attach(fifo_skel), "bpf_qdisc_fifo__attach"))
		goto out;

	SYS(out, "ip link add veth0 type veth peer veth1");
	hook.ifindex = if_nametoindex("veth0");
	SYS(out, "tc qdisc add dev veth0 root handle 1: mq");

	err = bpf_tc_hook_create(&hook);
	ASSERT_OK(err, "attach qdisc");

	bpf_tc_hook_destroy(&hook);

	SYS(out, "tc qdisc delete dev veth0 root mq");
out:
	bpf_qdisc_fifo__destroy(fifo_skel);
}

static void test_qdisc_attach_to_non_root(void)
{
	DECLARE_LIBBPF_OPTS(bpf_tc_hook, hook, .ifindex = LO_IFINDEX,
			    .attach_point = BPF_TC_QDISC,
			    .parent = TC_H_MAKE(1 << 16, 1),
			    .handle = 0x11 << 16,
			    .qdisc = "bpf_fifo");
	struct bpf_qdisc_fifo *fifo_skel;
	int err;

	fifo_skel = bpf_qdisc_fifo__open_and_load();
	if (!ASSERT_OK_PTR(fifo_skel, "bpf_qdisc_fifo__open_and_load"))
		return;

	if (!ASSERT_OK(bpf_qdisc_fifo__attach(fifo_skel), "bpf_qdisc_fifo__attach"))
		goto out;

	SYS(out, "tc qdisc add dev lo root handle 1: htb");
	SYS(out_del_htb, "tc class add dev lo parent 1: classid 1:1 htb rate 75Kbit");

	err = bpf_tc_hook_create(&hook);
	if (!ASSERT_ERR(err, "attach qdisc"))
		bpf_tc_hook_destroy(&hook);

out_del_htb:
	SYS(out, "tc qdisc delete dev lo root htb");
out:
	bpf_qdisc_fifo__destroy(fifo_skel);
}

static void test_incompl_ops(void)
{
	struct bpf_qdisc_fail__incompl_ops *skel;
	struct bpf_link *link;

	skel = bpf_qdisc_fail__incompl_ops__open_and_load();
	if (!ASSERT_OK_PTR(skel, "bpf_qdisc_fifo__open_and_load"))
		return;

	link = bpf_map__attach_struct_ops(skel->maps.test);
	if (!ASSERT_ERR_PTR(link, "bpf_map__attach_struct_ops"))
		bpf_link__destroy(link);

	bpf_qdisc_fail__incompl_ops__destroy(skel);
}

static int get_default_qdisc(char *qdisc_name)
{
	FILE *f;
	int num;

	f = fopen("/proc/sys/net/core/default_qdisc", "r");
	if (!f)
		return -errno;

	num = fscanf(f, "%s", qdisc_name);
	fclose(f);

	return num == 1 ? 0 : -EFAULT;
}

static void test_default_qdisc_attach_to_mq(void)
{
	char default_qdisc[IFNAMSIZ] = {};
	struct bpf_qdisc_fifo *fifo_skel;
	struct netns_obj *netns = NULL;
	int err;

	fifo_skel = bpf_qdisc_fifo__open_and_load();
	if (!ASSERT_OK_PTR(fifo_skel, "bpf_qdisc_fifo__open_and_load"))
		return;

	if (!ASSERT_OK(bpf_qdisc_fifo__attach(fifo_skel), "bpf_qdisc_fifo__attach"))
		goto out;

	err = get_default_qdisc(default_qdisc);
	if (!ASSERT_OK(err, "read sysctl net.core.default_qdisc"))
		goto out;

	err = write_sysctl("/proc/sys/net/core/default_qdisc", "bpf_fifo");
	if (!ASSERT_OK(err, "write sysctl net.core.default_qdisc"))
		goto out;

	netns = netns_new("bpf_qdisc_ns", true);
	if (!ASSERT_OK_PTR(netns, "netns_new"))
		goto out;

	SYS(out, "ip link add veth0 type veth peer veth1");
	SYS(out, "tc qdisc add dev veth0 root handle 1: mq");

	ASSERT_EQ(fifo_skel->bss->init_called, true, "init_called");

	SYS(out, "tc qdisc delete dev veth0 root mq");
out:
	netns_free(netns);
	if (default_qdisc[0])
		write_sysctl("/proc/sys/net/core/default_qdisc", default_qdisc);

	bpf_qdisc_fifo__destroy(fifo_skel);
}

void test_ns_bpf_qdisc(void)
{
	if (test__start_subtest("fifo"))
		test_fifo();
	if (test__start_subtest("fq"))
		test_fq();
	if (test__start_subtest("attach to mq"))
		test_qdisc_attach_to_mq();
	if (test__start_subtest("attach to non root"))
		test_qdisc_attach_to_non_root();
	if (test__start_subtest("incompl_ops"))
		test_incompl_ops();
}

void serial_test_bpf_qdisc_default(void)
{
	test_default_qdisc_attach_to_mq();
}
