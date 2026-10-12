// SPDX-License-Identifier: GPL-2.0

#include <netinet/tcp.h>

#include "test_progs.h"
#include "network_helpers.h"
#include "test_tc_change_tail_pmtu.skel.h"

#define CLIENT_NS	"tc-change-tail-cli-ns"
#define SERVER_NS	"tc-change-tail-srv-ns"
#define CLIENT_IP	"192.168.1.1"
#define SERVER_IP	"192.168.1.2"

#define TEST_PMTU	1000
#define TEST_MSS_MAX	(TEST_PMTU - 20 - 20)
#define TIMEOUT_MS	3000
#define XFER_BYTES	8192

void test_tc_change_tail_pmtu(void)
{
	LIBBPF_OPTS(bpf_tcx_opts, tcx_opts);
	int mss_before = 0, mss_after = 0, ifindex, port;
	int srv_fd = -1, srv_conn_fd = -1, cli_fd = -1;
	struct test_tc_change_tail_pmtu *skel = NULL;
	struct nstoken *nstoken = NULL;
	static char buf[XFER_BYTES];
	socklen_t optlen;
	ssize_t bytes;
	size_t total;

	if (!ASSERT_OK(make_netns(CLIENT_NS), "make client ns"))
		return;
	if (!ASSERT_OK(make_netns(SERVER_NS), "make server ns"))
		goto out_client_ns;

	nstoken = open_netns(CLIENT_NS);
	if (!ASSERT_OK_PTR(nstoken, "open client ns"))
		goto out;
	SYS(out, "ip link add veth1 type veth peer name veth2 netns " SERVER_NS);
	SYS(out, "ip -4 addr add " CLIENT_IP "/24 dev veth1");
	SYS(out, "ip link set veth1 up");
	ifindex = if_nametoindex("veth1");
	if (!ASSERT_NEQ(ifindex, 0, "if_nametoindex"))
		goto out;
	close_netns(nstoken);
	nstoken = NULL;

	nstoken = open_netns(SERVER_NS);
	if (!ASSERT_OK_PTR(nstoken, "open server ns"))
		goto out;
	SYS(out, "ip -4 addr add " SERVER_IP "/24 dev veth2");
	SYS(out, "ip link set veth2 up");
	srv_fd = start_server(AF_INET, SOCK_STREAM, SERVER_IP, 0, TIMEOUT_MS);
	if (!ASSERT_OK_FD(srv_fd, "start server"))
		goto out;
	close_netns(nstoken);
	nstoken = NULL;

	skel = test_tc_change_tail_pmtu__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open and load skeleton"))
		goto out;

	port = get_socket_local_port(srv_fd);
	if (!ASSERT_GE(port, 0, "get server port"))
		goto out;

	skel->bss->server_port = port;
	skel->bss->pmtu = TEST_PMTU;

	nstoken = open_netns(CLIENT_NS);
	if (!ASSERT_OK_PTR(nstoken, "open client ns"))
		goto out;

	skel->links.change_tail_icmp =
		bpf_program__attach_tcx(skel->progs.change_tail_icmp, ifindex,
					&tcx_opts);
	if (!ASSERT_OK_PTR(skel->links.change_tail_icmp, "attach tcx"))
		goto out;

	cli_fd = connect_to_fd(srv_fd, TIMEOUT_MS);
	if (!ASSERT_OK_FD(cli_fd, "connect to server"))
		goto out;
	srv_conn_fd = accept(srv_fd, NULL, NULL);
	if (!ASSERT_OK_FD(srv_conn_fd, "accept connection"))
		goto out;
	if (!ASSERT_OK(settimeo(srv_conn_fd, TIMEOUT_MS), "set server timeout"))
		goto out;

	optlen = sizeof(mss_before);
	if (!ASSERT_OK(getsockopt(cli_fd, IPPROTO_TCP, TCP_MAXSEG, &mss_before,
				  &optlen), "get mss before"))
		goto out;

	bytes = send(cli_fd, buf, sizeof(buf), 0);
	if (!ASSERT_EQ(bytes, (ssize_t)sizeof(buf), "send data"))
		goto out;

	for (total = 0; total < sizeof(buf); total += bytes) {
		bytes = recv(srv_conn_fd, buf, sizeof(buf), 0);
		if (bytes <= 0)
			break;
	}

	ASSERT_EQ(total, sizeof(buf), "receive data");
	ASSERT_OK(skel->data->change_tail_ret, "change tail");
	ASSERT_OK(skel->bss->adjust_room_ret, "adjust room");
	ASSERT_TRUE(skel->bss->icmp_sent, "icmp sent");

	optlen = sizeof(mss_after);
	if (!ASSERT_OK(getsockopt(cli_fd, IPPROTO_TCP, TCP_MAXSEG, &mss_after,
				  &optlen), "get mss after"))
		goto out;

	ASSERT_LT(mss_after, mss_before, "mss reduced");
	ASSERT_LE(mss_after, TEST_MSS_MAX, "mss below pmtu");
out:
	close(srv_conn_fd);
	close(cli_fd);
	close(srv_fd);
	test_tc_change_tail_pmtu__destroy(skel);
	close_netns(nstoken);
	remove_netns(SERVER_NS);
out_client_ns:
	remove_netns(CLIENT_NS);
}
