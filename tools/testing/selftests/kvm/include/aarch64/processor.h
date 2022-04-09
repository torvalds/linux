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
#include <linux/types.h>
#include <asm/sysreg.h>


#define ARM64_CORE_REG(x) (KVM_REG_ARM64 | KVM_REG_SIZE_U64 | \
			   KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(x))

/*
 * KVM_ARM64_SYS_REG(sys_reg_id): Helper macro to convert
 * SYS_* register definitions in asm/sysreg.h to use in KVM
 * calls such as get_reg() and set_reg().
 */
#define KVM_ARM64_SYS_REG(sys_reg_id)			\
	ARM64_SYS_REG(sys_reg_Op0(sys_reg_id),		\
			sys_reg_Op1(sys_reg_id),	\
			sys_reg_CRn(sys_reg_id),	\
			sys_reg_CRm(sys_reg_id),	\
			sys_reg_Op2(sys_reg_id))

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

void aarch64_vcpu_setup(struct kvm_vm *vm, uint32_t vcpuid, struct kvm_vcpu_init *init);
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

void aarch64_get_supported_page_sizes(uint32_t ipa,
				      bool *ps4k, bool *ps16k, bool *ps64k);

void vm_init_descriptor_tables(struct kvm_vm *vm);
void vcpu_init_descriptor_tables(struct kvm_vm *vm, uint32_t vcpuid);

typedef void(*handler_fn)(struct ex_regs *);
void vm_install_exception_handler(struct kvm_vm *vm,
		int vector, handler_fn handler);
void vm_install_sync_handler(struct kvm_vm *vm,
		int vector, int ec, handler_fn handler);

static inline void cpu_relax(void)
{
	asm volatile("yield" ::: "memory");
}

#define isb()		asm volatile("isb" : : : "memory")
#define dsb(opt)	asm volatile("dsb " #opt : : : "memory")
#define dmb(opt)	asm volatile("dmb " #opt : : : "memory")

#define dma_wmb()	dmb(oshst)
#define __iowmb()	dma_wmb()

#define dma_rmb()	dmb(oshld)

#define __iormb(v)							\
({									\
	unsigned long tmp;						\
									\
	dma_rmb();							\
									\
	/*								\
	 * Courtesy of arch/arm64/include/asm/io.h:			\
	 * Create a dummy control dependency from the IO read to any	\
	 * later instructions. This ensures that a subsequent call	\
	 * to udelay() will be ordered due to the ISB in __delay().	\
	 */								\
	asm volatile("eor	%0, %1, %1\n"				\
		     "cbnz	%0, ."					\
		     : "=r" (tmp) : "r" ((unsigned long)(v))		\
		     : "memory");					\
})

static __always_inline void __raw_writel(u32 val, volatile void *addr)
{
	asm volatile("str %w0, [%1]" : : "rZ" (val), "r" (addr));
}

static __always_inline u32 __raw_readl(const volatile void *addr)
{
	u32 val;
	asm volatile("ldr %w0, [%1]" : "=r" (val) : "r" (addr));
	return val;
}

#define writel_relaxed(v,c)	((void)__raw_writel((__force u32)cpu_to_le32(v),(c)))
#define readl_relaxed(c)	({ u32 __r = le32_to_cpu((__force __le32)__raw_readl(c)); __r; })

#define writel(v,c)		({ __iowmb(); writel_relaxed((v),(c));})
#define readl(c)		({ u32 __v = readl_relaxed(c); __iormb(__v); __v; })

static inline void local_irq_enable(void)
{
	asm volatile("msr daifclr, #3" : : : "memory");
}

static inline void local_irq_disable(void)
{
	asm volatile("msr daifset, #3" : : : "memory");
}

/**
 * struct arm_smccc_res - Result from SMC/HVC call
 * @a0-a3 result values from registers 0 to 3
 */
struct arm_smccc_res {
	unsigned long a0;
	unsigned long a1;
	unsigned long a2;
	unsigned long a3;
};

/**
 * smccc_hvc - Invoke a SMCCC function using the hvc conduit
 * @function_id: the SMCCC function to be called
 * @arg0-arg6: SMCCC function arguments, corresponding to registers x1-x7
 * @res: pointer to write the return values from registers x0-x3
 *
 */
void smccc_hvc(uint32_t function_id, uint64_t arg0, uint64_t arg1,
	       uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5,
	       uint64_t arg6, struct arm_smccc_res *res);

#endif /* SELFTEST_KVM_PROCESSOR_H */
