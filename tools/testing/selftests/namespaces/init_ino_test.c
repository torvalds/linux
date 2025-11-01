// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2025 Christian Brauner <brauner@kernel.org>

#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <linux/nsfs.h>

#include "../kselftest_harness.h"

struct ns_info {
	const char *name;
	const char *proc_path;
	unsigned int expected_ino;
};

static struct ns_info namespaces[] = {
	{ "ipc", "/proc/1/ns/ipc", IPC_NS_INIT_INO },
	{ "uts", "/proc/1/ns/uts", UTS_NS_INIT_INO },
	{ "user", "/proc/1/ns/user", USER_NS_INIT_INO },
	{ "pid", "/proc/1/ns/pid", PID_NS_INIT_INO },
	{ "cgroup", "/proc/1/ns/cgroup", CGROUP_NS_INIT_INO },
	{ "time", "/proc/1/ns/time", TIME_NS_INIT_INO },
	{ "net", "/proc/1/ns/net", NET_NS_INIT_INO },
	{ "mnt", "/proc/1/ns/mnt", MNT_NS_INIT_INO },
};

TEST(init_namespace_inodes)
{
	struct stat st;

	for (int i = 0; i < sizeof(namespaces) / sizeof(namespaces[0]); i++) {
		int ret = stat(namespaces[i].proc_path, &st);

		/* Some namespaces might not be available (e.g., time namespace on older kernels) */
		if (ret < 0) {
			if (errno == ENOENT) {
				ksft_test_result_skip("%s namespace not available\n",
						      namespaces[i].name);
				continue;
			}
			ASSERT_GE(ret, 0)
			TH_LOG("Failed to stat %s: %s",
			       namespaces[i].proc_path, strerror(errno));
		}

		ASSERT_EQ(st.st_ino, namespaces[i].expected_ino)
			TH_LOG("Namespace %s has inode 0x%lx, expected 0x%x",
			       namespaces[i].name, st.st_ino, namespaces[i].expected_ino);

		ksft_print_msg("Namespace %s: inode 0x%lx matches expected 0x%x\n",
			       namespaces[i].name, st.st_ino, namespaces[i].expected_ino);
	}
}

TEST_HARNESS_MAIN
