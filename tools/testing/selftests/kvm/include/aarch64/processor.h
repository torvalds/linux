/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AArch64 processor specific defines
 *
 * Copyright (C) 2018, Red Hat, Inc.
 */
#ifndef SELFTEST_KVM_PROCESSOR_H
#define SELFTEST_KVM_PROCESSOR_H

#include "kvm_util.h"
#include <linux/stringify.h>


#define ARM64_CORE_REG(x) (KVM_REG_ARM64 | KVM_REG_SIZE_U64 | \
			   KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(x))

#define CPACR_EL1               3, 0,  1, 0, 2
#define TCR_EL1                 3, 0,  2, 0, 2
#define MAIR_EL1                3, 0, 10, 2, 0
#define MPIDR_EL1               3, 0,  0, 0, 5
#define TTBR0_EL1               3, 0,  2, 0, 0
#define SCTLR_EL1               3, 0,  1, 0, 0
#define VBAR_EL1                3, 0, 12, 0, 0

#define ID_AA64DFR0_EL1         3, 0,  0, 5, 0

/*
 * Default MAIR
 *                  index   attribute
 * DEVICE_nGnRnE      0     0000:0000
 * DEVICE_nGnRE       1     0000:0100
 * DEVICE_GRE         2     0000:1100
 * NORMAL_NC          3     0100:0100
 * NORMAL             4     1111:1111
 * NORMAL_WT          5     1011:1011
 */
#define DEFAULT_MAIR_EL1 ((0x00ul << (0 * 8)) | \
			  (0x04ul << (1 * 8)) | \
			  (0x0cul << (2 * 8)) | \
			  (0x44ul << (3 * 8)) | \
			  (0xfful << (4 * 8)) | \
			  (0xbbul << (5 * 8)))

#define MPIDR_HWID_BITMASK (0xff00fffffful)

static inline void get_reg(struct kvm_vm *vm, uint32_t vcpuid, uint64_t id, uint64_t *addr)
{
	struct kvm_one_reg reg;
	reg.id = id;
	reg.addr = (uint64_t)addr;
	vcpu_ioctl(vm, vcpuid, KVM_GET_ONE_REG, &reg);
}

static inline void set_reg(struct kvm_vm *vm, uint32_t vcpuid, uint64_t id, uint64_t val)
{
	struct kvm_one_reg reg;
	reg.id = id;
	reg.addr = (uint64_t)&val;
	vcpu_ioctl(vm, vcpuid, KVM_SET_ONE_REG, &reg);
}

void aarch64_vcpu_setup(struct kvm_vm *vm, int vcpuid, struct kvm_vcpu_init *init);
void aarch64_vcpu_add_default(struct kvm_vm *vm, uint32_t vcpuid,
			      struct kvm_vcpu_init *init, void *guest_code);

struct ex_regs {
	u64 regs[31];
	u64 sp;
	u64 pc;
	u64 pstate;
};

#define VECTOR_NUM	16

enum {
	VECTOR_SYNC_CURRENT_SP0,
	VECTOR_IRQ_CURRENT_SP0,
	VECTOR_FIQ_CURRENT_SP0,
	VECTOR_ERROR_CURRENT_SP0,

	VECTOR_SYNC_CURRENT,
	VECTOR_IRQ_CURRENT,
	VECTOR_FIQ_CURRENT,
	VECTOR_ERROR_CURRENT,

	VECTOR_SYNC_LOWER_64,
	VECTOR_IRQ_LOWER_64,
	VECTOR_FIQ_LOWER_64,
	VECTOR_ERROR_LOWER_64,

	VECTOR_SYNC_LOWER_32,
	VECTOR_IRQ_LOWER_32,
	VECTOR_FIQ_LOWER_32,
	VECTOR_ERROR_LOWER_32,
};

#define VECTOR_IS_SYNC(v) ((v) == VECTOR_SYNC_CURRENT_SP0 || \
			   (v) == VECTOR_SYNC_CURRENT     || \
			   (v) == VECTOR_SYNC_LOWER_64    || \
			   (v) == VECTOR_SYNC_LOWER_32)

#define ESR_EC_NUM		64
#define ESR_EC_SHIFT		26
#define ESR_EC_MASK		(ESR_EC_NUM - 1)

#define ESR_EC_SVC64		0x15
#define ESR_EC_HW_BP_CURRENT	0x31
#define ESR_EC_SSTEP_CURRENT	0x33
#define ESR_EC_WP_CURRENT	0x35
#define ESR_EC_BRK_INS		0x3c

void vm_init_descriptor_tables(struct kvm_vm *vm);
void vcpu_init_descriptor_tables(struct kvm_vm *vm, uint32_t vcpuid);

typedef void(*handler_fn)(struct ex_regs *);
void vm_install_exception_handler(struct kvm_vm *vm,
		int vector, handler_fn handler);
void vm_install_sync_handler(struct kvm_vm *vm,
		int vector, int ec, handler_fn handler);

#define write_sysreg(reg, val)						  \
({									  \
	u64 __val = (u64)(val);						  \
	asm volatile("msr " __stringify(reg) ", %x0" : : "rZ" (__val));	  \
})

#define read_sysreg(reg)						  \
({	u64 val;							  \
	asm volatile("mrs %0, "__stringify(reg) : "=r"(val) : : "memory");\
	val;								  \
})

#define isb()	asm volatile("isb" : : : "memory")

#endif /* SELFTEST_KVM_PROCESSOR_H */
