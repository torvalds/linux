// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018 Facebook */

#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <fcntl.h>
#include <linux/bpf.h>
#include <linux/err.h>
#include <linux/types.h>
#include <linux/if_ether.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include "bpf_util.h"

#include "test_progs.h"
#include "test_select_reuseport_common.h"

#define MAX_TEST_NAME 80
#define MIN_TCPHDR_LEN 20
#define UDPHDR_LEN 8

#define TCP_SYNCOOKIE_SYSCTL "/proc/sys/net/ipv4/tcp_syncookies"
#define TCP_FO_SYSCTL "/proc/sys/net/ipv4/tcp_fastopen"
#define REUSEPORT_ARRAY_SIZE 32

static int result_map, tmp_index_ovr_map, linum_map, data_check_map;
static __u32 expected_results[NR_RESULTS];
static int sk_fds[REUSEPORT_ARRAY_SIZE];
static int reuseport_array = -1, outer_map = -1;
static enum bpf_map_type inner_map_type;
static int select_by_skb_data_prog;
static int saved_tcp_syncookie = -1;
static struct bpf_object *obj;
static int saved_tcp_fo = -1;
static __u32 index_zero;
static int epfd;

static union sa46 {
	struct sockaddr_in6 v6;
	struct sockaddr_in v4;
	sa_family_t family;
} srv_sa;

#define RET_IF(condition, tag, format...) ({				\
	if (CHECK_FAIL(condition)) {					\
		printf(tag " " format);					\
		return;							\
	}								\
})

#define RET_ERR(condition, tag, format...) ({				\
	if (CHECK_FAIL(condition)) {					\
		printf(tag " " format);					\
		return -1;						\
	}								\
})

static int create_maps(enum bpf_map_type inner_type)
{
	LIBBPF_OPTS(bpf_map_create_opts, opts);

	inner_map_type = inner_type;

	/* Creating reuseport_array */
	reuseport_array = bpf_map_create(inner_type, "reuseport_array",
					 sizeof(__u32), sizeof(__u32), REUSEPORT_ARRAY_SIZE, NULL);
	RET_ERR(reuseport_array < 0, "creating reuseport_array",
		"reuseport_array:%d errno:%d\n", reuseport_array, errno);

	/* Creating outer_map */
	opts.inner_map_fd = reuseport_array;
	outer_map = bpf_map_create(BPF_MAP_TYPE_ARRAY_OF_MAPS, "outer_map",
				   sizeof(__u32), sizeof(__u32), 1, &opts);
	RET_ERR(outer_map < 0, "creating outer_map",
		"outer_map:%d errno:%d\n", outer_map, errno);

	return 0;
}

static int prepare_bpf_obj(void)
{
	struct bpf_program *prog;
	struct bpf_map *map;
	int err;

	obj = bpf_object__open("test_select_reuseport_kern.bpf.o");
	err = libbpf_get_error(obj);
	RET_ERR(err, "open test_select_reuseport_kern.bpf.o",
		"obj:%p PTR_ERR(obj):%d\n", obj, err);

	map = bpf_object__find_map_by_name(obj, "outer_map");
	RET_ERR(!map, "find outer_map", "!map\n");
	err = bpf_map__reuse_fd(map, outer_map);
	RET_ERR(err, "reuse outer_map", "err:%d\n", err);

	err = bpf_object__load(obj);
	RET_ERR(err, "load bpf_object", "err:%d\n", err);

	prog = bpf_object__next_program(obj, NULL);
	RET_ERR(!prog, "get first bpf_program", "!prog\n");
	select_by_skb_data_prog = bpf_program__fd(prog);
	RET_ERR(select_by_skb_data_prog < 0, "get prog fd",
		"select_by_skb_data_prog:%d\n", select_by_skb_data_prog);

	map = bpf_object__find_map_by_name(obj, "result_map");
	RET_ERR(!map, "find result_map", "!map\n");
	result_map = bpf_map__fd(map);
	RET_ERR(result_map < 0, "get result_map fd",
		"result_map:%d\n", result_map);

	map = bpf_object__find_map_by_name(obj, "tmp_index_ovr_map");
	RET_ERR(!map, "find tmp_index_ovr_map\n", "!map");
	tmp_index_ovr_map = bpf_map__fd(map);
	RET_ERR(tmp_index_ovr_map < 0, "get tmp_index_ovr_map fd",
		"tmp_index_ovr_map:%d\n", tmp_index_ovr_map);

	map = bpf_object__find_map_by_name(obj, "linum_map");
	RET_ERR(!map, "find linum_map", "!map\n");
	linum_map = bpf_map__fd(map);
	RET_ERR(linum_map < 0, "get linum_map fd",
		"linum_map:%d\n", linum_map);

	map = bpf_object__find_map_by_name(obj, "data_check_map");
	RET_ERR(!map, "find data_check_map", "!map\n");
	data_check_map = bpf_map__fd(map);
	RET_ERR(data_check_map < 0, "get data_check_map fd",
		"data_check_map:%d\n", data_check_map);

	return 0;
}

