/* SPDX-License-Identifier: GPL-2.0 */
/*
 * RISC-V processor specific defines
 *
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 */
#ifndef SELFTEST_KVM_PROCESSOR_H
#define SELFTEST_KVM_PROCESSOR_H

#include "kvm_util.h"
#include <linux/stringify.h>

static inline uint64_t __kvm_reg_id(uint64_t type, uint64_t idx,
				    uint64_t  size)
{
	return KVM_REG_RISCV | type | idx | size;
}

#if __riscv_xlen == 64
#define KVM_REG_SIZE_ULONG	KVM_REG_SIZE_U64
#else
#define KVM_REG_SIZE_ULONG	KVM_REG_SIZE_U32
#endif

#define RISCV_CONFIG_REG(name)	__kvm_reg_id(KVM_REG_RISCV_CONFIG, \
					     KVM_REG_RISCV_CONFIG_REG(name), \
					     KVM_REG_SIZE_ULONG)

#define RISCV_CORE_REG(name)	__kvm_reg_id(KVM_REG_RISCV_CORE, \
					     KVM_REG_RISCV_CORE_REG(name), \
					     KVM_REG_SIZE_ULONG)

#define RISCV_CSR_REG(name)	__kvm_reg_id(KVM_REG_RISCV_CSR, \
					     KVM_REG_RISCV_CSR_REG(name), \
					     KVM_REG_SIZE_ULONG)

#define RISCV_TIMER_REG(name)	__kvm_reg_id(KVM_REG_RISCV_TIMER, \
					     KVM_REG_RISCV_TIMER_REG(name), \
					     KVM_REG_SIZE_U64)

static inline void get_reg(struct kvm_vm *vm, uint32_t vcpuid, uint64_t id,
			   unsigned long *addr)
{
	struct kvm_one_reg reg;

	reg.id = id;
	reg.addr = (unsigned long)addr;
	vcpu_get_reg(vm, vcpuid, &reg);
}

static inline void set_reg(struct kvm_vm *vm, uint32_t vcpuid, uint64_t id,
			   unsigned long val)
{
	struct kvm_one_reg reg;

	reg.id = id;
	reg.addr = (unsigned long)&val;
	vcpu_set_reg(vm, vcpuid, &reg);
}

/* L3 index Bit[47:39] */
#define PGTBL_L3_INDEX_MASK			0x0000FF8000000000ULL
#define PGTBL_L3_INDEX_SHIFT			39
#define PGTBL_L3_BLOCK_SHIFT			39
#define PGTBL_L3_BLOCK_SIZE			0x0000008000000000ULL
#define PGTBL_L3_MAP_MASK			(~(PGTBL_L3_BLOCK_SIZE - 1))
/* L2 index Bit[38:30] */
#define PGTBL_L2_INDEX_MASK			0x0000007FC0000000ULL
#define PGTBL_L2_INDEX_SHIFT			30
#define PGTBL_L2_BLOCK_SHIFT			30
#define PGTBL_L2_BLOCK_SIZE			0x0000000040000000ULL
#define PGTBL_L2_MAP_MASK			(~(PGTBL_L2_BLOCK_SIZE - 1))
/* L1 index Bit[29:21] */
#define PGTBL_L1_INDEX_MASK			0x000000003FE00000ULL
#define PGTBL_L1_INDEX_SHIFT			21
#define PGTBL_L1_BLOCK_SHIFT			21
#define PGTBL_L1_BLOCK_SIZE			0x0000000000200000ULL
#define PGTBL_L1_MAP_MASK			(~(PGTBL_L1_BLOCK_SIZE - 1))
/* L0 index Bit[20:12] */
#define PGTBL_L0_INDEX_MASK			0x00000000001FF000ULL
#define PGTBL_L0_INDEX_SHIFT			12
#define PGTBL_L0_BLOCK_SHIFT			12
#define PGTBL_L0_BLOCK_SIZE			0x0000000000001000ULL
#define PGTBL_L0_MAP_MASK			(~(PGTBL_L0_BLOCK_SIZE - 1))

#define PGTBL_PTE_ADDR_MASK			0x003FFFFFFFFFFC00ULL
#define PGTBL_PTE_ADDR_SHIFT			10
#define PGTBL_PTE_RSW_MASK			0x0000000000000300ULL
#define PGTBL_PTE_RSW_SHIFT			8
#define PGTBL_PTE_DIRTY_MASK			0x0000000000000080ULL
#define PGTBL_PTE_DIRTY_SHIFT			7
#define PGTBL_PTE_ACCESSED_MASK			0x0000000000000040ULL
#define PGTBL_PTE_ACCESSED_SHIFT		6
#define PGTBL_PTE_GLOBAL_MASK			0x0000000000000020ULL
#define PGTBL_PTE_GLOBAL_SHIFT			5
#define PGTBL_PTE_USER_MASK			0x0000000000000010ULL
#define PGTBL_PTE_USER_SHIFT			4
#define PGTBL_PTE_EXECUTE_MASK			0x0000000000000008ULL
#define PGTBL_PTE_EXECUTE_SHIFT			3
#define PGTBL_PTE_WRITE_MASK			0x0000000000000004ULL
#define PGTBL_PTE_WRITE_SHIFT			2
#define PGTBL_PTE_READ_MASK			0x0000000000000002ULL
#define PGTBL_PTE_READ_SHIFT			1
#define PGTBL_PTE_PERM_MASK			(PGTBL_PTE_ACCESSED_MASK | \
						 PGTBL_PTE_DIRTY_MASK | \
						 PGTBL_PTE_EXECUTE_MASK | \
						 PGTBL_PTE_WRITE_MASK | \
						 PGTBL_PTE_READ_MASK)
#define PGTBL_PTE_VALID_MASK			0x0000000000000001ULL
#define PGTBL_PTE_VALID_SHIFT			0

#define PGTBL_PAGE_SIZE				PGTBL_L0_BLOCK_SIZE
#define PGTBL_PAGE_SIZE_SHIFT			PGTBL_L0_BLOCK_SHIFT

#define SATP_PPN				_AC(0x00000FFFFFFFFFFF, UL)
#define SATP_MODE_39				_AC(0x8000000000000000, UL)
#define SATP_MODE_48				_AC(0x9000000000000000, UL)
#define SATP_ASID_BITS				16
#define SATP_ASID_SHIFT				44
#define SATP_ASID_MASK				_AC(0xFFFF, UL)

#define SBI_EXT_EXPERIMENTAL_START	0x08000000
#define SBI_EXT_EXPERIMENTAL_END	0x08FFFFFF

#define KVM_RISCV_SELFTESTS_SBI_EXT	SBI_EXT_EXPERIMENTAL_END

struct sbiret {
	long error;
	long value;
};

struct sbiret sbi_ecall(int ext, int fid, unsigned long arg0,
			unsigned long arg1, unsigned long arg2,
			unsigned long arg3, unsigned long arg4,
			unsigned long arg5);

#endif /* SELFTEST_KVM_PROCESSOR_H */
