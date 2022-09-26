// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */

#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <linux/compiler.h>

#include "test_progs.h"
#include "cgroup_helpers.h"
#include "network_helpers.h"
#include "test_tcp_hdr_options.h"
#include "test_tcp_hdr_options.skel.h"
#include "test_misc_tcp_hdr_options.skel.h"

#define LO_ADDR6 "::1"
#define CG_NAME "/tcpbpf-hdr-opt-test"

static struct bpf_test_option exp_passive_estab_in;
static struct bpf_test_option exp_active_estab_in;
static struct bpf_test_option exp_passive_fin_in;
static struct bpf_test_option exp_active_fin_in;
static struct hdr_stg exp_passive_hdr_stg;
static struct hdr_stg exp_active_hdr_stg = { .active = true, };

static struct test_misc_tcp_hdr_options *misc_skel;
static struct test_tcp_hdr_options *skel;
static int lport_linum_map_fd;
static int hdr_stg_map_fd;
static __u32 duration;
static int cg_fd;

struct sk_fds {
	int srv_fd;
	int passive_fd;
	int active_fd;
	int passive_lport;
	int active_lport;
};

static int create_netns(void)
{
	if (!ASSERT_OK(unshare(CLONE_NEWNET), "create netns"))
		return -1;

	if (!ASSERT_OK(system("ip link set dev lo up"), "run ip cmd"))
		return -1;

	return 0;
}

static void print_hdr_stg(const struct hdr_stg *hdr_stg, const char *prefix)
{
	fprintf(stderr, "%s{active:%u, resend_syn:%u, syncookie:%u, fastopen:%u}\n",
		prefix ? : "", hdr_stg->active, hdr_stg->resend_syn,
		hdr_stg->syncookie, hdr_stg->fastopen);
}

static void print_option(const struct bpf_test_option *opt, const char *prefix)
{
	fprintf(stderr, "%s{flags:0x%x, max_delack_ms:%u, rand:0x%x}\n",
		prefix ? : "", opt->flags, opt->max_delack_ms, opt->rand);
}

static void sk_fds_close(struct sk_fds *sk_fds)
{
	close(sk_fds->srv_fd);
	close(sk_fds->passive_fd);
	close(sk_fds->active_fd);
}

static int sk_fds_shutdown(struct sk_fds *sk_fds)
{
	int ret, abyte;

	shutdown(sk_fds->active_fd, SHUT_WR);
	ret = read(sk_fds->passive_fd, &abyte, sizeof(abyte));
	if (!ASSERT_EQ(ret, 0, "read-after-shutdown(passive_fd):"))
		return -1;

	shutdown(sk_fds->passive_fd, SHUT_WR);
	ret = read(sk_fds->active_fd, &abyte, sizeof(abyte));
	if (!ASSERT_EQ(ret, 0, "read-after-shutdown(active_fd):"))
		return -1;

	return 0;
}

