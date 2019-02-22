// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */

#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "cgroup_helpers.h"

enum bpf_array_idx {
	SRV_IDX,
	CLI_IDX,
	__NR_BPF_ARRAY_IDX,
};

#define CHECK(condition, tag, format...) ({				\
	int __ret = !!(condition);					\
	if (__ret) {							\
		printf("%s(%d):FAIL:%s ", __func__, __LINE__, tag);	\
		printf(format);						\
		printf("\n");						\
		exit(-1);						\
	}								\
})

#define TEST_CGROUP "/test-bpf-sock-fields"
#define DATA "Hello BPF!"
#define DATA_LEN sizeof(DATA)

static struct sockaddr_in6 srv_sa6, cli_sa6;
static int linum_map_fd;
static int addr_map_fd;
static int tp_map_fd;
static int sk_map_fd;
static __u32 srv_idx = SRV_IDX;
static __u32 cli_idx = CLI_IDX;

static void init_loopback6(struct sockaddr_in6 *sa6)
{
	memset(sa6, 0, sizeof(*sa6));
	sa6->sin6_family = AF_INET6;
	sa6->sin6_addr = in6addr_loopback;
}

static void print_sk(const struct bpf_sock *sk)
{
	char src_ip4[24], dst_ip4[24];
	char src_ip6[64], dst_ip6[64];

	inet_ntop(AF_INET, &sk->src_ip4, src_ip4, sizeof(src_ip4));
	inet_ntop(AF_INET6, &sk->src_ip6, src_ip6, sizeof(src_ip6));
	inet_ntop(AF_INET, &sk->dst_ip4, dst_ip4, sizeof(dst_ip4));
	inet_ntop(AF_INET6, &sk->dst_ip6, dst_ip6, sizeof(dst_ip6));

	printf("state:%u bound_dev_if:%u family:%u type:%u protocol:%u mark:%u priority:%u "
	       "src_ip4:%x(%s) src_ip6:%x:%x:%x:%x(%s) src_port:%u "
	       "dst_ip4:%x(%s) dst_ip6:%x:%x:%x:%x(%s) dst_port:%u\n",
	       sk->state, sk->bound_dev_if, sk->family, sk->type, sk->protocol,
	       sk->mark, sk->priority,
	       sk->src_ip4, src_ip4,
	       sk->src_ip6[0], sk->src_ip6[1], sk->src_ip6[2], sk->src_ip6[3],
	       src_ip6, sk->src_port,
	       sk->dst_ip4, dst_ip4,
	       sk->dst_ip6[0], sk->dst_ip6[1], sk->dst_ip6[2], sk->dst_ip6[3],
	       dst_ip6, ntohs(sk->dst_port));
}

static void print_tp(const struct bpf_tcp_sock *tp)
{
	printf("snd_cwnd:%u srtt_us:%u rtt_min:%u snd_ssthresh:%u rcv_nxt:%u "
	       "snd_nxt:%u snd:una:%u mss_cache:%u ecn_flags:%u "
	       "rate_delivered:%u rate_interval_us:%u packets_out:%u "
	       "retrans_out:%u total_retrans:%u segs_in:%u data_segs_in:%u "
	       "segs_out:%u data_segs_out:%u lost_out:%u sacked_out:%u "
	       "bytes_received:%llu bytes_acked:%llu\n",
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
	struct bpf_tcp_sock srv_tp, cli_tp;
	struct bpf_sock srv_sk, cli_sk;
	__u32 linum, idx0 = 0;
	int err;

	err = bpf_map_lookup_elem(linum_map_fd, &idx0, &linum);
	CHECK(err == -1, "bpf_map_lookup_elem(linum_map_fd)",
	      "err:%d errno:%d", err, errno);

	err = bpf_map_lookup_elem(sk_map_fd, &srv_idx, &srv_sk);
	CHECK(err == -1, "bpf_map_lookup_elem(sk_map_fd, &srv_idx)",
	      "err:%d errno:%d", err, errno);
	err = bpf_map_lookup_elem(tp_map_fd, &srv_idx, &srv_tp);
	CHECK(err == -1, "bpf_map_lookup_elem(tp_map_fd, &srv_idx)",
	      "err:%d errno:%d", err, errno);

	err = bpf_map_lookup_elem(sk_map_fd, &cli_idx, &cli_sk);
	CHECK(err == -1, "bpf_map_lookup_elem(sk_map_fd, &cli_idx)",
	      "err:%d errno:%d", err, errno);
	err = bpf_map_lookup_elem(tp_map_fd, &cli_idx, &cli_tp);
	CHECK(err == -1, "bpf_map_lookup_elem(tp_map_fd, &cli_idx)",
	      "err:%d errno:%d", err, errno);

	printf("srv_sk: ");
	print_sk(&srv_sk);
	printf("\n");

	printf("cli_sk: ");
	print_sk(&cli_sk);
	printf("\n");

	printf("srv_tp: ");
	print_tp(&srv_tp);
	printf("\n");

	printf("cli_tp: ");
	print_tp(&cli_tp);
	printf("\n");

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
	      "Unexpected srv_sk", "Check srv_sk output. linum:%u", linum);

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
	      "Unexpected cli_sk", "Check cli_sk output. linum:%u", linum);

	CHECK(srv_tp.data_segs_out != 1 ||
	      srv_tp.data_segs_in ||
	      srv_tp.snd_cwnd != 10 ||
	      srv_tp.total_retrans ||
	      srv_tp.bytes_acked != DATA_LEN,
	      "Unexpected srv_tp", "Check srv_tp output. linum:%u", linum);

	CHECK(cli_tp.data_segs_out ||
	      cli_tp.data_segs_in != 1 ||
	      cli_tp.snd_cwnd != 10 ||
	      cli_tp.total_retrans ||
	      cli_tp.bytes_received != DATA_LEN,
	      "Unexpected cli_tp", "Check cli_tp output. linum:%u", linum);
}

