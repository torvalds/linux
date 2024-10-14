// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>

#include "connect4_dropper.skel.h"

#include "cgroup_helpers.h"
#include "network_helpers.h"

static int run_test(int cgroup_fd, int server_fd, bool classid)
{
	struct connect4_dropper *skel;
	int fd, err = 0;

	skel = connect4_dropper__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return -1;

	skel->links.connect_v4_dropper =
		bpf_program__attach_cgroup(skel->progs.connect_v4_dropper,
					   cgroup_fd);
	if (!ASSERT_OK_PTR(skel->links.connect_v4_dropper, "prog_attach")) {
		err = -1;
		goto out;
	}

	if (classid && !ASSERT_OK(join_classid(), "join_classid")) {
		err = -1;
		goto out;
	}

	errno = 0;
	fd = connect_to_fd_opts(server_fd, NULL);
	if (fd >= 0) {
		log_err("Unexpected success to connect to server");
		err = -1;
		close(fd);
	} else if (errno != EPERM) {
		log_err("Unexpected errno from connect to server");
		err = -1;
	}
out:
	connect4_dropper__destroy(skel);
	return err;
}

void test_cgroup_v1v2(void)
{
	struct network_helper_opts opts = {};
	int server_fd, client_fd, cgroup_fd;
	static const int port = 60120;

	/* Step 1: Check base connectivity works without any BPF. */
	server_fd = start_server(AF_INET, SOCK_STREAM, NULL, port, 0);
	if (!ASSERT_GE(server_fd, 0, "server_fd"))
		return;
	client_fd = connect_to_fd_opts(server_fd, &opts);
	if (!ASSERT_GE(client_fd, 0, "client_fd")) {
		close(server_fd);
		return;
	}
	close(client_fd);
	close(server_fd);

	/* Step 2: Check BPF policy prog attached to cgroups drops connectivity. */
	cgroup_fd = test__join_cgroup("/connect_dropper");
	if (!ASSERT_GE(cgroup_fd, 0, "cgroup_fd"))
		return;
	server_fd = start_server(AF_INET, SOCK_STREAM, NULL, port, 0);
	if (!ASSERT_GE(server_fd, 0, "server_fd")) {
		close(cgroup_fd);
		return;
	}
	ASSERT_OK(run_test(cgroup_fd, server_fd, false), "cgroup-v2-only");
	setup_classid_environment();
	set_classid();
	ASSERT_OK(run_test(cgroup_fd, server_fd, true), "cgroup-v1v2");
	cleanup_classid_environment();
	close(server_fd);
	close(cgroup_fd);
}
