// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */

#define _GNU_SOURCE
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <linux/compiler.h>

#include "network_helpers.h"
#include "cgroup_helpers.h"
#include "test_progs.h"
#include "bpf_rlimit.h"
#include "test_sock_fields.skel.h"

enum bpf_linum_array_idx {
	EGRESS_LINUM_IDX,
	INGRESS_LINUM_IDX,
	READ_SK_DST_PORT_LINUM_IDX,
	__NR_BPF_LINUM_ARRAY_IDX,
};

struct bpf_spinlock_cnt {
	struct bpf_spin_lock lock;
	__u32 cnt;
};

#define PARENT_CGROUP	"/test-bpf-sock-fields"
#define CHILD_CGROUP	"/test-bpf-sock-fields/child"
#define DATA "Hello BPF!"
#define DATA_LEN sizeof(DATA)

static struct sockaddr_in6 srv_sa6, cli_sa6;
static int sk_pkt_out_cnt10_fd;
static struct test_sock_fields *skel;
static int sk_pkt_out_cnt_fd;
static __u64 parent_cg_id;
static __u64 child_cg_id;
static int linum_map_fd;
static __u32 duration;

static bool create_netns(void)
{
	if (!ASSERT_OK(unshare(CLONE_NEWNET), "create netns"))
		return false;

	if (!ASSERT_OK(system("ip link set dev lo up"), "bring up lo"))
		return false;

	return true;
}

static void print_sk(const struct bpf_sock *sk, const char *prefix)
{
	char src_ip4[24], dst_ip4[24];
	char src_ip6[64], dst_ip6[64];

	inet_ntop(AF_INET, &sk->src_ip4, src_ip4, sizeof(src_ip4));
	inet_ntop(AF_INET6, &sk->src_ip6, src_ip6, sizeof(src_ip6));
	inet_ntop(AF_INET, &sk->dst_ip4, dst_ip4, sizeof(dst_ip4));
	inet_ntop(AF_INET6, &sk->dst_ip6, dst_ip6, sizeof(dst_ip6));

	printf("%s: state:%u bound_dev_if:%u family:%u type:%u protocol:%u mark:%u priority:%u "
	       "src_ip4:%x(%s) src_ip6:%x:%x:%x:%x(%s) src_port:%u "
	       "dst_ip4:%x(%s) dst_ip6:%x:%x:%x:%x(%s) dst_port:%u\n",
	       prefix,
	       sk->state, sk->bound_dev_if, sk->family, sk->type, sk->protocol,
	       sk->mark, sk->priority,
	       sk->src_ip4, src_ip4,
	       sk->src_ip6[0], sk->src_ip6[1], sk->src_ip6[2], sk->src_ip6[3],
	       src_ip6, sk->src_port,
	       sk->dst_ip4, dst_ip4,
	       sk->dst_ip6[0], sk->dst_ip6[1], sk->dst_ip6[2], sk->dst_ip6[3],
	       dst_ip6, ntohs(sk->dst_port));
}

static void print_tp(const struct bpf_tcp_sock *tp, const char *prefix)
{
	printf("%s: snd_cwnd:%u srtt_us:%u rtt_min:%u snd_ssthresh:%u rcv_nxt:%u "
	       "snd_nxt:%u snd:una:%u mss_cache:%u ecn_flags:%u "
	       "rate_delivered:%u rate_interval_us:%u packets_out:%u "
	       "retrans_out:%u total_retrans:%u segs_in:%u data_segs_in:%u "
	       "segs_out:%u data_segs_out:%u lost_out:%u sacked_out:%u "
	       "bytes_received:%llu bytes_acked:%llu\n",
	       prefix,
	       tp->snd_cwnd, tp->srtt_us, tp->rtt_min, tp->snd_ssthresh,
	       tp->rcv_nxt, tp->snd_nxt, tp->snd_una, tp->mss_cache,
	       tp->ecn_flags, tp->rate_delivered, tp->rate_interval_us,
	       tp->packets_out, tp->retrans_out, tp->total_retrans,
	       tp->segs_in, tp->data_segs_in, tp->segs_out,
	       tp->data_segs_out, tp->lost_out, tp->sacked_out,
	       tp->bytes_received, tp->bytes_acked);
}

