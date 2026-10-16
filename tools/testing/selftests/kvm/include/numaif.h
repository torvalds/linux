/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2020, Google LLC. */

#ifndef SELFTEST_KVM_NUMAIF_H
#define SELFTEST_KVM_NUMAIF_H

#include <dirent.h>

#include <linux/bitmap.h>
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

/*
 * Calculate the @maxnode param for the above syscalls given the mask that will
 * be passed to the kernel, to account for a longstanding off-by-one bug in the
 * kernel that isn't properly documented in the manpages.  The manpages say
 * that @maxnode is "the maximum node ID plus one", but the kernel's actual
 * behavior is "the number of bits in the mask plus one", i.e. "the maximum
 * node ID plus two".
 */
#define MAXNODE_FOR_MASK(mask) (BITS_PER_TYPE(mask) + 1)

static inline int kvm_get_numa_memory_nodes(unsigned long *nodemask)
{
	int r;

	*nodemask = 0;

	r = get_mempolicy(NULL, nodemask, MAXNODE_FOR_MASK(*nodemask), 0,
			  MPOL_F_MEMS_ALLOWED);
	TEST_ASSERT(!r || errno == ENOSYS || errno == EPERM,
		    "Unexpected get_mempolicy() failure");
	return __builtin_popcountl(*nodemask);
}

/*
 * Return the node ID of the next NUMA node in the mask, starting at @from+1.
 * Guarantees a node is found, and that the found node is not @from.  Pass -1
 * to find the first node in the mask.
 */
static inline int kvm_get_next_numa_node(unsigned long nodemask, int from)
{
	const unsigned long nr_bits = BITS_PER_TYPE(nodemask);
	int to;

	to = find_next_bit(&nodemask, nr_bits, from + 1);
	if (to == nr_bits)
		to = find_next_bit(&nodemask, nr_bits, 0);

	TEST_ASSERT(to != nr_bits && to != from,
		    "Unabled to find second NUMA node (from = %d, to = %d)", from, to);
	return to;
}

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
