/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (c) 2023 MediaTek Inc.
 */

/**
 * DOC: UAPI of GenieZone Hypervisor
 *
 * This file declares common data structure shared among user space,
 * kernel space, and GenieZone hypervisor.
 */
#ifndef __GZVM_H__
#define __GZVM_H__

#include <linux/const.h>
#include <linux/types.h>
#include <linux/ioctl.h>

#include <asm/gzvm_arch.h>

/* GZVM ioctls */
#define GZVM_IOC_MAGIC			0x92	/* gz */

/* ioctls for /dev/gzvm fds */
#define GZVM_CREATE_VM             _IO(GZVM_IOC_MAGIC,   0x01) /* Returns a Geniezone VM fd */

/*
 * Check if the given capability is supported or not.
 * The argument is capability. Ex. GZVM_CAP_ARM_PROTECTED_VM or GZVM_CAP_ARM_VM_IPA_SIZE
 * return is 0 (supported, no error)
 * return is -EOPNOTSUPP (unsupported)
 * return is -EFAULT (failed to get the argument from userspace)
 */
#define GZVM_CHECK_EXTENSION       _IO(GZVM_IOC_MAGIC,   0x03)

/* ioctls for VM fds */
/* for GZVM_SET_MEMORY_REGION */
struct gzvm_memory_region {
	__u32 slot;
	__u32 flags;
	__u64 guest_phys_addr;
	__u64 memory_size; /* bytes */
};

#define GZVM_SET_MEMORY_REGION     _IOW(GZVM_IOC_MAGIC,  0x40, \
					struct gzvm_memory_region)
/*
 * GZVM_CREATE_VCPU receives as a parameter the vcpu slot,
 * and returns a vcpu fd.
 */
#define GZVM_CREATE_VCPU           _IO(GZVM_IOC_MAGIC,   0x41)

/* for GZVM_SET_USER_MEMORY_REGION */
struct gzvm_userspace_memory_region {
	__u32 slot;
	__u32 flags;
	__u64 guest_phys_addr;
	/* bytes */
	__u64 memory_size;
	/* start of the userspace allocated memory */
	__u64 userspace_addr;
};

#define GZVM_SET_USER_MEMORY_REGION _IOW(GZVM_IOC_MAGIC, 0x46, \
					 struct gzvm_userspace_memory_region)

/*
 * ioctls for vcpu fds
 */
#define GZVM_RUN                   _IO(GZVM_IOC_MAGIC,   0x80)

/* VM exit reason */
enum {
	GZVM_EXIT_UNKNOWN = 0x92920000,
	GZVM_EXIT_MMIO = 0x92920001,
	GZVM_EXIT_HYPERCALL = 0x92920002,
	GZVM_EXIT_IRQ = 0x92920003,
	GZVM_EXIT_EXCEPTION = 0x92920004,
	GZVM_EXIT_DEBUG = 0x92920005,
	GZVM_EXIT_FAIL_ENTRY = 0x92920006,
	GZVM_EXIT_INTERNAL_ERROR = 0x92920007,
	GZVM_EXIT_SYSTEM_EVENT = 0x92920008,
	GZVM_EXIT_SHUTDOWN = 0x92920009,
	GZVM_EXIT_GZ = 0x9292000a,
};