static void sa46_init_loopback(union sa46 *sa, sa_family_t family)
{
	memset(sa, 0, sizeof(*sa));
	sa->family = family;
	if (sa->family == AF_INET6)
		sa->v6.sin6_addr = in6addr_loopback;
	else
		sa->v4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
}

static void sa46_init_inany(union sa46 *sa, sa_family_t family)
{
	memset(sa, 0, sizeof(*sa));
	sa->family = family;
	if (sa->family == AF_INET6)
		sa->v6.sin6_addr = in6addr_any;
	else
		sa->v4.sin_addr.s_addr = INADDR_ANY;
}

static int read_int_sysctl(const char *sysctl)
{
	char buf[16];
	int fd, ret;

	fd = open(sysctl, 0);
	RET_ERR(fd == -1, "open(sysctl)",
		"sysctl:%s fd:%d errno:%d\n", sysctl, fd, errno);

	ret = read(fd, buf, sizeof(buf));
	RET_ERR(ret <= 0, "read(sysctl)",
		"sysctl:%s ret:%d errno:%d\n", sysctl, ret, errno);

	close(fd);
	return atoi(buf);
}

static int write_int_sysctl(const char *sysctl, int v)
{
	int fd, ret, size;
	char buf[16];

	fd = open(sysctl, O_RDWR);
	RET_ERR(fd == -1, "open(sysctl)",
		"sysctl:%s fd:%d errno:%d\n", sysctl, fd, errno);

	size = snprintf(buf, sizeof(buf), "%d", v);
	ret = write(fd, buf, size);
	RET_ERR(ret != size, "write(sysctl)",
		"sysctl:%s ret:%d size:%d errno:%d\n",
		sysctl, ret, size, errno);

	close(fd);
	return 0;
}

static void restore_sysctls(void)
{
	if (saved_tcp_fo != -1)
		write_int_sysctl(TCP_FO_SYSCTL, saved_tcp_fo);
	if (saved_tcp_syncookie != -1)
		write_int_sysctl(TCP_SYNCOOKIE_SYSCTL, saved_tcp_syncookie);
}

static int enable_fastopen(void)
{
	int fo;

	fo = read_int_sysctl(TCP_FO_SYSCTL);
	if (fo < 0)
		return -1;

	return write_int_sysctl(TCP_FO_SYSCTL, fo | 7);
}

static int enable_syncookie(void)
{
	return write_int_sysctl(TCP_SYNCOOKIE_SYSCTL, 2);
}

static int disable_syncookie(void)
{
	return write_int_sysctl(TCP_SYNCOOKIE_SYSCTL, 0);
}

static long get_linum(void)
{
	__u32 linum;
	int err;

	err = bpf_map_lookup_elem(linum_map, &index_zero, &linum);
	RET_ERR(err < 0, "lookup_elem(linum_map)", "err:%d errno:%d\n",
		err, errno);

	return linum;
}

