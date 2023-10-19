// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "udp_limit.skel.h"

#include <sys/types.h>
#include <sys/socket.h>

void test_udp_limit(void)
{
	struct udp_limit *skel;
	int fd1 = -1, fd2 = -1;
	int cgroup_fd;

	cgroup_fd = test__join_cgroup("/udp_limit");
	if (!ASSERT_GE(cgroup_fd, 0, "cg-join"))
		return;

	skel = udp_limit__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel-load"))
		goto close_cgroup_fd;

	skel->links.sock = bpf_program__attach_cgroup(skel->progs.sock, cgroup_fd);
	if (!ASSERT_OK_PTR(skel->links.sock, "cg_attach_sock"))
		goto close_skeleton;
	skel->links.sock_release = bpf_program__attach_cgroup(skel->progs.sock_release, cgroup_fd);
	if (!ASSERT_OK_PTR(skel->links.sock_release, "cg_attach_sock_release"))
		goto close_skeleton;

	/* BPF program enforces a single UDP socket per cgroup,
	 * verify that.
	 */
	fd1 = socket(AF_INET, SOCK_DGRAM, 0);
	if (!ASSERT_GE(fd1, 0, "socket(fd1)"))
		goto close_skeleton;

	fd2 = socket(AF_INET, SOCK_DGRAM, 0);
	if (!ASSERT_LT(fd2, 0, "socket(fd2)"))
		goto close_skeleton;

	/* We can reopen again after close. */
	close(fd1);
	fd1 = -1;

	fd1 = socket(AF_INET, SOCK_DGRAM, 0);
	if (!ASSERT_GE(fd1, 0, "socket(fd1-again)"))
		goto close_skeleton;

	/* Make sure the program was invoked the expected
	 * number of times:
	 * - open fd1           - BPF_CGROUP_INET_SOCK_CREATE
	 * - attempt to openfd2 - BPF_CGROUP_INET_SOCK_CREATE
	 * - close fd1          - BPF_CGROUP_INET_SOCK_RELEASE
	 * - open fd1 again     - BPF_CGROUP_INET_SOCK_CREATE
	 */
	if (!ASSERT_EQ(skel->bss->invocations, 4, "bss-invocations"))
		goto close_skeleton;

	/* We should still have a single socket in use */
	if (!ASSERT_EQ(skel->bss->in_use, 1, "bss-in_use"))
		goto close_skeleton;

close_skeleton:
	if (fd1 >= 0)
		close(fd1);
	if (fd2 >= 0)
		close(fd2);
	udp_limit__destroy(skel);
close_cgroup_fd:
	close(cgroup_fd);
}
