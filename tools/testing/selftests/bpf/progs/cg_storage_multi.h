/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __PROGS_CG_STORAGE_MULTI_H
#define __PROGS_CG_STORAGE_MULTI_H

#include <asm/types.h>

struct cgroup_value {
	__u32 egress_pkts;
	__u32 ingress_pkts;
};

#endif