static void check_data(int type, sa_family_t family, const struct cmd *cmd,
		       int cli_fd)
{
	struct data_check expected = {}, result;
	union sa46 cli_sa;
	socklen_t addrlen;
	int err;

	addrlen = sizeof(cli_sa);
	err = getsockname(cli_fd, (struct sockaddr *)&cli_sa,
			  &addrlen);
	RET_IF(err < 0, "getsockname(cli_fd)", "err:%d errno:%d\n",
	       err, errno);

	err = bpf_map_lookup_elem(data_check_map, &index_zero, &result);
	RET_IF(err < 0, "lookup_elem(data_check_map)", "err:%d errno:%d\n",
	       err, errno);

	if (type == SOCK_STREAM) {
		expected.len = MIN_TCPHDR_LEN;
		expected.ip_protocol = IPPROTO_TCP;
	} else {
		expected.len = UDPHDR_LEN;
		expected.ip_protocol = IPPROTO_UDP;
	}

	if (family == AF_INET6) {
		expected.eth_protocol = htons(ETH_P_IPV6);
		expected.bind_inany = !srv_sa.v6.sin6_addr.s6_addr32[3] &&
			!srv_sa.v6.sin6_addr.s6_addr32[2] &&
			!srv_sa.v6.sin6_addr.s6_addr32[1] &&
			!srv_sa.v6.sin6_addr.s6_addr32[0];

		memcpy(&expected.skb_addrs[0], cli_sa.v6.sin6_addr.s6_addr32,
		       sizeof(cli_sa.v6.sin6_addr));
		memcpy(&expected.skb_addrs[4], &in6addr_loopback,
		       sizeof(in6addr_loopback));
		expected.skb_ports[0] = cli_sa.v6.sin6_port;
		expected.skb_ports[1] = srv_sa.v6.sin6_port;
	} else {
		expected.eth_protocol = htons(ETH_P_IP);
		expected.bind_inany = !srv_sa.v4.sin_addr.s_addr;

		expected.skb_addrs[0] = cli_sa.v4.sin_addr.s_addr;
		expected.skb_addrs[1] = htonl(INADDR_LOOPBACK);
		expected.skb_ports[0] = cli_sa.v4.sin_port;
		expected.skb_ports[1] = srv_sa.v4.sin_port;
	}

	if (memcmp(&result, &expected, offsetof(struct data_check,
						equal_check_end))) {
		printf("unexpected data_check\n");
		printf("  result: (0x%x, %u, %u)\n",
		       result.eth_protocol, result.ip_protocol,
		       result.bind_inany);
		printf("expected: (0x%x, %u, %u)\n",
		       expected.eth_protocol, expected.ip_protocol,
		       expected.bind_inany);
		RET_IF(1, "data_check result != expected",
		       "bpf_prog_linum:%ld\n", get_linum());
	}

	RET_IF(!result.hash, "data_check result.hash empty",
	       "result.hash:%u", result.hash);

	expected.len += cmd ? sizeof(*cmd) : 0;
	if (type == SOCK_STREAM)
		RET_IF(expected.len > result.len, "expected.len > result.len",
		       "expected.len:%u result.len:%u bpf_prog_linum:%ld\n",
		       expected.len, result.len, get_linum());
	else
		RET_IF(expected.len != result.len, "expected.len != result.len",
		       "expected.len:%u result.len:%u bpf_prog_linum:%ld\n",
		       expected.len, result.len, get_linum());
}

