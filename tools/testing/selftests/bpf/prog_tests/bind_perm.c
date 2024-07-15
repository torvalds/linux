// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <sched.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "test_progs.h"
#include "cap_helpers.h"
#include "bind_perm.skel.h"

static int create_netns(void)
{
	if (!ASSERT_OK(unshare(CLONE_NEWNET), "create netns"))
		return -1;

	return 0;
}

void try_bind(int family, int port, int expected_errno)
{
	struct sockaddr_storage addr = {};
	struct sockaddr_in6 *sin6;
	struct sockaddr_in *sin;
	int fd = -1;

	fd = socket(family, SOCK_STREAM, 0);
	if (!ASSERT_GE(fd, 0, "socket"))
		goto close_socket;

	if (family == AF_INET) {
		sin = (struct sockaddr_in *)&addr;
		sin->sin_family = family;
		sin->sin_port = htons(port);
	} else {
		sin6 = (struct sockaddr_in6 *)&addr;
		sin6->sin6_family = family;
		sin6->sin6_port = htons(port);
	}

	errno = 0;
	bind(fd, (struct sockaddr *)&addr, sizeof(addr));
	ASSERT_EQ(errno, expected_errno, "bind");

close_socket:
	if (fd >= 0)
		close(fd);
}

void test_bind_perm(void)
{
	const __u64 net_bind_svc_cap = 1ULL << CAP_NET_BIND_SERVICE;
	struct bind_perm *skel;
	__u64 old_caps = 0;
	int cgroup_fd;

	if (create_netns())
		return;

	cgroup_fd = test__join_cgroup("/bind_perm");
	if (!ASSERT_GE(cgroup_fd, 0, "test__join_cgroup"))
		return;

	skel = bind_perm__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel"))
		goto close_cgroup_fd;

	skel->links.bind_v4_prog = bpf_program__attach_cgroup(skel->progs.bind_v4_prog, cgroup_fd);
	if (!ASSERT_OK_PTR(skel, "bind_v4_prog"))
		goto close_skeleton;

	skel->links.bind_v6_prog = bpf_program__attach_cgroup(skel->progs.bind_v6_prog, cgroup_fd);
	if (!ASSERT_OK_PTR(skel, "bind_v6_prog"))
		goto close_skeleton;

	ASSERT_OK(cap_disable_effective(net_bind_svc_cap, &old_caps),
		  "cap_disable_effective");

	try_bind(AF_INET, 110, EACCES);
	try_bind(AF_INET6, 110, EACCES);

	try_bind(AF_INET, 111, 0);
	try_bind(AF_INET6, 111, 0);

	if (old_caps & net_bind_svc_cap)
		ASSERT_OK(cap_enable_effective(net_bind_svc_cap, NULL),
			  "cap_enable_effective");

close_skeleton:
	bind_perm__destroy(skel);
close_cgroup_fd:
	close(cgroup_fd);
}
