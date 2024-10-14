// SPDX-License-Identifier: GPL-2.0

#include <sys/stat.h>
#include <sys/sysmacros.h>
#include "test_progs.h"
#include "cgroup_helpers.h"
#include "get_cgroup_id_kern.skel.h"

#define TEST_CGROUP "/test-bpf-get-cgroup-id/"

void test_cgroup_get_current_cgroup_id(void)
{
	struct get_cgroup_id_kern *skel;
	const struct timespec req = {
		.tv_sec = 0,
		.tv_nsec = 1,
	};
	int cgroup_fd;
	__u64 ucgid;

	cgroup_fd = cgroup_setup_and_join(TEST_CGROUP);
	if (!ASSERT_OK_FD(cgroup_fd, "cgroup switch"))
		return;

	skel = get_cgroup_id_kern__open_and_load();
	if (!ASSERT_OK_PTR(skel, "load program"))
		goto cleanup_cgroup;

	if (!ASSERT_OK(get_cgroup_id_kern__attach(skel), "attach bpf program"))
		goto cleanup_progs;

	skel->bss->expected_pid = getpid();
	/* trigger the syscall on which is attached the tested prog */
	if (!ASSERT_OK(syscall(__NR_nanosleep, &req, NULL), "nanosleep"))
		goto cleanup_progs;

	ucgid = get_cgroup_id(TEST_CGROUP);

	ASSERT_EQ(skel->bss->cg_id, ucgid, "compare cgroup ids");

cleanup_progs:
	get_cgroup_id_kern__destroy(skel);
cleanup_cgroup:
	close(cgroup_fd);
	cleanup_cgroup_environment();
}
