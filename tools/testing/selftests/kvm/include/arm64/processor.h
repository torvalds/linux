/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AArch64 processor specific defines
 *
 * Copyright (C) 2018, Red Hat, Inc.
 */
#ifndef SELFTEST_KVM_PROCESSOR_H
#define SELFTEST_KVM_PROCESSOR_H

#include "kvm_util.h"
#include "ucall_common.h"

#include <linux/stringify.h>
#include <linux/types.h>
#include <asm/brk-imm.h>
#include <asm/esr.h>
#include <asm/sysreg.h>


#define ARM64_CORE_REG(x) (KVM_REG_ARM64 | KVM_REG_SIZE_U64 | \
			   KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(x))

/*
 * KVM_ARM64_SYS_REG(sys_reg_id): Helper macro to convert
 * SYS_* register definitions in asm/sysreg.h to use in KVM
 * calls such as vcpu_get_reg() and vcpu_set_reg().
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

/* Linux doesn't use these memory types, so let's define them. */
#define MAIR_ATTR_DEVICE_GRE	UL(0x0c)
#define MAIR_ATTR_NORMAL_WT	UL(0xbb)

#define MT_DEVICE_nGnRnE	0
#define MT_DEVICE_nGnRE		1
#define MT_DEVICE_GRE		2
#define MT_NORMAL_NC		3
#define MT_NORMAL		4
#define MT_NORMAL_WT		5

#define DEFAULT_MAIR_EL1							\
	(MAIR_ATTRIDX(MAIR_ATTR_DEVICE_nGnRnE, MT_DEVICE_nGnRnE) |		\
	 MAIR_ATTRIDX(MAIR_ATTR_DEVICE_nGnRE, MT_DEVICE_nGnRE) |		\
	 MAIR_ATTRIDX(MAIR_ATTR_DEVICE_GRE, MT_DEVICE_GRE) |			\
	 MAIR_ATTRIDX(MAIR_ATTR_NORMAL_NC, MT_NORMAL_NC) |			\
	 MAIR_ATTRIDX(MAIR_ATTR_NORMAL, MT_NORMAL) |				\
	 MAIR_ATTRIDX(MAIR_ATTR_NORMAL_WT, MT_NORMAL_WT))

/* TCR_EL1 specific flags */
#define TCR_T0SZ_OFFSET	0
#define TCR_T0SZ(x)		((UL(64) - (x)) << TCR_T0SZ_OFFSET)

#define TCR_IRGN0_SHIFT	8
#define TCR_IRGN0_MASK		(UL(3) << TCR_IRGN0_SHIFT)
#define TCR_IRGN0_NC		(UL(0) << TCR_IRGN0_SHIFT)
#define TCR_IRGN0_WBWA		(UL(1) << TCR_IRGN0_SHIFT)
#define TCR_IRGN0_WT		(UL(2) << TCR_IRGN0_SHIFT)
#define TCR_IRGN0_WBnWA	(UL(3) << TCR_IRGN0_SHIFT)

#define TCR_ORGN0_SHIFT	10
#define TCR_ORGN0_MASK		(UL(3) << TCR_ORGN0_SHIFT)
#define TCR_ORGN0_NC		(UL(0) << TCR_ORGN0_SHIFT)
#define TCR_ORGN0_WBWA		(UL(1) << TCR_ORGN0_SHIFT)
#define TCR_ORGN0_WT		(UL(2) << TCR_ORGN0_SHIFT)
#define TCR_ORGN0_WBnWA	(UL(3) << TCR_ORGN0_SHIFT)

#define TCR_SH0_SHIFT		12
#define TCR_SH0_MASK		(UL(3) << TCR_SH0_SHIFT)
#define TCR_SH0_INNER		(UL(3) << TCR_SH0_SHIFT)

#define TCR_TG0_SHIFT		14
#define TCR_TG0_MASK		(UL(3) << TCR_TG0_SHIFT)
#define TCR_TG0_4K		(UL(0) << TCR_TG0_SHIFT)
#define TCR_TG0_64K		(UL(1) << TCR_TG0_SHIFT)
#define TCR_TG0_16K		(UL(2) << TCR_TG0_SHIFT)

