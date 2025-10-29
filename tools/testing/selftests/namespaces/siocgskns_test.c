// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <linux/if.h>
#include <linux/sockios.h>
#include <linux/nsfs.h>
#include <arpa/inet.h>
#include "../kselftest_harness.h"
#include "../filesystems/utils.h"
#include "wrappers.h"

#ifndef SIOCGSKNS
#define SIOCGSKNS 0x894C
#endif

#ifndef FD_NSFS_ROOT
#define FD_NSFS_ROOT -10003
#endif

#ifndef FILEID_NSFS
#define FILEID_NSFS 0xf1
#endif

/*
 * Test basic SIOCGSKNS functionality.
 * Create a socket and verify SIOCGSKNS returns the correct network namespace.
 */
TEST(siocgskns_basic)
{
	int sock_fd, netns_fd, current_netns_fd;
	struct stat st1, st2;

	/* Create a TCP socket */
	sock_fd = socket(AF_INET, SOCK_STREAM, 0);
	ASSERT_GE(sock_fd, 0);

	/* Use SIOCGSKNS to get network namespace */
	netns_fd = ioctl(sock_fd, SIOCGSKNS);
	if (netns_fd < 0) {
		close(sock_fd);
		if (errno == ENOTTY || errno == EINVAL)
			SKIP(return, "SIOCGSKNS not supported");
		ASSERT_GE(netns_fd, 0);
	}

	/* Get current network namespace */
	current_netns_fd = open("/proc/self/ns/net", O_RDONLY);
	ASSERT_GE(current_netns_fd, 0);

	/* Verify they match */
	ASSERT_EQ(fstat(netns_fd, &st1), 0);
	ASSERT_EQ(fstat(current_netns_fd, &st2), 0);
	ASSERT_EQ(st1.st_ino, st2.st_ino);

	close(sock_fd);
	close(netns_fd);
	close(current_netns_fd);
}

TEST_HARNESS_MAIN