static int sk_fds_connect(struct sk_fds *sk_fds, bool fast_open)
{
	const char fast[] = "FAST!!!";
	struct sockaddr_in6 addr6;
	socklen_t len;

	sk_fds->srv_fd = start_server(AF_INET6, SOCK_STREAM, LO_ADDR6, 0, 0);
	if (!ASSERT_NEQ(sk_fds->srv_fd, -1, "start_server"))
		goto error;

	if (fast_open)
		sk_fds->active_fd = fastopen_connect(sk_fds->srv_fd, fast,
						     sizeof(fast), 0);
	else
		sk_fds->active_fd = connect_to_fd(sk_fds->srv_fd, 0);

	if (!ASSERT_NEQ(sk_fds->active_fd, -1, "")) {
		close(sk_fds->srv_fd);
		goto error;
	}

	len = sizeof(addr6);
	if (!ASSERT_OK(getsockname(sk_fds->srv_fd, (struct sockaddr *)&addr6,
				   &len), "getsockname(srv_fd)"))
		goto error_close;
	sk_fds->passive_lport = ntohs(addr6.sin6_port);

	len = sizeof(addr6);
	if (!ASSERT_OK(getsockname(sk_fds->active_fd, (struct sockaddr *)&addr6,
				   &len), "getsockname(active_fd)"))
		goto error_close;
	sk_fds->active_lport = ntohs(addr6.sin6_port);

	sk_fds->passive_fd = accept(sk_fds->srv_fd, NULL, 0);
	if (!ASSERT_NEQ(sk_fds->passive_fd, -1, "accept(srv_fd)"))
		goto error_close;

	if (fast_open) {
		char bytes_in[sizeof(fast)];
		int ret;

		ret = read(sk_fds->passive_fd, bytes_in, sizeof(bytes_in));
		if (!ASSERT_EQ(ret, sizeof(fast), "read fastopen syn data")) {
			close(sk_fds->passive_fd);
			goto error_close;
		}
	}

	return 0;

error_close:
	close(sk_fds->active_fd);
	close(sk_fds->srv_fd);

error:
	memset(sk_fds, -1, sizeof(*sk_fds));
	return -1;
}

static int check_hdr_opt(const struct bpf_test_option *exp,
			 const struct bpf_test_option *act,
			 const char *hdr_desc)
{
	if (!ASSERT_OK(memcmp(exp, act, sizeof(*exp)), hdr_desc)) {
		print_option(exp, "expected: ");
		print_option(act, "  actual: ");
		return -1;
	}

	return 0;
}

static int check_hdr_stg(const struct hdr_stg *exp, int fd,
			 const char *stg_desc)
{
	struct hdr_stg act;

	if (!ASSERT_OK(bpf_map_lookup_elem(hdr_stg_map_fd, &fd, &act),
		  "map_lookup(hdr_stg_map_fd)"))
		return -1;

	if (!ASSERT_OK(memcmp(exp, &act, sizeof(*exp)), stg_desc)) {
		print_hdr_stg(exp, "expected: ");
		print_hdr_stg(&act, "  actual: ");
		return -1;
	}

	return 0;
}

static int check_error_linum(const struct sk_fds *sk_fds)
{
	unsigned int nr_errors = 0;
	struct linum_err linum_err;
	int lport;

	lport = sk_fds->passive_lport;
	if (!bpf_map_lookup_elem(lport_linum_map_fd, &lport, &linum_err)) {
		fprintf(stderr,
			"bpf prog error out at lport:passive(%d), linum:%u err:%d\n",
			lport, linum_err.linum, linum_err.err);
		nr_errors++;
	}

	lport = sk_fds->active_lport;
	if (!bpf_map_lookup_elem(lport_linum_map_fd, &lport, &linum_err)) {
		fprintf(stderr,
			"bpf prog error out at lport:active(%d), linum:%u err:%d\n",
			lport, linum_err.linum, linum_err.err);
		nr_errors++;
	}

	return nr_errors;
}

static void check_hdr_and_close_fds(struct sk_fds *sk_fds)
{
	const __u32 expected_inherit_cb_flags =
		BPF_SOCK_OPS_PARSE_UNKNOWN_HDR_OPT_CB_FLAG |
		BPF_SOCK_OPS_WRITE_HDR_OPT_CB_FLAG |
		BPF_SOCK_OPS_STATE_CB_FLAG;

	if (sk_fds_shutdown(sk_fds))
		goto check_linum;

	if (!ASSERT_EQ(expected_inherit_cb_flags, skel->bss->inherit_cb_flags,
		       "inherit_cb_flags"))
		goto check_linum;

	if (check_hdr_stg(&exp_passive_hdr_stg, sk_fds->passive_fd,
			  "passive_hdr_stg"))
		goto check_linum;

	if (check_hdr_stg(&exp_active_hdr_stg, sk_fds->active_fd,
			  "active_hdr_stg"))
		goto check_linum;

	if (check_hdr_opt(&exp_passive_estab_in, &skel->bss->passive_estab_in,
			  "passive_estab_in"))
		goto check_linum;

	if (check_hdr_opt(&exp_active_estab_in, &skel->bss->active_estab_in,
			  "active_estab_in"))
		goto check_linum;

	if (check_hdr_opt(&exp_passive_fin_in, &skel->bss->passive_fin_in,
			  "passive_fin_in"))
		goto check_linum;

	check_hdr_opt(&exp_active_fin_in, &skel->bss->active_fin_in,
		      "active_fin_in");

check_linum:
	ASSERT_FALSE(check_error_linum(sk_fds), "check_error_linum");
	sk_fds_close(sk_fds);
}

