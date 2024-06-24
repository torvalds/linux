/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/compiler.h>
#include <linux/types.h>
#include <unistd.h>
#include "../tests.h"

/* This workload was initially added to test enum augmentation with BTF in perf
 * trace because its the only syscall that has an enum argument. Since it is
 * a recent addition to the Linux kernel (at the time of the introduction of this
 * 'perf test' workload) we just add the required types and defines here instead
 * of including linux/landlock, that isn't available in older systems.
 *
 * We are not interested in the the result of the syscall, just in intercepting
 * its arguments.
 */

#ifndef __NR_landlock_add_rule
#define __NR_landlock_add_rule 445
#endif

#ifndef LANDLOCK_ACCESS_FS_READ_FILE
#define LANDLOCK_ACCESS_FS_READ_FILE	(1ULL << 2)

#define LANDLOCK_RULE_PATH_BENEATH	1

struct landlock_path_beneath_attr {
        __u64 allowed_access;
        __s32 parent_fd;
};
#endif

#ifndef LANDLOCK_ACCESS_NET_CONNECT_TCP
#define LANDLOCK_ACCESS_NET_CONNECT_TCP	(1ULL << 1)

#define LANDLOCK_RULE_NET_PORT		2

struct landlock_net_port_attr {
	__u64 allowed_access;
	__u64 port;
};
#endif

static int landlock(int argc __maybe_unused, const char **argv __maybe_unused)
{
	int fd = 11, flags = 45;

	struct landlock_path_beneath_attr path_beneath_attr = {
		.allowed_access = LANDLOCK_ACCESS_FS_READ_FILE,
		.parent_fd = 14,
	};

	struct landlock_net_port_attr net_port_attr = {
		.port = 19,
		.allowed_access = LANDLOCK_ACCESS_NET_CONNECT_TCP,
	};

	syscall(__NR_landlock_add_rule, fd, LANDLOCK_RULE_PATH_BENEATH,
		&path_beneath_attr, flags);

	syscall(__NR_landlock_add_rule, fd, LANDLOCK_RULE_NET_PORT,
		&net_port_attr, flags);

	return 0;
}

DEFINE_WORKLOAD(landlock);
