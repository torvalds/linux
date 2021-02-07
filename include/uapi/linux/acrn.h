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

/**
 * struct acrn_gp_regs - General registers of a User VM
 * @rax:	Value of register RAX
 * @rcx:	Value of register RCX
 * @rdx:	Value of register RDX
 * @rbx:	Value of register RBX
 * @rsp:	Value of register RSP
 * @rbp:	Value of register RBP
 * @rsi:	Value of register RSI
 * @rdi:	Value of register RDI
 * @r8:		Value of register R8
 * @r9:		Value of register R9
 * @r10:	Value of register R10
 * @r11:	Value of register R11
 * @r12:	Value of register R12
 * @r13:	Value of register R13
 * @r14:	Value of register R14
 * @r15:	Value of register R15
 */
struct acrn_gp_regs {
	__le64	rax;
	__le64	rcx;
	__le64	rdx;
	__le64	rbx;
	__le64	rsp;
	__le64	rbp;
	__le64	rsi;
	__le64	rdi;
	__le64	r8;
	__le64	r9;
	__le64	r10;
	__le64	r11;
	__le64	r12;
	__le64	r13;
	__le64	r14;
	__le64	r15;
};

/**
 * struct acrn_descriptor_ptr - Segment descriptor table of a User VM.
 * @limit:	Limit field.
 * @base:	Base field.
 * @reserved:	Reserved and must be 0.
 */
struct acrn_descriptor_ptr {
	__le16	limit;
	__le64	base;
	__le16	reserved[3];
} __attribute__ ((__packed__));

/**
 * struct acrn_regs - Registers structure of a User VM
 * @gprs:		General registers
 * @gdt:		Global Descriptor Table
 * @idt:		Interrupt Descriptor Table
 * @rip:		Value of register RIP
 * @cs_base:		Base of code segment selector
 * @cr0:		Value of register CR0
 * @cr4:		Value of register CR4
 * @cr3:		Value of register CR3
 * @ia32_efer:		Value of IA32_EFER MSR
 * @rflags:		Value of regsiter RFLAGS
 * @reserved_64:	Reserved and must be 0
 * @cs_ar:		Attribute field of code segment selector
 * @cs_limit:		Limit field of code segment selector
 * @reserved_32:	Reserved and must be 0
 * @cs_sel:		Value of code segment selector
 * @ss_sel:		Value of stack segment selector
 * @ds_sel:		Value of data segment selector
 * @es_sel:		Value of extra segment selector
 * @fs_sel:		Value of FS selector
 * @gs_sel:		Value of GS selector
 * @ldt_sel:		Value of LDT descriptor selector
 * @tr_sel:		Value of TSS descriptor selector
 */
struct acrn_regs {
	struct acrn_gp_regs		gprs;
	struct acrn_descriptor_ptr	gdt;
	struct acrn_descriptor_ptr	idt;

	__le64				rip;
	__le64				cs_base;
	__le64				cr0;
	__le64				cr4;
	__le64				cr3;
	__le64				ia32_efer;
	__le64				rflags;
	__le64				reserved_64[4];

	__le32				cs_ar;
	__le32				cs_limit;
	__le32				reserved_32[3];

	__le16				cs_sel;
	__le16				ss_sel;
	__le16				ds_sel;
	__le16				es_sel;
	__le16				fs_sel;
	__le16				gs_sel;
	__le16				ldt_sel;
	__le16				tr_sel;
};

/**
 * struct acrn_vcpu_regs - Info of vCPU registers state
 * @vcpu_id:	vCPU ID
 * @reserved:	Reserved and must be 0
 * @vcpu_regs:	vCPU registers state
 *
 * This structure will be passed to hypervisor directly.
 */
struct acrn_vcpu_regs {
	__u16			vcpu_id;
	__u16			reserved[3];
	struct acrn_regs	vcpu_regs;
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
#define ACRN_IOCTL_SET_VCPU_REGS	\
	_IOW(ACRN_IOCTL_TYPE, 0x16, struct acrn_vcpu_regs)

#endif /* _UAPI_ACRN_H */