static void prepare_out(void)
{
	skel->bss->active_syn_out = exp_passive_estab_in;
	skel->bss->passive_synack_out = exp_active_estab_in;

	skel->bss->active_fin_out = exp_passive_fin_in;
	skel->bss->passive_fin_out = exp_active_fin_in;
}

static void reset_test(void)
{
	size_t optsize = sizeof(struct bpf_test_option);
	int lport, err;

	memset(&skel->bss->passive_synack_out, 0, optsize);
	memset(&skel->bss->passive_fin_out, 0, optsize);

	memset(&skel->bss->passive_estab_in, 0, optsize);
	memset(&skel->bss->passive_fin_in, 0, optsize);

	memset(&skel->bss->active_syn_out, 0, optsize);
	memset(&skel->bss->active_fin_out, 0, optsize);

	memset(&skel->bss->active_estab_in, 0, optsize);
	memset(&skel->bss->active_fin_in, 0, optsize);

	skel->bss->inherit_cb_flags = 0;

	skel->data->test_kind = TCPOPT_EXP;
	skel->data->test_magic = 0xeB9F;

	memset(&exp_passive_estab_in, 0, optsize);
	memset(&exp_active_estab_in, 0, optsize);
	memset(&exp_passive_fin_in, 0, optsize);
	memset(&exp_active_fin_in, 0, optsize);

	memset(&exp_passive_hdr_stg, 0, sizeof(exp_passive_hdr_stg));
	memset(&exp_active_hdr_stg, 0, sizeof(exp_active_hdr_stg));
	exp_active_hdr_stg.active = true;

	err = bpf_map_get_next_key(lport_linum_map_fd, NULL, &lport);
	while (!err) {
		bpf_map_delete_elem(lport_linum_map_fd, &lport);
		err = bpf_map_get_next_key(lport_linum_map_fd, &lport, &lport);
	}
}

static void fastopen_estab(void)
{
	struct bpf_link *link;
	struct sk_fds sk_fds;

	hdr_stg_map_fd = bpf_map__fd(skel->maps.hdr_stg_map);
	lport_linum_map_fd = bpf_map__fd(skel->maps.lport_linum_map);

	exp_passive_estab_in.flags = OPTION_F_RAND | OPTION_F_MAX_DELACK_MS;
	exp_passive_estab_in.rand = 0xfa;
	exp_passive_estab_in.max_delack_ms = 11;

	exp_active_estab_in.flags = OPTION_F_RAND | OPTION_F_MAX_DELACK_MS;
	exp_active_estab_in.rand = 0xce;
	exp_active_estab_in.max_delack_ms = 22;

	exp_passive_hdr_stg.fastopen = true;

	prepare_out();

	/* Allow fastopen without fastopen cookie */
	if (write_sysctl("/proc/sys/net/ipv4/tcp_fastopen", "1543"))
		return;

	link = bpf_program__attach_cgroup(skel->progs.estab, cg_fd);
	if (!ASSERT_OK_PTR(link, "attach_cgroup(estab)"))
		return;

	if (sk_fds_connect(&sk_fds, true)) {
		bpf_link__destroy(link);
		return;
	}

	check_hdr_and_close_fds(&sk_fds);
	bpf_link__destroy(link);
}

