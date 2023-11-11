// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <network_helpers.h>
#include <linux/netfilter/nf_conntrack_common.h>
#include "test_bpf_nf.skel.h"
#include "test_bpf_nf_fail.skel.h"

static char log_buf[1024 * 1024];

struct {
	const char *prog_name;
	const char *err_msg;
} test_bpf_nf_fail_tests[] = {
	{ "alloc_release", "kernel function bpf_ct_release args#0 expected pointer to STRUCT nf_conn but" },
	{ "insert_insert", "kernel function bpf_ct_insert_entry args#0 expected pointer to STRUCT nf_conn___init but" },
	{ "lookup_insert", "kernel function bpf_ct_insert_entry args#0 expected pointer to STRUCT nf_conn___init but" },
	{ "set_timeout_after_insert", "kernel function bpf_ct_set_timeout args#0 expected pointer to STRUCT nf_conn___init but" },
	{ "set_status_after_insert", "kernel function bpf_ct_set_status args#0 expected pointer to STRUCT nf_conn___init but" },
	{ "change_timeout_after_alloc", "kernel function bpf_ct_change_timeout args#0 expected pointer to STRUCT nf_conn but" },
	{ "change_status_after_alloc", "kernel function bpf_ct_change_status args#0 expected pointer to STRUCT nf_conn but" },
	{ "write_not_allowlisted_field", "no write support to nf_conn at off" },
};

enum {
	TEST_XDP,
	TEST_TC_BPF,
};

#define TIMEOUT_MS		3000
#define IPS_STATUS_MASK		(IPS_CONFIRMED | IPS_SEEN_REPLY | \
				 IPS_SRC_NAT_DONE | IPS_DST_NAT_DONE | \
				 IPS_SRC_NAT | IPS_DST_NAT)

static int connect_to_server(int srv_fd)
{
	int fd = -1;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (!ASSERT_GE(fd, 0, "socket"))
		goto out;

	if (!ASSERT_EQ(connect_fd_to_fd(fd, srv_fd, TIMEOUT_MS), 0, "connect_fd_to_fd")) {
		close(fd);
		fd = -1;
	}
out:
	return fd;
}

static void test_bpf_nf_ct(int mode)
{
	const char *iptables = "iptables -t raw %s PREROUTING -j CONNMARK --set-mark 42/0";
	int srv_fd = -1, client_fd = -1, srv_client_fd = -1;
	struct sockaddr_in peer_addr = {};
	struct test_bpf_nf *skel;
	int prog_fd, err;
	socklen_t len;
	u16 srv_port;
	char cmd[64];
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.repeat = 1,
	);

	skel = test_bpf_nf__open_and_load();
	if (!ASSERT_OK_PTR(skel, "test_bpf_nf__open_and_load"))
		return;

	/* Enable connection tracking */
	snprintf(cmd, sizeof(cmd), iptables, "-A");
	if (!ASSERT_OK(system(cmd), "iptables"))
		goto end;

	srv_port = (mode == TEST_XDP) ? 5005 : 5006;
	srv_fd = start_server(AF_INET, SOCK_STREAM, "127.0.0.1", srv_port, TIMEOUT_MS);
	if (!ASSERT_GE(srv_fd, 0, "start_server"))
		goto end;

	client_fd = connect_to_server(srv_fd);
	if (!ASSERT_GE(client_fd, 0, "connect_to_server"))
		goto end;

	len = sizeof(peer_addr);
	srv_client_fd = accept(srv_fd, (struct sockaddr *)&peer_addr, &len);
	if (!ASSERT_GE(srv_client_fd, 0, "accept"))
		goto end;
	if (!ASSERT_EQ(len, sizeof(struct sockaddr_in), "sockaddr len"))
		goto end;

	skel->bss->saddr = peer_addr.sin_addr.s_addr;
	skel->bss->sport = peer_addr.sin_port;
	skel->bss->daddr = peer_addr.sin_addr.s_addr;
	skel->bss->dport = htons(srv_port);

	if (mode == TEST_XDP)
		prog_fd = bpf_program__fd(skel->progs.nf_xdp_ct_test);
	else
		prog_fd = bpf_program__fd(skel->progs.nf_skb_ct_test);

	err = bpf_prog_test_run_opts(prog_fd, &topts);
	if (!ASSERT_OK(err, "bpf_prog_test_run"))
		goto end;

	ASSERT_EQ(skel->bss->test_einval_bpf_tuple, -EINVAL, "Test EINVAL for NULL bpf_tuple");
	ASSERT_EQ(skel->bss->test_einval_reserved, -EINVAL, "Test EINVAL for reserved not set to 0");
	ASSERT_EQ(skel->bss->test_einval_netns_id, -EINVAL, "Test EINVAL for netns_id < -1");
	ASSERT_EQ(skel->bss->test_einval_len_opts, -EINVAL, "Test EINVAL for len__opts != NF_BPF_CT_OPTS_SZ");
	ASSERT_EQ(skel->bss->test_eproto_l4proto, -EPROTO, "Test EPROTO for l4proto != TCP or UDP");
	ASSERT_EQ(skel->bss->test_enonet_netns_id, -ENONET, "Test ENONET for bad but valid netns_id");
	ASSERT_EQ(skel->bss->test_enoent_lookup, -ENOENT, "Test ENOENT for failed lookup");
	ASSERT_EQ(skel->bss->test_eafnosupport, -EAFNOSUPPORT, "Test EAFNOSUPPORT for invalid len__tuple");
	ASSERT_EQ(skel->data->test_alloc_entry, 0, "Test for alloc new entry");
	ASSERT_EQ(skel->data->test_insert_entry, 0, "Test for insert new entry");
	ASSERT_EQ(skel->data->test_succ_lookup, 0, "Test for successful lookup");
	/* allow some tolerance for test_delta_timeout value to avoid races. */
	ASSERT_GT(skel->bss->test_delta_timeout, 8, "Test for min ct timeout update");
	ASSERT_LE(skel->bss->test_delta_timeout, 10, "Test for max ct timeout update");
	ASSERT_EQ(skel->bss->test_insert_lookup_mark, 77, "Test for insert and lookup mark value");
	ASSERT_EQ(skel->bss->test_status, IPS_STATUS_MASK, "Test for ct status update ");
	ASSERT_EQ(skel->data->test_exist_lookup, 0, "Test existing connection lookup");
	ASSERT_EQ(skel->bss->test_exist_lookup_mark, 43, "Test existing connection lookup ctmark");
	ASSERT_EQ(skel->data->test_snat_addr, 0, "Test for source natting");
	ASSERT_EQ(skel->data->test_dnat_addr, 0, "Test for destination natting");
