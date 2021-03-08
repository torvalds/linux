// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "bind_perm.skel.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/capability.h>

static int duration;

void try_bind(int family, int port, int expected_errno)
{
	struct sockaddr_storage addr = {};
	struct sockaddr_in6 *sin6;
	struct sockaddr_in *sin;
	int fd = -1;

	fd = socket(family, SOCK_STREAM, 0);
	if (CHECK(fd < 0, "fd", "errno %d", errno))
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

bool cap_net_bind_service(cap_flag_value_t flag)
{
	const cap_value_t cap_net_bind_service = CAP_NET_BIND_SERVICE;
	cap_flag_value_t original_value;
	bool was_effective = false;
	cap_t caps;

	caps = cap_get_proc();
	if (CHECK(!caps, "cap_get_proc", "errno %d", errno))
		goto free_caps;

	if (CHECK(cap_get_flag(caps, CAP_NET_BIND_SERVICE, CAP_EFFECTIVE,
			       &original_value),
		  "cap_get_flag", "errno %d", errno))
		goto free_caps;

	was_effective = (original_value == CAP_SET);

	if (CHECK(cap_set_flag(caps, CAP_EFFECTIVE, 1, &cap_net_bind_service,
			       flag),
		  "cap_set_flag", "errno %d", errno))
		goto free_caps;

	if (CHECK(cap_set_proc(caps), "cap_set_proc", "errno %d", errno))
		goto free_caps;

free_caps:
	CHECK(cap_free(caps), "cap_free", "errno %d", errno);
	return was_effective;
}

void test_bind_perm(void)
{
	bool cap_was_effective;
	struct bind_perm *skel;
	int cgroup_fd;

	cgroup_fd = test__join_cgroup("/bind_perm");
	if (CHECK(cgroup_fd < 0, "cg-join", "errno %d", errno))
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

	cap_was_effective = cap_net_bind_service(CAP_CLEAR);

	try_bind(AF_INET, 110, EACCES);
	try_bind(AF_INET6, 110, EACCES);

	try_bind(AF_INET, 111, 0);
	try_bind(AF_INET6, 111, 0);

	if (cap_was_effective)
		cap_net_bind_service(CAP_SET);

close_skeleton:
	bind_perm__destroy(skel);
close_cgroup_fd:
	close(cgroup_fd);
}