static void syncookie_estab(void)
{
	struct bpf_link *link;
	struct sk_fds sk_fds;

	hdr_stg_map_fd = bpf_map__fd(skel->maps.hdr_stg_map);
	lport_linum_map_fd = bpf_map__fd(skel->maps.lport_linum_map);

	exp_passive_estab_in.flags = OPTION_F_RAND | OPTION_F_MAX_DELACK_MS;
	exp_passive_estab_in.rand = 0xfa;
	exp_passive_estab_in.max_delack_ms = 11;

	exp_active_estab_in.flags = OPTION_F_RAND | OPTION_F_MAX_DELACK_MS |
					OPTION_F_RESEND;
	exp_active_estab_in.rand = 0xce;
	exp_active_estab_in.max_delack_ms = 22;

	exp_passive_hdr_stg.syncookie = true;
	exp_active_hdr_stg.resend_syn = true,

	prepare_out();

	/* Clear the RESEND to ensure the bpf prog can learn
	 * want_cookie and set the RESEND by itself.
	 */
	skel->bss->passive_synack_out.flags &= ~OPTION_F_RESEND;

	/* Enforce syncookie mode */
	if (write_sysctl("/proc/sys/net/ipv4/tcp_syncookies", "2"))
		return;

	link = bpf_program__attach_cgroup(skel->progs.estab, cg_fd);
	if (!ASSERT_OK_PTR(link, "attach_cgroup(estab)"))
		return;

	if (sk_fds_connect(&sk_fds, false)) {
		bpf_link__destroy(link);
		return;
	}

	check_hdr_and_close_fds(&sk_fds);
	bpf_link__destroy(link);
}

static void fin(void)
{
	struct bpf_link *link;
	struct sk_fds sk_fds;

	hdr_stg_map_fd = bpf_map__fd(skel->maps.hdr_stg_map);
	lport_linum_map_fd = bpf_map__fd(skel->maps.lport_linum_map);

	exp_passive_fin_in.flags = OPTION_F_RAND;
	exp_passive_fin_in.rand = 0xfa;

	exp_active_fin_in.flags = OPTION_F_RAND;
	exp_active_fin_in.rand = 0xce;

	prepare_out();

	if (write_sysctl("/proc/sys/net/ipv4/tcp_syncookies", "1"))
		return;

	link = bpf_program__attach_cgroup(skel->progs.estab, cg_fd);
	if (!ASSERT_OK_PTR(link, "attach_cgroup(estab)"))
		return;

	if (sk_fds_connect(&sk_fds, false)) {
		bpf_link__destroy(link);
		return;
	}

	check_hdr_and_close_fds(&sk_fds);
	bpf_link__destroy(link);
}

static void __simple_estab(bool exprm)
{
	struct bpf_link *link;
	struct sk_fds sk_fds;

	hdr_stg_map_fd = bpf_map__fd(skel->maps.hdr_stg_map);
	lport_linum_map_fd = bpf_map__fd(skel->maps.lport_linum_map);

	exp_passive_estab_in.flags = OPTION_F_RAND | OPTION_F_MAX_DELACK_MS;
	exp_passive_estab_in.rand = 0xfa;
	exp_passive_estab_in.max_delack_ms = 11;

	exp_active_estab_in.flags = OPTION_F_RAND | OPTION_F_MAX_DELACK_MS;
	exp_active_estab_in.rand = 0xce;
	exp_active_estab_in.max_delack_ms = 22;

	prepare_out();

	if (!exprm) {
		skel->data->test_kind = 0xB9;
		skel->data->test_magic = 0;
	}

	if (write_sysctl("/proc/sys/net/ipv4/tcp_syncookies", "1"))
		return;

	link = bpf_program__attach_cgroup(skel->progs.estab, cg_fd);
	if (!ASSERT_OK_PTR(link, "attach_cgroup(estab)"))
		return;

	if (sk_fds_connect(&sk_fds, false)) {
		bpf_link__destroy(link);
		return;
	}

	check_hdr_and_close_fds(&sk_fds);
	bpf_link__destroy(link);
}