/**
 * struct gzvm_vcpu_run: Same purpose as kvm_run, this struct is
 *			shared between userspace, kernel and
 *			GenieZone hypervisor
 * @exit_reason: The reason why gzvm_vcpu_run has stopped running the vCPU
 * @immediate_exit: Polled when the vcpu is scheduled.
 *                  If set, immediately returns -EINTR
 * @padding1: Reserved for future-proof and must be zero filled
 * @mmio: The nested struct in anonymous union. Handle mmio in host side
 * @phys_addr: The address guest tries to access
 * @data: The value to be written (is_write is 1) or
 *        be filled by user for reads (is_write is 0)
 * @size: The size of written data.
 *        Only the first `size` bytes of `data` are handled
 * @reg_nr: The register number where the data is stored
 * @is_write: 1 for VM to perform a write or 0 for VM to perform a read
 * @fail_entry: The nested struct in anonymous union.
 *              Handle invalid entry address at the first run
 * @hardware_entry_failure_reason: The reason codes about hardware entry failure
 * @cpu: The current processor number via smp_processor_id()
 * @exception: The nested struct in anonymous union.
 *             Handle exception occurred in VM
 * @exception: Which exception vector
 * @error_code: Exception error codes
 * @hypercall: The nested struct in anonymous union.
 *             Some hypercalls issued from VM must be handled
 * @args: The hypercall's arguments
 * @internal: The nested struct in anonymous union. The errors from hypervisor
 * @suberror: The errors codes about GZVM_EXIT_INTERNAL_ERROR
 * @ndata: The number of elements used in data[]
 * @data: Keep the detailed information about GZVM_EXIT_INTERNAL_ERROR
 * @system_event: The nested struct in anonymous union.
 *                VM's PSCI must be handled by host
 * @type: System event type.
 *        Ex. GZVM_SYSTEM_EVENT_SHUTDOWN or GZVM_SYSTEM_EVENT_RESET...etc.
 * @ndata: The number of elements used in data[]
 * @data: Keep the detailed information about GZVM_EXIT_SYSTEM_EVENT
 * @padding: Fix it to a reasonable size future-proof for keeping the same
 *           struct size when adding new variables in the union is needed
 *
 * Keep identical layout between the 3 modules
 */
struct gzvm_vcpu_run {
	/* to userspace */
	__u32 exit_reason;
	__u8 immediate_exit;
	__u8 padding1[3];
	/* union structure of collection of guest exit reason */
	union {
		/* GZVM_EXIT_MMIO */
		struct {
			/* from FAR_EL2 */
			__u64 phys_addr;
			__u8 data[8];
			/* from ESR_EL2 as */
			__u64 size;
			/* from ESR_EL2 */
			__u32 reg_nr;
			/* from ESR_EL2 */
			__u8 is_write;
		} mmio;
		/* GZVM_EXIT_FAIL_ENTRY */
		struct {
			__u64 hardware_entry_failure_reason;
			__u32 cpu;
		} fail_entry;
		/* GZVM_EXIT_EXCEPTION */
		struct {
			__u32 exception;
			__u32 error_code;
		} exception;
		/* GZVM_EXIT_HYPERCALL */
		struct {
			__u64 args[8];	/* in-out */
		} hypercall;
		/* GZVM_EXIT_INTERNAL_ERROR */
		struct {
			__u32 suberror;
			__u32 ndata;
			__u64 data[16];
		} internal;
		/* GZVM_EXIT_SYSTEM_EVENT */
		struct {
#define GZVM_SYSTEM_EVENT_SHUTDOWN       1
#define GZVM_SYSTEM_EVENT_RESET          2
#define GZVM_SYSTEM_EVENT_CRASH          3
#define GZVM_SYSTEM_EVENT_WAKEUP         4
#define GZVM_SYSTEM_EVENT_SUSPEND        5
#define GZVM_SYSTEM_EVENT_SEV_TERM       6
#define GZVM_SYSTEM_EVENT_S2IDLE         7
			__u32 type;
			__u32 ndata;
			__u64 data[16];
		} system_event;
		char padding[256];
	};
};

/* for GZVM_ENABLE_CAP */
struct gzvm_enable_cap {
	/* in */
	__u64 cap;
	/**
	 * we have total 5 (8 - 3) registers can be used for
	 * additional args
	 */
	__u64 args[5];
};

#define GZVM_ENABLE_CAP            _IOW(GZVM_IOC_MAGIC,  0xa3, \
					struct gzvm_enable_cap)

/* for GZVM_GET/SET_ONE_REG */
struct gzvm_one_reg {
	__u64 id;
	__u64 addr;
};

#define GZVM_GET_ONE_REG	   _IOW(GZVM_IOC_MAGIC,  0xab, \
					struct gzvm_one_reg)
#define GZVM_SET_ONE_REG	   _IOW(GZVM_IOC_MAGIC,  0xac, \
					struct gzvm_one_reg)

#define GZVM_REG_GENERIC	   0x0000000000000000ULL

#endif /* __GZVM_H__ */
