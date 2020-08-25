// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (C) 2020 Google LLC.
 */

#include <test_progs.h>
#include <linux/limits.h>

#include "local_storage.skel.h"
#include "network_helpers.h"

int create_and_unlink_file(void)
{
	char fname[PATH_MAX] = "/tmp/fileXXXXXX";
	int fd;

	fd = mkstemp(fname);
	if (fd < 0)
		return fd;

	close(fd);
	unlink(fname);
	return 0;
}

void test_test_local_storage(void)
{
	struct local_storage *skel = NULL;
	int err, duration = 0, serv_sk = -1;

	skel = local_storage__open_and_load();
	if (CHECK(!skel, "skel_load", "lsm skeleton failed\n"))
		goto close_prog;

	err = local_storage__attach(skel);
	if (CHECK(err, "attach", "lsm attach failed: %d\n", err))
		goto close_prog;

	skel->bss->monitored_pid = getpid();

	err = create_and_unlink_file();
	if (CHECK(err < 0, "exec_cmd", "err %d errno %d\n", err, errno))
		goto close_prog;

	CHECK(skel->data->inode_storage_result != 0, "inode_storage_result",
	      "inode_local_storage not set\n");

	serv_sk = start_server(AF_INET6, SOCK_STREAM, NULL, 0, 0);
	if (CHECK(serv_sk < 0, "start_server", "failed to start server\n"))
		goto close_prog;

	CHECK(skel->data->sk_storage_result != 0, "sk_storage_result",
	      "sk_local_storage not set\n");

	close(serv_sk);

close_prog:
	local_storage__destroy(skel);
}
