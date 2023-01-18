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

/**
 * struct gh_vm_dtb_config - Set the location of the VM's devicetree blob
 * @guest_phys_addr: Address of the VM's devicetree in guest memory.
 * @size: Maximum size of the devicetree.
 */
struct gh_vm_dtb_config {
	__u64 guest_phys_addr;
	__u64 size;
};
#define GH_VM_SET_DTB_CONFIG	_IOW(GH_IOCTL_TYPE, 0x2, struct gh_vm_dtb_config)

#define GH_VM_START		_IO(GH_IOCTL_TYPE, 0x3)

/**
 * GH_FN_VCPU - create a vCPU instance to control a vCPU
 *
 * gh_fn_desc is filled with &struct gh_fn_vcpu_arg
 *
 * The vcpu type will register with the VM Manager to expect to control
 * vCPU number `vcpu_id`. It returns a file descriptor allowing interaction with
 * the vCPU. See the Gunyah vCPU API description sections for interacting with
 * the Gunyah vCPU file descriptors.
 *
 * Return: file descriptor to manipulate the vcpu. See GH_VCPU_* ioctls
 */
#define GH_FN_VCPU 		1

/**
 * GH_FN_IRQFD - register eventfd to assert a Gunyah doorbell
 *
 * gh_fn_desc is filled with gh_fn_irqfd_arg
 *
 * Allows setting an eventfd to directly trigger a guest interrupt.
 * irqfd.fd specifies the file descriptor to use as the eventfd.
 * irqfd.label corresponds to the doorbell label used in the guest VM's devicetree.
 *
 * Return: 0
 */
#define GH_FN_IRQFD 		2

/**
 * GH_FN_IOEVENTFD - register ioeventfd to trigger when VM faults on parameter
 *
 * gh_fn_desc is filled with gh_fn_ioeventfd_arg
 *
 * Attaches an ioeventfd to a legal mmio address within the guest. A guest write
 * in the registered address will signal the provided event instead of triggering
 * an exit on the GH_VCPU_RUN ioctl.
 *
 * If GH_IOEVENTFD_DATAMATCH flag is set, the event will be signaled only if the
 * written value to the registered address is equal to datamatch in
 * struct gh_fn_ioeventfd_arg.
 *
 * Return: 0
 */
#define GH_FN_IOEVENTFD 	3

#define GH_FN_MAX_ARG_SIZE		256

/**
 * struct gh_fn_vcpu_arg - Arguments to create a vCPU
 * @id: vcpu id
 */
struct gh_fn_vcpu_arg {
	__u32 id;
};

#define GH_IRQFD_LEVEL			(1UL << 0)

/**
 * struct gh_fn_irqfd_arg - Arguments to create an irqfd function
 * @fd: an eventfd which when written to will raise a doorbell
 * @label: Label of the doorbell created on the guest VM
 * @flags: GH_IRQFD_LEVEL configures the corresponding doorbell to behave
 *         like a level triggered interrupt.
 * @padding: padding bytes
 */
struct gh_fn_irqfd_arg {
	__u32 fd;
	__u32 label;
	__u32 flags;
	__u32 padding;
};

#define GH_IOEVENTFD_DATAMATCH		(1UL << 0)

/**
 * struct gh_fn_ioeventfd_arg - Arguments to create an ioeventfd function
 * @datamatch: data used when GH_IOEVENTFD_DATAMATCH is set
 * @addr: Address in guest memory
 * @len: Length of access
 * @fd: When ioeventfd is matched, this eventfd is written
 * @flags: If GH_IOEVENTFD_DATAMATCH flag is set, the event will be signaled
 *         only if the written value to the registered address is equal to
 *         @datamatch
 * @padding: padding bytes
 */
struct gh_fn_ioeventfd_arg {
	__u64 datamatch;
	__u64 addr;        /* legal mmio address */
	__u32 len;         /* 1, 2, 4, or 8 bytes; or 0 to ignore length */
	__s32 fd;
	__u32 flags;
	__u32 padding;
};

/**
 * struct gh_fn_desc - Arguments to create a VM function
 * @type: Type of the function. See GH_FN_* macro for supported types
 * @arg_size: Size of argument to pass to the function. arg_size <= GH_FN_MAX_ARG_SIZE
 * @arg: Value or pointer to argument given to the function
 */
struct gh_fn_desc {
	__u32 type;
	__u32 arg_size;
	__u64 arg;
};

