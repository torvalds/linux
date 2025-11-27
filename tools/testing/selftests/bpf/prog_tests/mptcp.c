// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020, Tessares SA. */
/* Copyright (c) 2022, SUSE. */

#include <linux/const.h>
#include <netinet/in.h>
#include <test_progs.h>
#include <unistd.h>
#include <errno.h>
#include "cgroup_helpers.h"
#include "network_helpers.h"
#include "mptcp_sock.skel.h"
#include "mptcpify.skel.h"
#include "mptcp_subflow.skel.h"
#include "mptcp_sockmap.skel.h"

#define NS_TEST "mptcp_ns"
#define ADDR_1	"10.0.1.1"
#define ADDR_2	"10.0.1.2"
#define PORT_1	10001

#ifndef IPPROTO_MPTCP
#define IPPROTO_MPTCP 262
#endif

#ifndef SOL_MPTCP
#define SOL_MPTCP 284
#endif
#ifndef MPTCP_INFO
#define MPTCP_INFO		1
#endif
#ifndef MPTCP_INFO_FLAG_FALLBACK
#define MPTCP_INFO_FLAG_FALLBACK		_BITUL(0)
#endif
#ifndef MPTCP_INFO_FLAG_REMOTE_KEY_RECEIVED
#define MPTCP_INFO_FLAG_REMOTE_KEY_RECEIVED	_BITUL(1)
#endif

#ifndef TCP_CA_NAME_MAX
#define TCP_CA_NAME_MAX	16
#endif

struct __mptcp_info {
	__u8	mptcpi_subflows;
	__u8	mptcpi_add_addr_signal;
	__u8	mptcpi_add_addr_accepted;
	__u8	mptcpi_subflows_max;
	__u8	mptcpi_add_addr_signal_max;
	__u8	mptcpi_add_addr_accepted_max;
	__u32	mptcpi_flags;
	__u32	mptcpi_token;
	__u64	mptcpi_write_seq;
	__u64	mptcpi_snd_una;
	__u64	mptcpi_rcv_nxt;
	__u8	mptcpi_local_addr_used;
	__u8	mptcpi_local_addr_max;
	__u8	mptcpi_csum_enabled;
	__u32	mptcpi_retransmits;
	__u64	mptcpi_bytes_retrans;
	__u64	mptcpi_bytes_sent;
	__u64	mptcpi_bytes_received;
	__u64	mptcpi_bytes_acked;
};

struct mptcp_storage {
	__u32 invoked;
	__u32 is_mptcp;
	struct sock *sk;
	__u32 token;
	struct sock *first;
	char ca_name[TCP_CA_NAME_MAX];
};

static int start_mptcp_server(int family, const char *addr_str, __u16 port,
			      int timeout_ms)
{
	struct network_helper_opts opts = {
		.timeout_ms	= timeout_ms,
		.proto		= IPPROTO_MPTCP,
	};

	return start_server_str(family, SOCK_STREAM, addr_str, port, &opts);
}

static int verify_tsk(int map_fd, int client_fd)
{
	int err, cfd = client_fd;
	struct mptcp_storage val;

	err = bpf_map_lookup_elem(map_fd, &cfd, &val);
	if (!ASSERT_OK(err, "bpf_map_lookup_elem"))
		return err;

	if (!ASSERT_EQ(val.invoked, 1, "unexpected invoked count"))
		err++;

	if (!ASSERT_EQ(val.is_mptcp, 0, "unexpected is_mptcp"))
		err++;

	return err;
}

static void get_msk_ca_name(char ca_name[])
{
	size_t len;
	int fd;

	fd = open("/proc/sys/net/ipv4/tcp_congestion_control", O_RDONLY);
	if (!ASSERT_GE(fd, 0, "failed to open tcp_congestion_control"))
		return;

	len = read(fd, ca_name, TCP_CA_NAME_MAX);
	if (!ASSERT_GT(len, 0, "failed to read ca_name"))
		goto err;

	if (len > 0 && ca_name[len - 1] == '\n')
		ca_name[len - 1] = '\0';

err:
	close(fd);
}

