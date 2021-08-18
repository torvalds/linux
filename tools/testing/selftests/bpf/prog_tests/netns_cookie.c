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
	int server_fd = 0, client_fd = 0, cgroup_fd = 0, err = 0, val = 0;
	struct netns_cookie_prog *skel;
	uint64_t cookie_expected_value;
	socklen_t vallen = sizeof(cookie_expected_value);

	skel = netns_cookie_prog__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	cgroup_fd = test__join_cgroup("/netns_cookie");
	if (CHECK(cgroup_fd < 0, "join_cgroup", "cgroup creation failed\n"))
		goto out;

	skel->links.get_netns_cookie_sockops = bpf_program__attach_cgroup(
		skel->progs.get_netns_cookie_sockops, cgroup_fd);
	if (!ASSERT_OK_PTR(skel->links.get_netns_cookie_sockops, "prog_attach"))
		goto close_cgroup_fd;

	server_fd = start_server(AF_INET6, SOCK_STREAM, "::1", 0, 0);
	if (CHECK(server_fd < 0, "start_server", "errno %d\n", errno))
		goto close_cgroup_fd;

	client_fd = connect_to_fd(server_fd, 0);
	if (CHECK(client_fd < 0, "connect_to_fd", "errno %d\n", errno))
		goto close_server_fd;

	err = bpf_map_lookup_elem(bpf_map__fd(skel->maps.netns_cookies),
				&client_fd, &val);
	if (!ASSERT_OK(err, "map_lookup(socket_cookies)"))
		goto close_client_fd;

	err = getsockopt(client_fd, SOL_SOCKET, SO_NETNS_COOKIE,
				&cookie_expected_value, &vallen);
	if (!ASSERT_OK(err, "getsockopt)"))
		goto close_client_fd;

	ASSERT_EQ(val, cookie_expected_value, "cookie_value");

close_client_fd:
	close(client_fd);
close_server_fd:
	close(server_fd);
close_cgroup_fd:
	close(cgroup_fd);
out:
	netns_cookie_prog__destroy(skel);
}