end:
	if (srv_client_fd != -1)
		close(srv_client_fd);
	if (client_fd != -1)
		close(client_fd);
	if (srv_fd != -1)
		close(srv_fd);
	snprintf(cmd, sizeof(cmd), iptables, "-D");
	system(cmd);
	test_bpf_nf__destroy(skel);
}

static void test_bpf_nf_ct_fail(const char *prog_name, const char *err_msg)
{
	LIBBPF_OPTS(bpf_object_open_opts, opts, .kernel_log_buf = log_buf,
						.kernel_log_size = sizeof(log_buf),
						.kernel_log_level = 1);
	struct test_bpf_nf_fail *skel;
	struct bpf_program *prog;
	int ret;

	skel = test_bpf_nf_fail__open_opts(&opts);
	if (!ASSERT_OK_PTR(skel, "test_bpf_nf_fail__open"))
		return;

	prog = bpf_object__find_program_by_name(skel->obj, prog_name);
	if (!ASSERT_OK_PTR(prog, "bpf_object__find_program_by_name"))
		goto end;

	bpf_program__set_autoload(prog, true);

	ret = test_bpf_nf_fail__load(skel);
	if (!ASSERT_ERR(ret, "test_bpf_nf_fail__load must fail"))
		goto end;

	if (!ASSERT_OK_PTR(strstr(log_buf, err_msg), "expected error message")) {
		fprintf(stderr, "Expected: %s\n", err_msg);
		fprintf(stderr, "Verifier: %s\n", log_buf);
	}

end:
	test_bpf_nf_fail__destroy(skel);
}

void test_bpf_nf(void)
{
	int i;
	if (test__start_subtest("xdp-ct"))
		test_bpf_nf_ct(TEST_XDP);
	if (test__start_subtest("tc-bpf-ct"))
		test_bpf_nf_ct(TEST_TC_BPF);
	for (i = 0; i < ARRAY_SIZE(test_bpf_nf_fail_tests); i++) {
		if (test__start_subtest(test_bpf_nf_fail_tests[i].prog_name))
			test_bpf_nf_ct_fail(test_bpf_nf_fail_tests[i].prog_name,
					    test_bpf_nf_fail_tests[i].err_msg);
	}
}