static void check_result(void)
{
	struct bpf_tcp_sock srv_tp, cli_tp, listen_tp;
	struct bpf_sock srv_sk, cli_sk, listen_sk;
	__u32 idx, ingress_linum, egress_linum, linum;
	int err;

	idx = EGRESS_LINUM_IDX;
	err = bpf_map_lookup_elem(linum_map_fd, &idx, &egress_linum);
	CHECK(err < 0, "bpf_map_lookup_elem(linum_map_fd)",
	      "err:%d errno:%d\n", err, errno);

	idx = INGRESS_LINUM_IDX;
	err = bpf_map_lookup_elem(linum_map_fd, &idx, &ingress_linum);
	CHECK(err < 0, "bpf_map_lookup_elem(linum_map_fd)",
	      "err:%d errno:%d\n", err, errno);

	idx = READ_SK_DST_PORT_LINUM_IDX;
	err = bpf_map_lookup_elem(linum_map_fd, &idx, &linum);
	ASSERT_OK(err, "bpf_map_lookup_elem(linum_map_fd, READ_SK_DST_PORT_IDX)");
	ASSERT_EQ(linum, 0, "failure in read_sk_dst_port on line");

	memcpy(&srv_sk, &skel->bss->srv_sk, sizeof(srv_sk));
	memcpy(&srv_tp, &skel->bss->srv_tp, sizeof(srv_tp));
	memcpy(&cli_sk, &skel->bss->cli_sk, sizeof(cli_sk));
	memcpy(&cli_tp, &skel->bss->cli_tp, sizeof(cli_tp));
	memcpy(&listen_sk, &skel->bss->listen_sk, sizeof(listen_sk));
	memcpy(&listen_tp, &skel->bss->listen_tp, sizeof(listen_tp));

	print_sk(&listen_sk, "listen_sk");
	print_sk(&srv_sk, "srv_sk");
	print_sk(&cli_sk, "cli_sk");
	print_tp(&listen_tp, "listen_tp");
	print_tp(&srv_tp, "srv_tp");
	print_tp(&cli_tp, "cli_tp");

	CHECK(listen_sk.state != 10 ||
	      listen_sk.family != AF_INET6 ||
	      listen_sk.protocol != IPPROTO_TCP ||
	      memcmp(listen_sk.src_ip6, &in6addr_loopback,
		     sizeof(listen_sk.src_ip6)) ||
	      listen_sk.dst_ip6[0] || listen_sk.dst_ip6[1] ||
	      listen_sk.dst_ip6[2] || listen_sk.dst_ip6[3] ||
	      listen_sk.src_port != ntohs(srv_sa6.sin6_port) ||
	      listen_sk.dst_port,
	      "listen_sk",
	      "Unexpected. Check listen_sk output. ingress_linum:%u\n",
	      ingress_linum);

	CHECK(srv_sk.state == 10 ||
	      !srv_sk.state ||
	      srv_sk.family != AF_INET6 ||
	      srv_sk.protocol != IPPROTO_TCP ||
	      memcmp(srv_sk.src_ip6, &in6addr_loopback,
		     sizeof(srv_sk.src_ip6)) ||
	      memcmp(srv_sk.dst_ip6, &in6addr_loopback,
		     sizeof(srv_sk.dst_ip6)) ||
	      srv_sk.src_port != ntohs(srv_sa6.sin6_port) ||
	      srv_sk.dst_port != cli_sa6.sin6_port,
	      "srv_sk", "Unexpected. Check srv_sk output. egress_linum:%u\n",
	      egress_linum);

	CHECK(!skel->bss->lsndtime, "srv_tp", "Unexpected lsndtime:0\n");

	CHECK(cli_sk.state == 10 ||
	      !cli_sk.state ||
	      cli_sk.family != AF_INET6 ||
	      cli_sk.protocol != IPPROTO_TCP ||
	      memcmp(cli_sk.src_ip6, &in6addr_loopback,
		     sizeof(cli_sk.src_ip6)) ||
	      memcmp(cli_sk.dst_ip6, &in6addr_loopback,
		     sizeof(cli_sk.dst_ip6)) ||
	      cli_sk.src_port != ntohs(cli_sa6.sin6_port) ||
	      cli_sk.dst_port != srv_sa6.sin6_port,
	      "cli_sk", "Unexpected. Check cli_sk output. egress_linum:%u\n",
	      egress_linum);

	CHECK(listen_tp.data_segs_out ||
	      listen_tp.data_segs_in ||
	      listen_tp.total_retrans ||
	      listen_tp.bytes_acked,
	      "listen_tp",
	      "Unexpected. Check listen_tp output. ingress_linum:%u\n",
	      ingress_linum);

	CHECK(srv_tp.data_segs_out != 2 ||
	      srv_tp.data_segs_in ||
	      srv_tp.snd_cwnd != 10 ||
	      srv_tp.total_retrans ||
	      srv_tp.bytes_acked < 2 * DATA_LEN,
	      "srv_tp", "Unexpected. Check srv_tp output. egress_linum:%u\n",
	      egress_linum);

	CHECK(cli_tp.data_segs_out ||
	      cli_tp.data_segs_in != 2 ||
	      cli_tp.snd_cwnd != 10 ||
	      cli_tp.total_retrans ||
	      cli_tp.bytes_received < 2 * DATA_LEN,
	      "cli_tp", "Unexpected. Check cli_tp output. egress_linum:%u\n",
	      egress_linum);

	CHECK(skel->bss->parent_cg_id != parent_cg_id,
	      "parent_cg_id", "%zu != %zu\n",
	      (size_t)skel->bss->parent_cg_id, (size_t)parent_cg_id);

	CHECK(skel->bss->child_cg_id != child_cg_id,
	      "child_cg_id", "%zu != %zu\n",
	       (size_t)skel->bss->child_cg_id, (size_t)child_cg_id);
}