static const char *result_to_str(enum result res)
{
	switch (res) {
	case DROP_ERR_INNER_MAP:
		return "DROP_ERR_INNER_MAP";
	case DROP_ERR_SKB_DATA:
		return "DROP_ERR_SKB_DATA";
	case DROP_ERR_SK_SELECT_REUSEPORT:
		return "DROP_ERR_SK_SELECT_REUSEPORT";
	case DROP_MISC:
		return "DROP_MISC";
	case PASS:
		return "PASS";
	case PASS_ERR_SK_SELECT_REUSEPORT:
		return "PASS_ERR_SK_SELECT_REUSEPORT";
	default:
		return "UNKNOWN";
	}
}

static void check_results(void)
{
	__u32 results[NR_RESULTS];
	__u32 i, broken = 0;
	int err;

	for (i = 0; i < NR_RESULTS; i++) {
		err = bpf_map_lookup_elem(result_map, &i, &results[i]);
		RET_IF(err < 0, "lookup_elem(result_map)",
		       "i:%u err:%d errno:%d\n", i, err, errno);
	}

	for (i = 0; i < NR_RESULTS; i++) {
		if (results[i] != expected_results[i]) {
			broken = i;
			break;
		}
	}

	if (i == NR_RESULTS)
		return;

	printf("unexpected result\n");
	printf(" result: [");
	printf("%u", results[0]);
	for (i = 1; i < NR_RESULTS; i++)
		printf(", %u", results[i]);
	printf("]\n");

	printf("expected: [");
	printf("%u", expected_results[0]);
	for (i = 1; i < NR_RESULTS; i++)
		printf(", %u", expected_results[i]);
	printf("]\n");

	printf("mismatch on %s (bpf_prog_linum:%ld)\n", result_to_str(broken),
	       get_linum());

	CHECK_FAIL(true);
}

static int send_data(int type, sa_family_t family, void *data, size_t len,
		     enum result expected)
{
	union sa46 cli_sa;
	int fd, err;

	fd = socket(family, type, 0);
	RET_ERR(fd == -1, "socket()", "fd:%d errno:%d\n", fd, errno);

	sa46_init_loopback(&cli_sa, family);
	err = bind(fd, (struct sockaddr *)&cli_sa, sizeof(cli_sa));
	RET_ERR(fd == -1, "bind(cli_sa)", "err:%d errno:%d\n", err, errno);

	err = sendto(fd, data, len, MSG_FASTOPEN, (struct sockaddr *)&srv_sa,
		     sizeof(srv_sa));
	RET_ERR(err != len && expected >= PASS,
		"sendto()", "family:%u err:%d errno:%d expected:%d\n",
		family, err, errno, expected);

	return fd;
}

static void do_test(int type, sa_family_t family, struct cmd *cmd,
		    enum result expected)
{
	int nev, srv_fd, cli_fd;
	struct epoll_event ev;
	struct cmd rcv_cmd;
	ssize_t nread;

	cli_fd = send_data(type, family, cmd, cmd ? sizeof(*cmd) : 0,
			   expected);
	if (cli_fd < 0)
		return;
	nev = epoll_wait(epfd, &ev, 1, expected >= PASS ? 5 : 0);
	RET_IF((nev <= 0 && expected >= PASS) ||
	       (nev > 0 && expected < PASS),
	       "nev <> expected",
	       "nev:%d expected:%d type:%d family:%d data:(%d, %d)\n",
	       nev, expected, type, family,
	       cmd ? cmd->reuseport_index : -1,
	       cmd ? cmd->pass_on_failure : -1);
	check_results();
	check_data(type, family, cmd, cli_fd);

	if (expected < PASS)
		return;

	RET_IF(expected != PASS_ERR_SK_SELECT_REUSEPORT &&
	       cmd->reuseport_index != ev.data.u32,
	       "check cmd->reuseport_index",
	       "cmd:(%u, %u) ev.data.u32:%u\n",
	       cmd->pass_on_failure, cmd->reuseport_index, ev.data.u32);

