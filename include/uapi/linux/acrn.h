/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Userspace interface for /dev/acrn_hsm - ACRN Hypervisor Service Module
 *
 * This file can be used by applications that need to communicate with the HSM
 * via the ioctl interface.
 *
 * Copyright (C) 2021 Intel Corporation. All rights reserved.
 */

#ifndef _UAPI_ACRN_H
#define _UAPI_ACRN_H

#include <linux/types.h>
#include <linux/uuid.h>

/**
 * struct acrn_vm_creation - Info to create a User VM
 * @vmid:		User VM ID returned from the hypervisor
 * @reserved0:		Reserved and must be 0
 * @vcpu_num:		Number of vCPU in the VM. Return from hypervisor.
 * @reserved1:		Reserved and must be 0
 * @uuid:		UUID of the VM. Pass to hypervisor directly.
 * @vm_flag:		Flag of the VM creating. Pass to hypervisor directly.
 * @ioreq_buf:		Service VM GPA of I/O request buffer. Pass to
 *			hypervisor directly.
 * @cpu_affinity:	CPU affinity of the VM. Pass to hypervisor directly.
 * 			It's a bitmap which indicates CPUs used by the VM.
 */
struct acrn_vm_creation {
	__u16	vmid;
	__u16	reserved0;
	__u16	vcpu_num;
	__u16	reserved1;
	guid_t	uuid;
	__u64	vm_flag;
	__u64	ioreq_buf;
	__u64	cpu_affinity;
};

/* The ioctl type, documented in ioctl-number.rst */
#define ACRN_IOCTL_TYPE			0xA2

/*
 * Common IOCTL IDs definition for ACRN userspace
 */
#define ACRN_IOCTL_CREATE_VM		\
	_IOWR(ACRN_IOCTL_TYPE, 0x10, struct acrn_vm_creation)
#define ACRN_IOCTL_DESTROY_VM		\
	_IO(ACRN_IOCTL_TYPE, 0x11)
#define ACRN_IOCTL_START_VM		\
	_IO(ACRN_IOCTL_TYPE, 0x12)
#define ACRN_IOCTL_PAUSE_VM		\
	_IO(ACRN_IOCTL_TYPE, 0x13)
#define ACRN_IOCTL_RESET_VM		\
	_IO(ACRN_IOCTL_TYPE, 0x15)

#endif /* _UAPI_ACRN_H */