static int verify_msk(int map_fd, int client_fd, __u32 token)
{
	char ca_name[TCP_CA_NAME_MAX];
	int err, cfd = client_fd;
	struct mptcp_storage val;

	if (!ASSERT_GT(token, 0, "invalid token"))
		return -1;

	get_msk_ca_name(ca_name);

	err = bpf_map_lookup_elem(map_fd, &cfd, &val);
	if (!ASSERT_OK(err, "bpf_map_lookup_elem"))
		return err;

	if (!ASSERT_EQ(val.invoked, 1, "unexpected invoked count"))
		err++;

	if (!ASSERT_EQ(val.is_mptcp, 1, "unexpected is_mptcp"))
		err++;

	if (!ASSERT_EQ(val.token, token, "unexpected token"))
		err++;

	if (!ASSERT_EQ(val.first, val.sk, "unexpected first"))
		err++;

	if (!ASSERT_STRNEQ(val.ca_name, ca_name, TCP_CA_NAME_MAX, "unexpected ca_name"))
		err++;

	return err;
}

static int run_test(int cgroup_fd, int server_fd, bool is_mptcp)
{
	int client_fd, prog_fd, map_fd, err;
	struct mptcp_sock *sock_skel;

	sock_skel = mptcp_sock__open_and_load();
	if (!ASSERT_OK_PTR(sock_skel, "skel_open_load"))
		return libbpf_get_error(sock_skel);

	err = mptcp_sock__attach(sock_skel);
	if (!ASSERT_OK(err, "skel_attach"))
		goto out;

	prog_fd = bpf_program__fd(sock_skel->progs._sockops);
	map_fd = bpf_map__fd(sock_skel->maps.socket_storage_map);
	err = bpf_prog_attach(prog_fd, cgroup_fd, BPF_CGROUP_SOCK_OPS, 0);
	if (!ASSERT_OK(err, "bpf_prog_attach"))
		goto out;

	client_fd = connect_to_fd(server_fd, 0);
	if (!ASSERT_GE(client_fd, 0, "connect to fd")) {
		err = -EIO;
		goto out;
	}

	err += is_mptcp ? verify_msk(map_fd, client_fd, sock_skel->bss->token) :
			  verify_tsk(map_fd, client_fd);

	close(client_fd);

out:
	mptcp_sock__destroy(sock_skel);
	return err;
}

static void test_base(void)
{
	struct netns_obj *netns = NULL;
	int server_fd, cgroup_fd;

	cgroup_fd = test__join_cgroup("/mptcp");
	if (!ASSERT_GE(cgroup_fd, 0, "test__join_cgroup"))
		return;

	netns = netns_new(NS_TEST, true);
	if (!ASSERT_OK_PTR(netns, "netns_new"))
		goto fail;

	/* without MPTCP */
	server_fd = start_server(AF_INET, SOCK_STREAM, NULL, 0, 0);
	if (!ASSERT_GE(server_fd, 0, "start_server"))
		goto with_mptcp;

	ASSERT_OK(run_test(cgroup_fd, server_fd, false), "run_test tcp");

	close(server_fd);

with_mptcp:
	/* with MPTCP */
	server_fd = start_mptcp_server(AF_INET, NULL, 0, 0);
	if (!ASSERT_GE(server_fd, 0, "start_mptcp_server"))
		goto fail;

	ASSERT_OK(run_test(cgroup_fd, server_fd, true), "run_test mptcp");

	close(server_fd);

fail:
	netns_free(netns);
	close(cgroup_fd);
}

static void send_byte(int fd)
{
	char b = 0x55;

	ASSERT_EQ(write(fd, &b, sizeof(b)), 1, "send single byte");
}