	srv_fd = sk_fds[ev.data.u32];
	if (type == SOCK_STREAM) {
		int new_fd = accept(srv_fd, NULL, 0);

		RET_IF(new_fd == -1, "accept(srv_fd)",
		       "ev.data.u32:%u new_fd:%d errno:%d\n",
		       ev.data.u32, new_fd, errno);

		nread = recv(new_fd, &rcv_cmd, sizeof(rcv_cmd), MSG_DONTWAIT);
		RET_IF(nread != sizeof(rcv_cmd),
		       "recv(new_fd)",
		       "ev.data.u32:%u nread:%zd sizeof(rcv_cmd):%zu errno:%d\n",
		       ev.data.u32, nread, sizeof(rcv_cmd), errno);

		close(new_fd);
	} else {
		nread = recv(srv_fd, &rcv_cmd, sizeof(rcv_cmd), MSG_DONTWAIT);
		RET_IF(nread != sizeof(rcv_cmd),
		       "recv(sk_fds)",
		       "ev.data.u32:%u nread:%zd sizeof(rcv_cmd):%zu errno:%d\n",
		       ev.data.u32, nread, sizeof(rcv_cmd), errno);
	}

	close(cli_fd);
}

static void test_err_inner_map(int type, sa_family_t family)
{
	struct cmd cmd = {
		.reuseport_index = 0,
		.pass_on_failure = 0,
	};

	expected_results[DROP_ERR_INNER_MAP]++;
	do_test(type, family, &cmd, DROP_ERR_INNER_MAP);
}

static void test_err_skb_data(int type, sa_family_t family)
{
	expected_results[DROP_ERR_SKB_DATA]++;
	do_test(type, family, NULL, DROP_ERR_SKB_DATA);
}

static void test_err_sk_select_port(int type, sa_family_t family)
{
	struct cmd cmd = {
		.reuseport_index = REUSEPORT_ARRAY_SIZE,
		.pass_on_failure = 0,
	};

	expected_results[DROP_ERR_SK_SELECT_REUSEPORT]++;
	do_test(type, family, &cmd, DROP_ERR_SK_SELECT_REUSEPORT);
}

static void test_pass(int type, sa_family_t family)
{
	struct cmd cmd;
	int i;

	cmd.pass_on_failure = 0;
	for (i = 0; i < REUSEPORT_ARRAY_SIZE; i++) {
		expected_results[PASS]++;
		cmd.reuseport_index = i;
		do_test(type, family, &cmd, PASS);
	}
}

static void test_syncookie(int type, sa_family_t family)
{
	int err, tmp_index = 1;
	struct cmd cmd = {
		.reuseport_index = 0,
		.pass_on_failure = 0,
	};

	/*
	 * +1 for TCP-SYN and
	 * +1 for the TCP-ACK (ack the syncookie)
	 */
	expected_results[PASS] += 2;
	enable_syncookie();
	/*
	 * Simulate TCP-SYN and TCP-ACK are handled by two different sk:
	 * TCP-SYN: select sk_fds[tmp_index = 1] tmp_index is from the
	 *          tmp_index_ovr_map
	 * TCP-ACK: select sk_fds[reuseport_index = 0] reuseport_index
	 *          is from the cmd.reuseport_index
	 */
	err = bpf_map_update_elem(tmp_index_ovr_map, &index_zero,
				  &tmp_index, BPF_ANY);
	RET_IF(err < 0, "update_elem(tmp_index_ovr_map, 0, 1)",
	       "err:%d errno:%d\n", err, errno);
	do_test(type, family, &cmd, PASS);
	err = bpf_map_lookup_elem(tmp_index_ovr_map, &index_zero,
				  &tmp_index);
	RET_IF(err < 0 || tmp_index >= 0,
	       "lookup_elem(tmp_index_ovr_map)",
	       "err:%d errno:%d tmp_index:%d\n",
	       err, errno, tmp_index);
	disable_syncookie();
}

static void test_pass_on_err(int type, sa_family_t family)
{
	struct cmd cmd = {
		.reuseport_index = REUSEPORT_ARRAY_SIZE,
		.pass_on_failure = 1,
	};

	expected_results[PASS_ERR_SK_SELECT_REUSEPORT] += 1;
	do_test(type, family, &cmd, PASS_ERR_SK_SELECT_REUSEPORT);
}

