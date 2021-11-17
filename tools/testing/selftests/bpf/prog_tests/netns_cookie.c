// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include "netns_cookie_prog.skel.h"
#include "network_helpers.h"

#ifndef SO_NETNS_COOKIE
#define SO_NETNS_COOKIE 71
#endif

static int duration;

void test_netns_cookie(void)
{
	int server_fd = -1, client_fd = -1, cgroup_fd = -1;
	int err, val, ret, map, verdict;
	struct netns_cookie_prog *skel;
	uint64_t cookie_expected_value;
	socklen_t vallen = sizeof(cookie_expected_value);
	static const char send_msg[] = "message";

	skel = netns_cookie_prog__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	cgroup_fd = test__join_cgroup("/netns_cookie");
	if (CHECK(cgroup_fd < 0, "join_cgroup", "cgroup creation failed\n"))
		goto done;

	skel->links.get_netns_cookie_sockops = bpf_program__attach_cgroup(
		skel->progs.get_netns_cookie_sockops, cgroup_fd);
	if (!ASSERT_OK_PTR(skel->links.get_netns_cookie_sockops, "prog_attach"))
		goto done;

	verdict = bpf_program__fd(skel->progs.get_netns_cookie_sk_msg);
	map = bpf_map__fd(skel->maps.sock_map);
	err = bpf_prog_attach(verdict, map, BPF_SK_MSG_VERDICT, 0);
	if (!ASSERT_OK(err, "prog_attach"))
		goto done;

	server_fd = start_server(AF_INET6, SOCK_STREAM, "::1", 0, 0);
	if (CHECK(server_fd < 0, "start_server", "errno %d\n", errno))
		goto done;

	client_fd = connect_to_fd(server_fd, 0);
	if (CHECK(client_fd < 0, "connect_to_fd", "errno %d\n", errno))
		goto done;

	ret = send(client_fd, send_msg, sizeof(send_msg), 0);
	if (CHECK(ret != sizeof(send_msg), "send(msg)", "ret:%d\n", ret))
		goto done;

	err = bpf_map_lookup_elem(bpf_map__fd(skel->maps.sockops_netns_cookies),
				  &client_fd, &val);
	if (!ASSERT_OK(err, "map_lookup(sockops_netns_cookies)"))
		goto done;

	err = getsockopt(client_fd, SOL_SOCKET, SO_NETNS_COOKIE,
			 &cookie_expected_value, &vallen);
	if (!ASSERT_OK(err, "getsockopt"))
		goto done;

	ASSERT_EQ(val, cookie_expected_value, "cookie_value");

	err = bpf_map_lookup_elem(bpf_map__fd(skel->maps.sk_msg_netns_cookies),
				  &client_fd, &val);
	if (!ASSERT_OK(err, "map_lookup(sk_msg_netns_cookies)"))
		goto done;

	ASSERT_EQ(val, cookie_expected_value, "cookie_value");

done:
	if (server_fd != -1)
		close(server_fd);
	if (client_fd != -1)
		close(client_fd);
	if (cgroup_fd != -1)
		close(cgroup_fd);
	netns_cookie_prog__destroy(skel);
}