static int verify_mptcpify(int server_fd, int client_fd)
{
	struct __mptcp_info info;
	socklen_t optlen;
	int protocol;
	int err = 0;

	optlen = sizeof(protocol);
	if (!ASSERT_OK(getsockopt(server_fd, SOL_SOCKET, SO_PROTOCOL, &protocol, &optlen),
		       "getsockopt(SOL_PROTOCOL)"))
		return -1;

	if (!ASSERT_EQ(protocol, IPPROTO_MPTCP, "protocol isn't MPTCP"))
		err++;

	optlen = sizeof(info);
	if (!ASSERT_OK(getsockopt(client_fd, SOL_MPTCP, MPTCP_INFO, &info, &optlen),
		       "getsockopt(MPTCP_INFO)"))
		return -1;

	if (!ASSERT_GE(info.mptcpi_flags, 0, "unexpected mptcpi_flags"))
		err++;
	if (!ASSERT_FALSE(info.mptcpi_flags & MPTCP_INFO_FLAG_FALLBACK,
			  "MPTCP fallback"))
		err++;
	if (!ASSERT_TRUE(info.mptcpi_flags & MPTCP_INFO_FLAG_REMOTE_KEY_RECEIVED,
			 "no remote key received"))
		err++;

	return err;
}

static int run_mptcpify(int cgroup_fd)
{
	int server_fd, client_fd, err = 0;
	struct mptcpify *mptcpify_skel;

	mptcpify_skel = mptcpify__open_and_load();
	if (!ASSERT_OK_PTR(mptcpify_skel, "skel_open_load"))
		return libbpf_get_error(mptcpify_skel);

	mptcpify_skel->bss->pid = getpid();

	err = mptcpify__attach(mptcpify_skel);
	if (!ASSERT_OK(err, "skel_attach"))
		goto out;

	/* without MPTCP */
	server_fd = start_server(AF_INET, SOCK_STREAM, NULL, 0, 0);
	if (!ASSERT_GE(server_fd, 0, "start_server")) {
		err = -EIO;
		goto out;
	}

	client_fd = connect_to_fd(server_fd, 0);
	if (!ASSERT_GE(client_fd, 0, "connect to fd")) {
		err = -EIO;
		goto close_server;
	}

	send_byte(client_fd);

	err = verify_mptcpify(server_fd, client_fd);

	close(client_fd);
close_server:
	close(server_fd);
out:
	mptcpify__destroy(mptcpify_skel);
	return err;
}

static void test_mptcpify(void)
{
	struct netns_obj *netns = NULL;
	int cgroup_fd;

	cgroup_fd = test__join_cgroup("/mptcpify");
	if (!ASSERT_GE(cgroup_fd, 0, "test__join_cgroup"))
		return;

	netns = netns_new(NS_TEST, true);
	if (!ASSERT_OK_PTR(netns, "netns_new"))
		goto fail;

	ASSERT_OK(run_mptcpify(cgroup_fd), "run_mptcpify");

fail:
	netns_free(netns);
	close(cgroup_fd);
}

static int endpoint_init(char *flags)
{
	SYS(fail, "ip -net %s link add veth1 type veth peer name veth2", NS_TEST);
	SYS(fail, "ip -net %s addr add %s/24 dev veth1", NS_TEST, ADDR_1);
	SYS(fail, "ip -net %s link set dev veth1 up", NS_TEST);
	SYS(fail, "ip -net %s addr add %s/24 dev veth2", NS_TEST, ADDR_2);
	SYS(fail, "ip -net %s link set dev veth2 up", NS_TEST);
	if (SYS_NOFAIL("ip -net %s mptcp endpoint add %s %s", NS_TEST, ADDR_2, flags)) {
		printf("'ip mptcp' not supported, skip this test.\n");
		test__skip();
		goto fail;
	}

	return 0;
fail:
	return -1;
}

static void wait_for_new_subflows(int fd)
{
	socklen_t len;
	u8 subflows;
	int err, i;

	len = sizeof(subflows);
	/* Wait max 5 sec for new subflows to be created */
	for (i = 0; i < 50; i++) {
		err = getsockopt(fd, SOL_MPTCP, MPTCP_INFO, &subflows, &len);
		if (!err && subflows > 0)
			break;

		usleep(100000); /* 0.1s */
	}
}