#define GH_VM_ADD_FUNCTION	_IOW(GH_IOCTL_TYPE, 0x4, struct gh_fn_desc)
#define GH_VM_REMOVE_FUNCTION	_IOW(GH_IOCTL_TYPE, 0x7, struct gh_fn_desc)

enum gh_vm_status {
	GH_VM_STATUS_LOAD_FAILED	= 1,
#define GH_VM_STATUS_LOAD_FAILED	GH_VM_STATUS_LOAD_FAILED
	GH_VM_STATUS_EXITED		= 2,
#define GH_VM_STATUS_EXITED		GH_VM_STATUS_EXITED
	GH_VM_STATUS_CRASHED		= 3,
#define GH_VM_STATUS_CRASHED		GH_VM_STATUS_CRASHED
};

/*
 * Gunyah presently sends max 4 bytes of exit_reason.
 * If that changes, this macro can be safely increased without breaking
 * userspace so long as struct gh_vcpu_run < PAGE_SIZE.
 */
#define GH_VM_MAX_EXIT_REASON_SIZE	8u

/**
 * struct gh_vm_exit_info - Reason for VM exit as reported by Gunyah
 * See Gunyah documentation for values.
 * @type: Describes how VM exited
 * @padding: padding bytes
 * @reason_size: Number of bytes valid for `reason`
 * @reason: See Gunyah documentation for interpretation. Note: these values are
 *          not interpreted by Linux and need to be converted from little-endian
 *          as applicable.
 */
struct gh_vm_exit_info {
	__u16 type;
	__u16 padding;
	__u32 reason_size;
	__u8 reason[GH_VM_MAX_EXIT_REASON_SIZE];
};

#define GH_VCPU_EXIT_UNKNOWN            0
#define GH_VCPU_EXIT_MMIO               1
#define GH_VCPU_EXIT_STATUS             2

/**
 * struct gh_vcpu_run - Application code obtains a pointer to the gh_vcpu_run
 *                      structure by mmap()ing a vcpu fd.
 * @immediate_exit: polled when scheduling the vcpu. If set, immediately returns -EINTR.
 * @padding: padding bytes
 * @exit_reason: Set when GH_VCPU_RUN returns successfully and gives reason why
 *               GH_VCPU_RUN has stopped running the vCPU.
 * @mmio: Used when exit_reason == GH_VCPU_EXIT_MMIO
 *        The guest has faulted on an memory-mapped I/O instruction that
 *        couldn't be satisfied by gunyah.
 * @mmio.phys_addr: Address guest tried to access
 * @mmio.data: the value that was written if `is_write == 1`. Filled by
 *        user for reads (`is_wite == 0`).
 * @mmio.len: Length of write. Only the first `len` bytes of `data`
 *       are considered by Gunyah.
 * @mmio.is_write: 1 if VM tried to perform a write, 0 for a read
 * @status: Used when exit_reason == GH_VCPU_EXIT_STATUS.
 *          The guest VM is no longer runnable. This struct informs why.
 * @status.status: See `enum gh_vm_status` for possible values
 * @status.exit_info: Used when status == GH_VM_STATUS_EXITED
 */
struct gh_vcpu_run {
	/* in */
	__u8 immediate_exit;
	__u8 padding[7];

	/* out */
	__u32 exit_reason;

	union {
		struct {
			__u64 phys_addr;
			__u8  data[8];
			__u32 len;
			__u8  is_write;
		} mmio;

		struct {
			enum gh_vm_status status;
			struct gh_vm_exit_info exit_info;
		} status;
	};
};

#define GH_VCPU_RUN		_IO(GH_IOCTL_TYPE, 0x5)
#define GH_VCPU_MMAP_SIZE	_IO(GH_IOCTL_TYPE, 0x6)

/**
 * ANDROID: android14-6.1 unfortunately contains UAPI that won't be carried
 * in kernel.org. Expose orthogonal ioctls that will never conflict with
 * kernel.org for these UAPIs. See b/268234781.
 */
#define GH_ANDROID_IOCTL_TYPE		'A'

#define GH_VM_ANDROID_LEND_USER_MEM	_IOW(GH_ANDROID_IOCTL_TYPE, 0x11, \
						struct gh_userspace_memory_region)

struct gh_vm_firmware_config {
	__u64 guest_phys_addr;
	__u64 size;
};

#define GH_VM_ANDROID_SET_FW_CONFIG	_IOW(GH_ANDROID_IOCTL_TYPE, 0x12, \
						struct gh_vm_firmware_config)

#endif
