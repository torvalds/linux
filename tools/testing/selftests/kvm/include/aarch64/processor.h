/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AArch64 processor specific defines
 *
 * Copyright (C) 2018, Red Hat, Inc.
 */
#ifndef SELFTEST_KVM_PROCESSOR_H
#define SELFTEST_KVM_PROCESSOR_H

#include "kvm_util.h"


#define ARM64_CORE_REG(x) (KVM_REG_ARM64 | KVM_REG_SIZE_U64 | \
			   KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(x))

#define CPACR_EL1	3, 0,  1, 0, 2
#define TCR_EL1		3, 0,  2, 0, 2
#define MAIR_EL1	3, 0, 10, 2, 0
#define TTBR0_EL1	3, 0,  2, 0, 0
#define SCTLR_EL1	3, 0,  1, 0, 0

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

#endif /* SELFTEST_KVM_PROCESSOR_H */