static void no_exprm_estab(void)
{
	__simple_estab(false);
}

static void simple_estab(void)
{
	__simple_estab(true);
}

static void misc(void)
{
	const char send_msg[] = "MISC!!!";
	char recv_msg[sizeof(send_msg)];
	const unsigned int nr_data = 2;
	struct bpf_link *link;
	struct sk_fds sk_fds;
	int i, ret;

	lport_linum_map_fd = bpf_map__fd(misc_skel->maps.lport_linum_map);

	if (write_sysctl("/proc/sys/net/ipv4/tcp_syncookies", "1"))
		return;

	link = bpf_program__attach_cgroup(misc_skel->progs.misc_estab, cg_fd);
	if (!ASSERT_OK_PTR(link, "attach_cgroup(misc_estab)"))
		return;

	if (sk_fds_connect(&sk_fds, false)) {
		bpf_link__destroy(link);
		return;
	}

	for (i = 0; i < nr_data; i++) {
		/* MSG_EOR to ensure skb will not be combined */
		ret = send(sk_fds.active_fd, send_msg, sizeof(send_msg),
			   MSG_EOR);
		if (!ASSERT_EQ(ret, sizeof(send_msg), "send(msg)"))
			goto check_linum;

		ret = read(sk_fds.passive_fd, recv_msg, sizeof(recv_msg));
		if (ASSERT_EQ(ret, sizeof(send_msg), "read(msg)"))
			goto check_linum;
	}

	if (sk_fds_shutdown(&sk_fds))
		goto check_linum;

	ASSERT_EQ(misc_skel->bss->nr_syn, 1, "unexpected nr_syn");

	ASSERT_EQ(misc_skel->bss->nr_data, nr_data, "unexpected nr_data");

	/* The last ACK may have been delayed, so it is either 1 or 2. */
	CHECK(misc_skel->bss->nr_pure_ack != 1 &&
	      misc_skel->bss->nr_pure_ack != 2,
	      "unexpected nr_pure_ack",
	      "expected (1 or 2) != actual (%u)\n",
		misc_skel->bss->nr_pure_ack);

	ASSERT_EQ(misc_skel->bss->nr_fin, 1, "unexpected nr_fin");

check_linum:
	ASSERT_FALSE(check_error_linum(&sk_fds), "check_error_linum");
	sk_fds_close(&sk_fds);
	bpf_link__destroy(link);
}

struct test {
	const char *desc;
	void (*run)(void);
};

#define DEF_TEST(name) { #name, name }
static struct test tests[] = {
	DEF_TEST(simple_estab),
	DEF_TEST(no_exprm_estab),
	DEF_TEST(syncookie_estab),
	DEF_TEST(fastopen_estab),
	DEF_TEST(fin),
	DEF_TEST(misc),
};

void test_tcp_hdr_options(void)
{
	int i;

	skel = test_tcp_hdr_options__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open and load skel"))
		return;

	misc_skel = test_misc_tcp_hdr_options__open_and_load();
	if (!ASSERT_OK_PTR(misc_skel, "open and load misc test skel"))
		goto skel_destroy;

	cg_fd = test__join_cgroup(CG_NAME);
	if (ASSERT_GE(cg_fd, 0, "join_cgroup"))
		goto skel_destroy;

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		if (!test__start_subtest(tests[i].desc))
			continue;

		if (create_netns())
			break;

		tests[i].run();

		reset_test();
	}

	close(cg_fd);
skel_destroy:
	test_misc_tcp_hdr_options__destroy(misc_skel);
	test_tcp_hdr_options__destroy(skel);
}