static void run_subflow(void)
{
	int server_fd, client_fd, err;
	char new[TCP_CA_NAME_MAX];
	char cc[TCP_CA_NAME_MAX];
	unsigned int mark;
	socklen_t len;

	server_fd = start_mptcp_server(AF_INET, ADDR_1, PORT_1, 0);
	if (!ASSERT_OK_FD(server_fd, "start_mptcp_server"))
		return;

	client_fd = connect_to_fd(server_fd, 0);
	if (!ASSERT_OK_FD(client_fd, "connect_to_fd"))
		goto close_server;

	send_byte(client_fd);
	wait_for_new_subflows(client_fd);

	len = sizeof(mark);
	err = getsockopt(client_fd, SOL_SOCKET, SO_MARK, &mark, &len);
	if (ASSERT_OK(err, "getsockopt(client_fd, SO_MARK)"))
		ASSERT_EQ(mark, 0, "mark");

	len = sizeof(new);
	err = getsockopt(client_fd, SOL_TCP, TCP_CONGESTION, new, &len);
	if (ASSERT_OK(err, "getsockopt(client_fd, TCP_CONGESTION)")) {
		get_msk_ca_name(cc);
		ASSERT_STREQ(new, cc, "cc");
	}

	close(client_fd);
close_server:
	close(server_fd);
}

static void test_subflow(void)
{
	struct mptcp_subflow *skel;
	struct netns_obj *netns;
	int cgroup_fd;

	cgroup_fd = test__join_cgroup("/mptcp_subflow");
	if (!ASSERT_OK_FD(cgroup_fd, "join_cgroup: mptcp_subflow"))
		return;

	skel = mptcp_subflow__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_load: mptcp_subflow"))
		goto close_cgroup;

	skel->bss->pid = getpid();

	skel->links.mptcp_subflow =
		bpf_program__attach_cgroup(skel->progs.mptcp_subflow, cgroup_fd);
	if (!ASSERT_OK_PTR(skel->links.mptcp_subflow, "attach mptcp_subflow"))
		goto skel_destroy;

	skel->links._getsockopt_subflow =
		bpf_program__attach_cgroup(skel->progs._getsockopt_subflow, cgroup_fd);
	if (!ASSERT_OK_PTR(skel->links._getsockopt_subflow, "attach _getsockopt_subflow"))
		goto skel_destroy;

	netns = netns_new(NS_TEST, true);
	if (!ASSERT_OK_PTR(netns, "netns_new: mptcp_subflow"))
		goto skel_destroy;

	if (endpoint_init("subflow") < 0)
		goto close_netns;

	run_subflow();

close_netns:
	netns_free(netns);
skel_destroy:
	mptcp_subflow__destroy(skel);
close_cgroup:
	close(cgroup_fd);
}

/* Test sockmap on MPTCP server handling non-mp-capable clients. */
static void test_sockmap_with_mptcp_fallback(struct mptcp_sockmap *skel)
{
	int listen_fd = -1, client_fd1 = -1, client_fd2 = -1;
	int server_fd1 = -1, server_fd2 = -1, sent, recvd;
	char snd[9] = "123456789";
	char rcv[10];

	/* start server with MPTCP enabled */
	listen_fd = start_mptcp_server(AF_INET, NULL, 0, 0);
	if (!ASSERT_OK_FD(listen_fd, "sockmap-fb:start_mptcp_server"))
		return;

	skel->bss->trace_port = ntohs(get_socket_local_port(listen_fd));
	skel->bss->sk_index = 0;
	/* create client without MPTCP enabled */
	client_fd1 = connect_to_fd_opts(listen_fd, NULL);
	if (!ASSERT_OK_FD(client_fd1, "sockmap-fb:connect_to_fd"))
		goto end;

	server_fd1 = accept(listen_fd, NULL, 0);
	skel->bss->sk_index = 1;
	client_fd2 = connect_to_fd_opts(listen_fd, NULL);
	if (!ASSERT_OK_FD(client_fd2, "sockmap-fb:connect_to_fd"))
		goto end;

	server_fd2 = accept(listen_fd, NULL, 0);
	/* test normal redirect behavior: data sent by client_fd1 can be
	 * received by client_fd2
	 */
	skel->bss->redirect_idx = 1;
	sent = send(client_fd1, snd, sizeof(snd), 0);
	if (!ASSERT_EQ(sent, sizeof(snd), "sockmap-fb:send(client_fd1)"))
		goto end;

	/* try to recv more bytes to avoid truncation check */
	recvd = recv(client_fd2, rcv, sizeof(rcv), 0);
	if (!ASSERT_EQ(recvd, sizeof(snd), "sockmap-fb:recv(client_fd2)"))
		goto end;

end:
	if (client_fd1 >= 0)
		close(client_fd1);
	if (client_fd2 >= 0)
		close(client_fd2);
	if (server_fd1 >= 0)
		close(server_fd1);
	if (server_fd2 >= 0)
		close(server_fd2);
	close(listen_fd);
}