static void check_sk_pkt_out_cnt(int accept_fd, int cli_fd)
{
	struct bpf_spinlock_cnt pkt_out_cnt = {}, pkt_out_cnt10 = {};
	int err;

	pkt_out_cnt.cnt = ~0;
	pkt_out_cnt10.cnt = ~0;
	err = bpf_map_lookup_elem(sk_pkt_out_cnt_fd, &accept_fd, &pkt_out_cnt);
	if (!err)
		err = bpf_map_lookup_elem(sk_pkt_out_cnt10_fd, &accept_fd,
					  &pkt_out_cnt10);

	/* The bpf prog only counts for fullsock and
	 * passive connection did not become fullsock until 3WHS
	 * had been finished, so the bpf prog only counted two data
	 * packet out.
	 */
	CHECK(err || pkt_out_cnt.cnt < 0xeB9F + 2 ||
	      pkt_out_cnt10.cnt < 0xeB9F + 20,
	      "bpf_map_lookup_elem(sk_pkt_out_cnt, &accept_fd)",
	      "err:%d errno:%d pkt_out_cnt:%u pkt_out_cnt10:%u\n",
	      err, errno, pkt_out_cnt.cnt, pkt_out_cnt10.cnt);

	pkt_out_cnt.cnt = ~0;
	pkt_out_cnt10.cnt = ~0;
	err = bpf_map_lookup_elem(sk_pkt_out_cnt_fd, &cli_fd, &pkt_out_cnt);
	if (!err)
		err = bpf_map_lookup_elem(sk_pkt_out_cnt10_fd, &cli_fd,
					  &pkt_out_cnt10);
	/* Active connection is fullsock from the beginning.
	 * 1 SYN and 1 ACK during 3WHS
	 * 2 Acks on data packet.
	 *
	 * The bpf_prog initialized it to 0xeB9F.
	 */
	CHECK(err || pkt_out_cnt.cnt < 0xeB9F + 4 ||
	      pkt_out_cnt10.cnt < 0xeB9F + 40,
	      "bpf_map_lookup_elem(sk_pkt_out_cnt, &cli_fd)",
	      "err:%d errno:%d pkt_out_cnt:%u pkt_out_cnt10:%u\n",
	      err, errno, pkt_out_cnt.cnt, pkt_out_cnt10.cnt);
}

static int init_sk_storage(int sk_fd, __u32 pkt_out_cnt)
{
	struct bpf_spinlock_cnt scnt = {};
	int err;

	scnt.cnt = pkt_out_cnt;
	err = bpf_map_update_elem(sk_pkt_out_cnt_fd, &sk_fd, &scnt,
				  BPF_NOEXIST);
	if (CHECK(err, "bpf_map_update_elem(sk_pkt_out_cnt_fd)",
		  "err:%d errno:%d\n", err, errno))
		return err;

	err = bpf_map_update_elem(sk_pkt_out_cnt10_fd, &sk_fd, &scnt,
				  BPF_NOEXIST);
	if (CHECK(err, "bpf_map_update_elem(sk_pkt_out_cnt10_fd)",
		  "err:%d errno:%d\n", err, errno))
		return err;

	return 0;
}

