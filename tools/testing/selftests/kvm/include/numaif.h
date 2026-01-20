/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2020, Google LLC. */

#ifndef SELFTEST_KVM_NUMAIF_H
#define SELFTEST_KVM_NUMAIF_H

#include <dirent.h>

#include <linux/mempolicy.h>

#include "kvm_syscalls.h"

KVM_SYSCALL_DEFINE(get_mempolicy, 5, int *, policy, const unsigned long *, nmask,
		   unsigned long, maxnode, void *, addr, int, flags);

KVM_SYSCALL_DEFINE(set_mempolicy, 3, int, mode, const unsigned long *, nmask,
		   unsigned long, maxnode);

KVM_SYSCALL_DEFINE(set_mempolicy_home_node, 4, unsigned long, start,
		   unsigned long, len, unsigned long, home_node,
		   unsigned long, flags);

KVM_SYSCALL_DEFINE(migrate_pages, 4, int, pid, unsigned long, maxnode,
		   const unsigned long *, frommask, const unsigned long *, tomask);

KVM_SYSCALL_DEFINE(move_pages, 6, int, pid, unsigned long, count, void *, pages,
		   const int *, nodes, int *, status, int, flags);

KVM_SYSCALL_DEFINE(mbind, 6, void *, addr, unsigned long, size, int, mode,
		   const unsigned long *, nodemask, unsigned long, maxnode,
		   unsigned int, flags);

static inline int get_max_numa_node(void)
{
	struct dirent *de;
	int max_node = 0;
	DIR *d;

	/*
	 * Assume there's a single node if the kernel doesn't support NUMA,
	 * or if no nodes are found.
	 */
	d = opendir("/sys/devices/system/node");
	if (!d)
		return 0;

	while ((de = readdir(d)) != NULL) {
		int node_id;
		char *endptr;

		if (strncmp(de->d_name, "node", 4) != 0)
			continue;

		node_id = strtol(de->d_name + 4, &endptr, 10);
		if (*endptr != '\0')
			continue;

		if (node_id > max_node)
			max_node = node_id;
	}
	closedir(d);

	return max_node;
}

static bool is_numa_available(void)
{
	/*
	 * Probe for NUMA by doing a dummy get_mempolicy().  If the syscall
	 * fails with ENOSYS, then the kernel was built without NUMA support.
	 * if the syscall fails with EPERM, then the process/user lacks the
	 * necessary capabilities (CAP_SYS_NICE).
	 */
	return !get_mempolicy(NULL, NULL, 0, NULL, 0) ||
		(errno != ENOSYS && errno != EPERM);
}

static inline bool is_multi_numa_node_system(void)
{
	return is_numa_available() && get_max_numa_node() >= 1;
}

#endif /* SELFTEST_KVM_NUMAIF_H */