static void test_detach_bpf(int type, sa_family_t family)
{
#ifdef SO_DETACH_REUSEPORT_BPF
	__u32 nr_run_before = 0, nr_run_after = 0, tmp, i;
	struct epoll_event ev;
	int cli_fd, err, nev;
	struct cmd cmd = {};
	int optvalue = 0;

	err = setsockopt(sk_fds[0], SOL_SOCKET, SO_DETACH_REUSEPORT_BPF,
			 &optvalue, sizeof(optvalue));
	RET_IF(err == -1, "setsockopt(SO_DETACH_REUSEPORT_BPF)",
	       "err:%d errno:%d\n", err, errno);

	err = setsockopt(sk_fds[1], SOL_SOCKET, SO_DETACH_REUSEPORT_BPF,
			 &optvalue, sizeof(optvalue));
	RET_IF(err == 0 || errno != ENOENT,
	       "setsockopt(SO_DETACH_REUSEPORT_BPF)",
	       "err:%d errno:%d\n", err, errno);

	for (i = 0; i < NR_RESULTS; i++) {
		err = bpf_map_lookup_elem(result_map, &i, &tmp);
		RET_IF(err < 0, "lookup_elem(result_map)",
		       "i:%u err:%d errno:%d\n", i, err, errno);
		nr_run_before += tmp;
	}

	cli_fd = send_data(type, family, &cmd, sizeof(cmd), PASS);
	if (cli_fd < 0)
		return;
	nev = epoll_wait(epfd, &ev, 1, 5);
	RET_IF(nev <= 0, "nev <= 0",
	       "nev:%d expected:1 type:%d family:%d data:(0, 0)\n",
	       nev,  type, family);

	for (i = 0; i < NR_RESULTS; i++) {
		err = bpf_map_lookup_elem(result_map, &i, &tmp);
		RET_IF(err < 0, "lookup_elem(result_map)",
		       "i:%u err:%d errno:%d\n", i, err, errno);
		nr_run_after += tmp;
	}

	RET_IF(nr_run_before != nr_run_after,
	       "nr_run_before != nr_run_after",
	       "nr_run_before:%u nr_run_after:%u\n",
	       nr_run_before, nr_run_after);

	close(cli_fd);
#else
	test__skip();
#endif
}