/* Test sockmap rejection of MPTCP sockets - both server and client sides. */
static void test_sockmap_reject_mptcp(struct mptcp_sockmap *skel)
{
	int listen_fd = -1, server_fd = -1, client_fd1 = -1;
	int err, zero = 0;

	/* start server with MPTCP enabled */
	listen_fd = start_mptcp_server(AF_INET, NULL, 0, 0);
	if (!ASSERT_OK_FD(listen_fd, "start_mptcp_server"))
		return;

	skel->bss->trace_port = ntohs(get_socket_local_port(listen_fd));
	skel->bss->sk_index = 0;
	/* create client with MPTCP enabled */
	client_fd1 = connect_to_fd(listen_fd, 0);
	if (!ASSERT_OK_FD(client_fd1, "connect_to_fd client_fd1"))
		goto end;

	/* bpf_sock_map_update() called from sockops should reject MPTCP sk */
	if (!ASSERT_EQ(skel->bss->helper_ret, -EOPNOTSUPP, "should reject"))
		goto end;

	server_fd = accept(listen_fd, NULL, 0);
	err = bpf_map_update_elem(bpf_map__fd(skel->maps.sock_map),
				  &zero, &server_fd, BPF_NOEXIST);
	if (!ASSERT_EQ(err, -EOPNOTSUPP, "server should be disallowed"))
		goto end;

	/* MPTCP client should also be disallowed */
	err = bpf_map_update_elem(bpf_map__fd(skel->maps.sock_map),
				  &zero, &client_fd1, BPF_NOEXIST);
	if (!ASSERT_EQ(err, -EOPNOTSUPP, "client should be disallowed"))
		goto end;
end:
	if (client_fd1 >= 0)
		close(client_fd1);
	if (server_fd >= 0)
		close(server_fd);
	close(listen_fd);
}

static void test_mptcp_sockmap(void)
{
	struct mptcp_sockmap *skel;
	struct netns_obj *netns;
	int cgroup_fd, err;

	cgroup_fd = test__join_cgroup("/mptcp_sockmap");
	if (!ASSERT_OK_FD(cgroup_fd, "join_cgroup: mptcp_sockmap"))
		return;

	skel = mptcp_sockmap__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_load: mptcp_sockmap"))
		goto close_cgroup;

	skel->links.mptcp_sockmap_inject =
		bpf_program__attach_cgroup(skel->progs.mptcp_sockmap_inject, cgroup_fd);
	if (!ASSERT_OK_PTR(skel->links.mptcp_sockmap_inject, "attach sockmap"))
		goto skel_destroy;

	err = bpf_prog_attach(bpf_program__fd(skel->progs.mptcp_sockmap_redirect),
			      bpf_map__fd(skel->maps.sock_map),
			      BPF_SK_SKB_STREAM_VERDICT, 0);
	if (!ASSERT_OK(err, "bpf_prog_attach stream verdict"))
		goto skel_destroy;

	netns = netns_new(NS_TEST, true);
	if (!ASSERT_OK_PTR(netns, "netns_new: mptcp_sockmap"))
		goto skel_destroy;

	if (endpoint_init("subflow") < 0)
		goto close_netns;

	test_sockmap_with_mptcp_fallback(skel);
	test_sockmap_reject_mptcp(skel);

close_netns:
	netns_free(netns);
skel_destroy:
	mptcp_sockmap__destroy(skel);
close_cgroup:
	close(cgroup_fd);
}

void test_mptcp(void)
{
	if (test__start_subtest("base"))
		test_base();
	if (test__start_subtest("mptcpify"))
		test_mptcpify();
	if (test__start_subtest("subflow"))
		test_subflow();
	if (test__start_subtest("sockmap"))
		test_mptcp_sockmap();
}
