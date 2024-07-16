/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * tools/testing/selftests/kvm/include/numaif.h
 *
 * Copyright (C) 2020, Google LLC.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 *
 * Header file that provides access to NUMA API functions not explicitly
 * exported to user space.
 */

#ifndef SELFTEST_KVM_NUMAIF_H
#define SELFTEST_KVM_NUMAIF_H

#define __NR_get_mempolicy 239
#define __NR_migrate_pages 256

/* System calls */
long get_mempolicy(int *policy, const unsigned long *nmask,
		   unsigned long maxnode, void *addr, int flags)
{
	return syscall(__NR_get_mempolicy, policy, nmask,
		       maxnode, addr, flags);
}

long migrate_pages(int pid, unsigned long maxnode,
		   const unsigned long *frommask,
		   const unsigned long *tomask)
{
	return syscall(__NR_migrate_pages, pid, maxnode, frommask, tomask);
}

/* Policies */
#define MPOL_DEFAULT	 0
#define MPOL_PREFERRED	 1
#define MPOL_BIND	 2
#define MPOL_INTERLEAVE	 3

#define MPOL_MAX MPOL_INTERLEAVE

/* Flags for get_mem_policy */
#define MPOL_F_NODE	    (1<<0)  /* return next il node or node of address */
				    /* Warning: MPOL_F_NODE is unsupported and
				     * subject to change. Don't use.
				     */
#define MPOL_F_ADDR	    (1<<1)  /* look up vma using address */
#define MPOL_F_MEMS_ALLOWED (1<<2)  /* query nodes allowed in cpuset */

/* Flags for mbind */
#define MPOL_MF_STRICT	     (1<<0) /* Verify existing pages in the mapping */
#define MPOL_MF_MOVE	     (1<<1) /* Move pages owned by this process to conform to mapping */
#define MPOL_MF_MOVE_ALL     (1<<2) /* Move every page to conform to mapping */

#endif /* SELFTEST_KVM_NUMAIF_H */