#define TCR_IPS_SHIFT		32
#define TCR_IPS_MASK		(UL(7) << TCR_IPS_SHIFT)
#define TCR_IPS_52_BITS	(UL(6) << TCR_IPS_SHIFT)
#define TCR_IPS_48_BITS	(UL(5) << TCR_IPS_SHIFT)
#define TCR_IPS_40_BITS	(UL(2) << TCR_IPS_SHIFT)
#define TCR_IPS_36_BITS	(UL(1) << TCR_IPS_SHIFT)

#define TCR_HA			(UL(1) << 39)
#define TCR_DS			(UL(1) << 59)

/*
 * AttrIndx[2:0] encoding (mapping attributes defined in the MAIR* registers).
 */
#define PTE_ATTRINDX(t)	((t) << 2)
#define PTE_ATTRINDX_MASK	GENMASK(4, 2)
#define PTE_ATTRINDX_SHIFT	2

#define PTE_VALID		BIT(0)
#define PGD_TYPE_TABLE		BIT(1)
#define PUD_TYPE_TABLE		BIT(1)
#define PMD_TYPE_TABLE		BIT(1)
#define PTE_TYPE_PAGE		BIT(1)

#define PTE_SHARED		(UL(3) << 8) /* SH[1:0], inner shareable */
#define PTE_AF			BIT(10)

#define PTE_ADDR_MASK(page_shift)	GENMASK(47, (page_shift))
#define PTE_ADDR_51_48			GENMASK(15, 12)
#define PTE_ADDR_51_48_SHIFT		12
#define PTE_ADDR_MASK_LPA2(page_shift)	GENMASK(49, (page_shift))
#define PTE_ADDR_51_50_LPA2		GENMASK(9, 8)
#define PTE_ADDR_51_50_LPA2_SHIFT	8

