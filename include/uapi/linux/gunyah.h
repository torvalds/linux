/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _UAPI_LINUX_GUNYAH
#define _UAPI_LINUX_GUNYAH

/*
 * Userspace interface for /dev/gunyah - gunyah based virtual machine
 */

#include <linux/types.h>
#include <linux/ioctl.h>

#define GH_IOCTL_TYPE			'G'

/*
 * ioctls for /dev/gunyah fds:
 */
#define GH_CREATE_VM			_IO(GH_IOCTL_TYPE, 0x0) /* Returns a Gunyah VM fd */

/*
 * ioctls for VM fds
 */

#define GH_MEM_ALLOW_READ	(1UL << 0)
#define GH_MEM_ALLOW_WRITE	(1UL << 1)
#define GH_MEM_ALLOW_EXEC	(1UL << 2)

/**
 * struct gh_userspace_memory_region - Userspace memory descripion for GH_VM_SET_USER_MEM_REGION
 * @label: Unique identifer to the region.
 * @flags: Flags for memory parcel behavior
 * @guest_phys_addr: Location of the memory region in guest's memory space (page-aligned)
 * @memory_size: Size of the region (page-aligned)
 * @userspace_addr: Location of the memory region in caller (userspace)'s memory
 *
 * See Documentation/virt/gunyah/vm-manager.rst for further details.
 */
struct gh_userspace_memory_region {
	__u32 label;
	__u32 flags;
	__u64 guest_phys_addr;
	__u64 memory_size;
	__u64 userspace_addr;
};

#define GH_VM_SET_USER_MEM_REGION	_IOW(GH_IOCTL_TYPE, 0x1, \
						struct gh_userspace_memory_region)

#endif