static void test(void)
{
	int listen_fd = -1, cli_fd = -1, accept_fd = -1, err, i;
	socklen_t addrlen = sizeof(struct sockaddr_in6);
	char buf[DATA_LEN];

	/* Prepare listen_fd */
	listen_fd = start_server(AF_INET6, SOCK_STREAM, "::1", 0xcafe, 0);
	/* start_server() has logged the error details */
	if (CHECK_FAIL(listen_fd == -1))
		goto done;

	err = getsockname(listen_fd, (struct sockaddr *)&srv_sa6, &addrlen);
	if (CHECK(err, "getsockname(listen_fd)", "err:%d errno:%d\n", err,
		  errno))
		goto done;
	memcpy(&skel->bss->srv_sa6, &srv_sa6, sizeof(srv_sa6));

	cli_fd = connect_to_fd(listen_fd, 0);
	if (CHECK_FAIL(cli_fd == -1))
		goto done;

	err = getsockname(cli_fd, (struct sockaddr *)&cli_sa6, &addrlen);
	if (CHECK(err, "getsockname(cli_fd)", "err:%d errno:%d\n",
		  err, errno))
		goto done;

	accept_fd = accept(listen_fd, NULL, NULL);
	if (CHECK(accept_fd == -1, "accept(listen_fd)",
		  "accept_fd:%d errno:%d\n",
		  accept_fd, errno))
		goto done;

	if (init_sk_storage(accept_fd, 0xeB9F))
		goto done;

	for (i = 0; i < 2; i++) {
		/* Send some data from accept_fd to cli_fd.
		 * MSG_EOR to stop kernel from coalescing two pkts.
		 */
		err = send(accept_fd, DATA, DATA_LEN, MSG_EOR);
		if (CHECK(err != DATA_LEN, "send(accept_fd)",
			  "err:%d errno:%d\n", err, errno))
			goto done;

		err = recv(cli_fd, buf, DATA_LEN, 0);
		if (CHECK(err != DATA_LEN, "recv(cli_fd)", "err:%d errno:%d\n",
			  err, errno))
			goto done;
	}

	shutdown(cli_fd, SHUT_WR);
	err = recv(accept_fd, buf, 1, 0);
	if (CHECK(err, "recv(accept_fd) for fin", "err:%d errno:%d\n",
		  err, errno))
		goto done;
	shutdown(accept_fd, SHUT_WR);
	err = recv(cli_fd, buf, 1, 0);
	if (CHECK(err, "recv(cli_fd) for fin", "err:%d errno:%d\n",
		  err, errno))
		goto done;
	check_sk_pkt_out_cnt(accept_fd, cli_fd);
	check_result();

done:
	if (accept_fd != -1)
		close(accept_fd);
	if (cli_fd != -1)
		close(cli_fd);
	if (listen_fd != -1)
		close(listen_fd);
}

void test_sock_fields(void)
{
	int parent_cg_fd = -1, child_cg_fd = -1;
	struct bpf_link *link;

	/* Use a dedicated netns to have a fixed listen port */
	if (!create_netns())
		return;

	/* Create a cgroup, get fd, and join it */
	parent_cg_fd = test__join_cgroup(PARENT_CGROUP);
	if (CHECK_FAIL(parent_cg_fd < 0))
		return;
	parent_cg_id = get_cgroup_id(PARENT_CGROUP);
	if (CHECK_FAIL(!parent_cg_id))
		goto done;

	child_cg_fd = test__join_cgroup(CHILD_CGROUP);
	if (CHECK_FAIL(child_cg_fd < 0))
		goto done;
	child_cg_id = get_cgroup_id(CHILD_CGROUP);
	if (CHECK_FAIL(!child_cg_id))
		goto done;

	skel = test_sock_fields__open_and_load();
	if (CHECK(!skel, "test_sock_fields__open_and_load", "failed\n"))
		goto done;

	link = bpf_program__attach_cgroup(skel->progs.egress_read_sock_fields, child_cg_fd);
	if (!ASSERT_OK_PTR(link, "attach_cgroup(egress_read_sock_fields)"))
		goto done;
	skel->links.egress_read_sock_fields = link;

	link = bpf_program__attach_cgroup(skel->progs.ingress_read_sock_fields, child_cg_fd);
	if (!ASSERT_OK_PTR(link, "attach_cgroup(ingress_read_sock_fields)"))
		goto done;
	skel->links.ingress_read_sock_fields = link;

	link = bpf_program__attach_cgroup(skel->progs.read_sk_dst_port, child_cg_fd);
	if (!ASSERT_OK_PTR(link, "attach_cgroup(read_sk_dst_port"))
		goto done;
	skel->links.read_sk_dst_port = link;

	linum_map_fd = bpf_map__fd(skel->maps.linum_map);
	sk_pkt_out_cnt_fd = bpf_map__fd(skel->maps.sk_pkt_out_cnt);
	sk_pkt_out_cnt10_fd = bpf_map__fd(skel->maps.sk_pkt_out_cnt10);

	test();

done:
	test_sock_fields__detach(skel);
	test_sock_fields__destroy(skel);
	if (child_cg_fd >= 0)
		close(child_cg_fd);
	if (parent_cg_fd >= 0)
		close(parent_cg_fd);
}