static void test(void)
{
	int listen_fd, cli_fd, accept_fd, epfd, err;
	struct epoll_event ev;
	socklen_t addrlen;

	addrlen = sizeof(struct sockaddr_in6);
	ev.events = EPOLLIN;

	epfd = epoll_create(1);
	CHECK(epfd == -1, "epoll_create()", "epfd:%d errno:%d", epfd, errno);

	/* Prepare listen_fd */
	listen_fd = socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK, 0);
	CHECK(listen_fd == -1, "socket()", "listen_fd:%d errno:%d",
	      listen_fd, errno);

	init_loopback6(&srv_sa6);
	err = bind(listen_fd, (struct sockaddr *)&srv_sa6, sizeof(srv_sa6));
	CHECK(err, "bind(listen_fd)", "err:%d errno:%d", err, errno);

	err = getsockname(listen_fd, (struct sockaddr *)&srv_sa6, &addrlen);
	CHECK(err, "getsockname(listen_fd)", "err:%d errno:%d", err, errno);

	err = listen(listen_fd, 1);
	CHECK(err, "listen(listen_fd)", "err:%d errno:%d", err, errno);

	/* Prepare cli_fd */
	cli_fd = socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK, 0);
	CHECK(cli_fd == -1, "socket()", "cli_fd:%d errno:%d", cli_fd, errno);

	init_loopback6(&cli_sa6);
	err = bind(cli_fd, (struct sockaddr *)&cli_sa6, sizeof(cli_sa6));
	CHECK(err, "bind(cli_fd)", "err:%d errno:%d", err, errno);

	err = getsockname(cli_fd, (struct sockaddr *)&cli_sa6, &addrlen);
	CHECK(err, "getsockname(cli_fd)", "err:%d errno:%d",
	      err, errno);

	/* Update addr_map with srv_sa6 and cli_sa6 */
	err = bpf_map_update_elem(addr_map_fd, &srv_idx, &srv_sa6, 0);
	CHECK(err, "map_update", "err:%d errno:%d", err, errno);

	err = bpf_map_update_elem(addr_map_fd, &cli_idx, &cli_sa6, 0);
	CHECK(err, "map_update", "err:%d errno:%d", err, errno);

	/* Connect from cli_sa6 to srv_sa6 */
	err = connect(cli_fd, (struct sockaddr *)&srv_sa6, addrlen);
	printf("srv_sa6.sin6_port:%u cli_sa6.sin6_port:%u\n\n",
	       ntohs(srv_sa6.sin6_port), ntohs(cli_sa6.sin6_port));
	CHECK(err && errno != EINPROGRESS,
	      "connect(cli_fd)", "err:%d errno:%d", err, errno);

	ev.data.fd = listen_fd;
	err = epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev);
	CHECK(err, "epoll_ctl(EPOLL_CTL_ADD, listen_fd)", "err:%d errno:%d",
	      err, errno);

	/* Accept the connection */
	/* Have some timeout in accept(listen_fd). Just in case. */
	err = epoll_wait(epfd, &ev, 1, 1000);
	CHECK(err != 1 || ev.data.fd != listen_fd,
	      "epoll_wait(listen_fd)",
	      "err:%d errno:%d ev.data.fd:%d listen_fd:%d",
	      err, errno, ev.data.fd, listen_fd);

	accept_fd = accept(listen_fd, NULL, NULL);
	CHECK(accept_fd == -1, "accept(listen_fd)", "accept_fd:%d errno:%d",
	      accept_fd, errno);
	close(listen_fd);

	/* Send some data from accept_fd to cli_fd */
	err = send(accept_fd, DATA, DATA_LEN, 0);
	CHECK(err != DATA_LEN, "send(accept_fd)", "err:%d errno:%d",
	      err, errno);

	/* Have some timeout in recv(cli_fd). Just in case. */
	ev.data.fd = cli_fd;
	err = epoll_ctl(epfd, EPOLL_CTL_ADD, cli_fd, &ev);
	CHECK(err, "epoll_ctl(EPOLL_CTL_ADD, cli_fd)", "err:%d errno:%d",
	      err, errno);

	err = epoll_wait(epfd, &ev, 1, 1000);
	CHECK(err != 1 || ev.data.fd != cli_fd,
	      "epoll_wait(cli_fd)", "err:%d errno:%d ev.data.fd:%d cli_fd:%d",
	      err, errno, ev.data.fd, cli_fd);

	err = recv(cli_fd, NULL, 0, MSG_TRUNC);
	CHECK(err, "recv(cli_fd)", "err:%d errno:%d", err, errno);

	close(epfd);
	close(accept_fd);
	close(cli_fd);

	check_result();
}

