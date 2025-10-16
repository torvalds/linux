/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2020, Google LLC. */

#ifndef SELFTEST_KVM_NUMAIF_H
#define SELFTEST_KVM_NUMAIF_H

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

#endif /* SELFTEST_KVM_NUMAIF_H */