static void prepare_sk_fds(int type, sa_family_t family, bool inany)
{
	const int first = REUSEPORT_ARRAY_SIZE - 1;
	int i, err, optval = 1;
	struct epoll_event ev;
	socklen_t addrlen;

	if (inany)
		sa46_init_inany(&srv_sa, family);
	else
		sa46_init_loopback(&srv_sa, family);
	addrlen = sizeof(srv_sa);

	/*
	 * The sk_fds[] is filled from the back such that the order
	 * is exactly opposite to the (struct sock_reuseport *)reuse->socks[].
	 */
	for (i = first; i >= 0; i--) {
		sk_fds[i] = socket(family, type, 0);
		RET_IF(sk_fds[i] == -1, "socket()", "sk_fds[%d]:%d errno:%d\n",
		       i, sk_fds[i], errno);
		err = setsockopt(sk_fds[i], SOL_SOCKET, SO_REUSEPORT,
				 &optval, sizeof(optval));
		RET_IF(err == -1, "setsockopt(SO_REUSEPORT)",
		       "sk_fds[%d] err:%d errno:%d\n",
		       i, err, errno);

		if (i == first) {
			err = setsockopt(sk_fds[i], SOL_SOCKET,
					 SO_ATTACH_REUSEPORT_EBPF,
					 &select_by_skb_data_prog,
					 sizeof(select_by_skb_data_prog));
			RET_IF(err < 0, "setsockopt(SO_ATTACH_REUEPORT_EBPF)",
			       "err:%d errno:%d\n", err, errno);
		}

		err = bind(sk_fds[i], (struct sockaddr *)&srv_sa, addrlen);
		RET_IF(err < 0, "bind()", "sk_fds[%d] err:%d errno:%d\n",
		       i, err, errno);

		if (type == SOCK_STREAM) {
			err = listen(sk_fds[i], 10);
			RET_IF(err < 0, "listen()",
			       "sk_fds[%d] err:%d errno:%d\n",
			       i, err, errno);
		}

		err = bpf_map_update_elem(reuseport_array, &i, &sk_fds[i],
					  BPF_NOEXIST);
		RET_IF(err < 0, "update_elem(reuseport_array)",
		       "sk_fds[%d] err:%d errno:%d\n", i, err, errno);

		if (i == first) {
			socklen_t addrlen = sizeof(srv_sa);

			err = getsockname(sk_fds[i], (struct sockaddr *)&srv_sa,
					  &addrlen);
			RET_IF(err == -1, "getsockname()",
			       "sk_fds[%d] err:%d errno:%d\n", i, err, errno);
		}
	}

	epfd = epoll_create(1);
	RET_IF(epfd == -1, "epoll_create(1)",
	       "epfd:%d errno:%d\n", epfd, errno);

	ev.events = EPOLLIN;
	for (i = 0; i < REUSEPORT_ARRAY_SIZE; i++) {
		ev.data.u32 = i;
		err = epoll_ctl(epfd, EPOLL_CTL_ADD, sk_fds[i], &ev);
		RET_IF(err, "epoll_ctl(EPOLL_CTL_ADD)", "sk_fds[%d]\n", i);
	}
}

static void setup_per_test(int type, sa_family_t family, bool inany,
			   bool no_inner_map)
{
	int ovr = -1, err;

	prepare_sk_fds(type, family, inany);
	err = bpf_map_update_elem(tmp_index_ovr_map, &index_zero, &ovr,
				  BPF_ANY);
	RET_IF(err < 0, "update_elem(tmp_index_ovr_map, 0, -1)",
	       "err:%d errno:%d\n", err, errno);

	/* Install reuseport_array to outer_map? */
	if (no_inner_map)
		return;

	err = bpf_map_update_elem(outer_map, &index_zero, &reuseport_array,
				  BPF_ANY);
	RET_IF(err < 0, "update_elem(outer_map, 0, reuseport_array)",
	       "err:%d errno:%d\n", err, errno);
}

static void cleanup_per_test(bool no_inner_map)
{
	int i, err, zero = 0;

	memset(expected_results, 0, sizeof(expected_results));

	for (i = 0; i < NR_RESULTS; i++) {
		err = bpf_map_update_elem(result_map, &i, &zero, BPF_ANY);
		RET_IF(err, "reset elem in result_map",
		       "i:%u err:%d errno:%d\n", i, err, errno);
	}

	err = bpf_map_update_elem(linum_map, &zero, &zero, BPF_ANY);
	RET_IF(err, "reset line number in linum_map", "err:%d errno:%d\n",
	       err, errno);

	for (i = 0; i < REUSEPORT_ARRAY_SIZE; i++)
		close(sk_fds[i]);
	close(epfd);

	/* Delete reuseport_array from outer_map? */
	if (no_inner_map)
		return;

	err = bpf_map_delete_elem(outer_map, &index_zero);
	RET_IF(err < 0, "delete_elem(outer_map)",
	       "err:%d errno:%d\n", err, errno);
}

static void cleanup(void)
{
	if (outer_map >= 0) {
		close(outer_map);
		outer_map = -1;
	}

	if (reuseport_array >= 0) {
		close(reuseport_array);
		reuseport_array = -1;
	}

	if (obj) {
		bpf_object__close(obj);
		obj = NULL;
	}

	memset(expected_results, 0, sizeof(expected_results));
}