void aarch64_vcpu_setup(struct kvm_vcpu *vcpu, struct kvm_vcpu_init *init);
struct kvm_vcpu *aarch64_vcpu_add(struct kvm_vm *vm, uint32_t vcpu_id,
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

void aarch64_get_supported_page_sizes(uint32_t ipa, uint32_t *ipa4k,
					uint32_t *ipa16k, uint32_t *ipa64k);

void vm_init_descriptor_tables(struct kvm_vm *vm);
void vcpu_init_descriptor_tables(struct kvm_vcpu *vcpu);

typedef void(*handler_fn)(struct ex_regs *);
void vm_install_exception_handler(struct kvm_vm *vm,
		int vector, handler_fn handler);
void vm_install_sync_handler(struct kvm_vm *vm,
		int vector, int ec, handler_fn handler);

uint64_t *virt_get_pte_hva_at_level(struct kvm_vm *vm, vm_vaddr_t gva, int level);
uint64_t *virt_get_pte_hva(struct kvm_vm *vm, vm_vaddr_t gva);

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

static __always_inline void __raw_writeq(u64 val, volatile void *addr)
{
	asm volatile("str %0, [%1]" : : "rZ" (val), "r" (addr));
}

static __always_inline u64 __raw_readq(const volatile void *addr)
{
	u64 val;
	asm volatile("ldr %0, [%1]" : "=r" (val) : "r" (addr));
	return val;
}

#define writel_relaxed(v,c)	((void)__raw_writel((__force u32)cpu_to_le32(v),(c)))
#define readl_relaxed(c)	({ u32 __r = le32_to_cpu((__force __le32)__raw_readl(c)); __r; })
#define writeq_relaxed(v,c)	((void)__raw_writeq((__force u64)cpu_to_le64(v),(c)))
#define readq_relaxed(c)	({ u64 __r = le64_to_cpu((__force __le64)__raw_readq(c)); __r; })

#define writel(v,c)		({ __iowmb(); writel_relaxed((v),(c));})
#define readl(c)		({ u32 __v = readl_relaxed(c); __iormb(__v); __v; })
#define writeq(v,c)		({ __iowmb(); writeq_relaxed((v),(c));})
#define readq(c)		({ u64 __v = readq_relaxed(c); __iormb(__v); __v; })


static inline void local_irq_enable(void)
{
	asm volatile("msr daifclr, #3" : : : "memory");
}

static inline void local_irq_disable(void)
{
	asm volatile("msr daifset, #3" : : : "memory");
}

static inline void local_serror_enable(void)
{
	asm volatile("msr daifclr, #4" : : : "memory");
}

static inline void local_serror_disable(void)
{
	asm volatile("msr daifset, #4" : : : "memory");
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

/**
 * smccc_smc - Invoke a SMCCC function using the smc conduit
 * @function_id: the SMCCC function to be called
 * @arg0-arg6: SMCCC function arguments, corresponding to registers x1-x7
 * @res: pointer to write the return values from registers x0-x3
 *
 */
void smccc_smc(uint32_t function_id, uint64_t arg0, uint64_t arg1,
	       uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5,
	       uint64_t arg6, struct arm_smccc_res *res);

/* Execute a Wait For Interrupt instruction. */
void wfi(void);

void test_wants_mte(void);
void test_disable_default_vgic(void);

bool vm_supports_el2(struct kvm_vm *vm);

static inline bool test_supports_el2(void)
{
	struct kvm_vm *vm = vm_create(1);
	bool supported = vm_supports_el2(vm);

	kvm_vm_free(vm);
	return supported;
}

static inline bool vcpu_has_el2(struct kvm_vcpu *vcpu)
{
	return vcpu->init.features[0] & BIT(KVM_ARM_VCPU_HAS_EL2);
}

#define MAPPED_EL2_SYSREG(el2, el1)		\
	case SYS_##el1:				\
		if (vcpu_has_el2(vcpu))		\
			alias = SYS_##el2;	\
		break


static __always_inline u64 ctxt_reg_alias(struct kvm_vcpu *vcpu, u32 encoding)
{
	u32 alias = encoding;

	BUILD_BUG_ON(!__builtin_constant_p(encoding));

	switch (encoding) {
	MAPPED_EL2_SYSREG(SCTLR_EL2,		SCTLR_EL1);
	MAPPED_EL2_SYSREG(CPTR_EL2,		CPACR_EL1);
	MAPPED_EL2_SYSREG(TTBR0_EL2,		TTBR0_EL1);
	MAPPED_EL2_SYSREG(TTBR1_EL2,		TTBR1_EL1);
	MAPPED_EL2_SYSREG(TCR_EL2,		TCR_EL1);
	MAPPED_EL2_SYSREG(VBAR_EL2,		VBAR_EL1);
	MAPPED_EL2_SYSREG(AFSR0_EL2,		AFSR0_EL1);
	MAPPED_EL2_SYSREG(AFSR1_EL2,		AFSR1_EL1);
	MAPPED_EL2_SYSREG(ESR_EL2,		ESR_EL1);
	MAPPED_EL2_SYSREG(FAR_EL2,		FAR_EL1);
	MAPPED_EL2_SYSREG(MAIR_EL2,		MAIR_EL1);
	MAPPED_EL2_SYSREG(TCR2_EL2,		TCR2_EL1);
	MAPPED_EL2_SYSREG(PIR_EL2,		PIR_EL1);
	MAPPED_EL2_SYSREG(PIRE0_EL2,		PIRE0_EL1);
	MAPPED_EL2_SYSREG(POR_EL2,		POR_EL1);
	MAPPED_EL2_SYSREG(AMAIR_EL2,		AMAIR_EL1);
	MAPPED_EL2_SYSREG(ELR_EL2,		ELR_EL1);
	MAPPED_EL2_SYSREG(SPSR_EL2,		SPSR_EL1);
	MAPPED_EL2_SYSREG(ZCR_EL2,		ZCR_EL1);
	MAPPED_EL2_SYSREG(CONTEXTIDR_EL2,	CONTEXTIDR_EL1);
	MAPPED_EL2_SYSREG(SCTLR2_EL2,		SCTLR2_EL1);
	MAPPED_EL2_SYSREG(CNTHCTL_EL2,		CNTKCTL_EL1);
	case SYS_SP_EL1:
		if (!vcpu_has_el2(vcpu))
			return ARM64_CORE_REG(sp_el1);

		alias = SYS_SP_EL2;
		break;
	default:
		BUILD_BUG();
	}

	return KVM_ARM64_SYS_REG(alias);
}

void kvm_get_default_vcpu_target(struct kvm_vm *vm, struct kvm_vcpu_init *init);

static inline unsigned int get_current_el(void)
{
	return (read_sysreg(CurrentEL) >> 2) & 0x3;
}

#define do_smccc(...)				\
do {						\
	if (get_current_el() == 2)		\
		smccc_smc(__VA_ARGS__);		\
	else					\
		smccc_hvc(__VA_ARGS__);		\
} while (0)

#endif /* SELFTEST_KVM_PROCESSOR_H */