int main(int argc, char **argv)
{
	struct bpf_prog_load_attr attr = {
		.file = "test_sock_fields_kern.o",
		.prog_type = BPF_PROG_TYPE_CGROUP_SKB,
		.expected_attach_type = BPF_CGROUP_INET_EGRESS,
	};
	int cgroup_fd, prog_fd, err;
	struct bpf_object *obj;
	struct bpf_map *map;

	err = setup_cgroup_environment();
	CHECK(err, "setup_cgroup_environment()", "err:%d errno:%d",
	      err, errno);

	atexit(cleanup_cgroup_environment);

	/* Create a cgroup, get fd, and join it */
	cgroup_fd = create_and_get_cgroup(TEST_CGROUP);
	CHECK(cgroup_fd == -1, "create_and_get_cgroup()",
	      "cgroup_fd:%d errno:%d", cgroup_fd, errno);

	err = join_cgroup(TEST_CGROUP);
	CHECK(err, "join_cgroup", "err:%d errno:%d", err, errno);

	err = bpf_prog_load_xattr(&attr, &obj, &prog_fd);
	CHECK(err, "bpf_prog_load_xattr()", "err:%d", err);

	err = bpf_prog_attach(prog_fd, cgroup_fd, BPF_CGROUP_INET_EGRESS, 0);
	CHECK(err == -1, "bpf_prog_attach(CPF_CGROUP_INET_EGRESS)",
	      "err:%d errno%d", err, errno);
	close(cgroup_fd);

	map = bpf_object__find_map_by_name(obj, "addr_map");
	CHECK(!map, "cannot find addr_map", "(null)");
	addr_map_fd = bpf_map__fd(map);

	map = bpf_object__find_map_by_name(obj, "sock_result_map");
	CHECK(!map, "cannot find sock_result_map", "(null)");
	sk_map_fd = bpf_map__fd(map);

	map = bpf_object__find_map_by_name(obj, "tcp_sock_result_map");
	CHECK(!map, "cannot find tcp_sock_result_map", "(null)");
	tp_map_fd = bpf_map__fd(map);

	map = bpf_object__find_map_by_name(obj, "linum_map");
	CHECK(!map, "cannot find linum_map", "(null)");
	linum_map_fd = bpf_map__fd(map);

	test();

	bpf_object__close(obj);
	cleanup_cgroup_environment();

	printf("PASS\n");

	return 0;
}
