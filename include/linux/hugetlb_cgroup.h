/*
 * Copyright IBM Corporation, 2012
 * Author Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2.1 of the GNU Lesser General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

#ifndef _LINUX_HUGETLB_CGROUP_H
#define _LINUX_HUGETLB_CGROUP_H

#include <linux/res_counter.h>

struct hugetlb_cgroup;

#ifdef CONFIG_CGROUP_HUGETLB
static inline bool hugetlb_cgroup_disabled(void)
{
	if (hugetlb_subsys.disabled)
		return true;
	return false;
}

#else
static inline bool hugetlb_cgroup_disabled(void)
{
	return true;
}

#endif  /* CONFIG_MEM_RES_CTLR_HUGETLB */
#endif