static const char *maptype_str(enum bpf_map_type type)
{
	switch (type) {
	case BPF_MAP_TYPE_REUSEPORT_SOCKARRAY:
		return "reuseport_sockarray";
	case BPF_MAP_TYPE_SOCKMAP:
		return "sockmap";
	case BPF_MAP_TYPE_SOCKHASH:
		return "sockhash";
	default:
		return "unknown";
	}
}

static const char *family_str(sa_family_t family)
{
	switch (family) {
	case AF_INET:
		return "IPv4";
	case AF_INET6:
		return "IPv6";
	default:
		return "unknown";
	}
}

static const char *sotype_str(int sotype)
{
	switch (sotype) {
	case SOCK_STREAM:
		return "TCP";
	case SOCK_DGRAM:
		return "UDP";
	default:
		return "unknown";
	}
}

#define TEST_INIT(fn_, ...) { .fn = fn_, .name = #fn_, __VA_ARGS__ }

static void test_config(int sotype, sa_family_t family, bool inany)
{
	const struct test {
		void (*fn)(int sotype, sa_family_t family);
		const char *name;
		bool no_inner_map;
		int need_sotype;
	} tests[] = {
		TEST_INIT(test_err_inner_map,
			  .no_inner_map = true),
		TEST_INIT(test_err_skb_data),
		TEST_INIT(test_err_sk_select_port),
		TEST_INIT(test_pass),
		TEST_INIT(test_syncookie,
			  .need_sotype = SOCK_STREAM),
		TEST_INIT(test_pass_on_err),
		TEST_INIT(test_detach_bpf),
	};
	char s[MAX_TEST_NAME];
	const struct test *t;

	for (t = tests; t < tests + ARRAY_SIZE(tests); t++) {
		if (t->need_sotype && t->need_sotype != sotype)
			continue; /* test not compatible with socket type */

		snprintf(s, sizeof(s), "%s %s/%s %s %s",
			 maptype_str(inner_map_type),
			 family_str(family), sotype_str(sotype),
			 inany ? "INANY" : "LOOPBACK", t->name);

		if (!test__start_subtest(s))
			continue;

		setup_per_test(sotype, family, inany, t->no_inner_map);
		t->fn(sotype, family);
		cleanup_per_test(t->no_inner_map);
	}
}

#define BIND_INANY true

static void test_all(void)
{
	const struct config {
		int sotype;
		sa_family_t family;
		bool inany;
	} configs[] = {
		{ SOCK_STREAM, AF_INET },
		{ SOCK_STREAM, AF_INET, BIND_INANY },
		{ SOCK_STREAM, AF_INET6 },
		{ SOCK_STREAM, AF_INET6, BIND_INANY },
		{ SOCK_DGRAM, AF_INET },
		{ SOCK_DGRAM, AF_INET6 },
	};
	const struct config *c;

	for (c = configs; c < configs + ARRAY_SIZE(configs); c++)
		test_config(c->sotype, c->family, c->inany);
}

void test_map_type(enum bpf_map_type mt)
{
	if (create_maps(mt))
		goto out;
	if (prepare_bpf_obj())
		goto out;

	test_all();
out:
	cleanup();
}

void serial_test_select_reuseport(void)
{
	saved_tcp_fo = read_int_sysctl(TCP_FO_SYSCTL);
	if (saved_tcp_fo < 0)
		goto out;
	saved_tcp_syncookie = read_int_sysctl(TCP_SYNCOOKIE_SYSCTL);
	if (saved_tcp_syncookie < 0)
		goto out;

	if (enable_fastopen())
		goto out;
	if (disable_syncookie())
		goto out;

	test_map_type(BPF_MAP_TYPE_REUSEPORT_SOCKARRAY);
	test_map_type(BPF_MAP_TYPE_SOCKMAP);
	test_map_type(BPF_MAP_TYPE_SOCKHASH);
out:
	restore_sysctls();
}
