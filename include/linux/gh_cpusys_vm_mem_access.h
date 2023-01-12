/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _GH_CPUSYS_VM_MEM_ACCESS_H_
#define _GH_CPUSYS_VM_MEM_ACCESS_H_

#if IS_ENABLED(CONFIG_GH_CPUSYS_VM_MEM_ACCESS)
int gh_cpusys_vm_get_share_mem_info(struct resource *res);
#else
static inline int gh_cpusys_vm_get_share_mem_info(struct resource *res)
{
	return -EINVAL;
}
#endif /* CONFIG_GH_CPUSYS_VM_MEM_ACCESS */

#endif /* _GH_CPUSYS_VM_MEM_ACCESS_H_ */
