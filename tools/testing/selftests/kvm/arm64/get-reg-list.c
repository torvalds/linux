// SPDX-License-Identifier: GPL-2.0
/*
 * Check for KVM_GET_REG_LIST regressions.
 *
 * Copyright (C) 2020, Red Hat, Inc.
 *
 * While the blessed list should be created from the oldest possible
 * kernel, we can't go older than v5.2, though, because that's the first
 * release which includes df205b5c6328 ("KVM: arm64: Filter out invalid
 * core register IDs in KVM_GET_REG_LIST"). Without that commit the core
 * registers won't match expectations.
 */
#include <stdio.h>
#include "kvm_util.h"
#include "test_util.h"
#include "processor.h"

struct feature_id_reg {
	__u64 reg;
	__u64 id_reg;
	__u64 feat_shift;
	__u64 feat_min;
};

static struct feature_id_reg feat_id_regs[] = {
	{
		ARM64_SYS_REG(3, 0, 2, 0, 3),	/* TCR2_EL1 */
		ARM64_SYS_REG(3, 0, 0, 7, 3),	/* ID_AA64MMFR3_EL1 */
		0,
		1
	},
	{
		ARM64_SYS_REG(3, 0, 10, 2, 2),	/* PIRE0_EL1 */
		ARM64_SYS_REG(3, 0, 0, 7, 3),	/* ID_AA64MMFR3_EL1 */
		8,
		1
	},
	{
		ARM64_SYS_REG(3, 0, 10, 2, 3),	/* PIR_EL1 */
		ARM64_SYS_REG(3, 0, 0, 7, 3),	/* ID_AA64MMFR3_EL1 */
		8,
		1
	},
	{
		ARM64_SYS_REG(3, 0, 10, 2, 4),	/* POR_EL1 */
		ARM64_SYS_REG(3, 0, 0, 7, 3),	/* ID_AA64MMFR3_EL1 */
		16,
		1
	},
	{
		ARM64_SYS_REG(3, 3, 10, 2, 4),	/* POR_EL0 */
		ARM64_SYS_REG(3, 0, 0, 7, 3),	/* ID_AA64MMFR3_EL1 */
		16,
		1
	}
};

bool filter_reg(__u64 reg)
{
	/*
	 * DEMUX register presence depends on the host's CLIDR_EL1.
	 * This means there's no set of them that we can bless.
	 */
	if ((reg & KVM_REG_ARM_COPROC_MASK) == KVM_REG_ARM_DEMUX)
		return true;

	return false;
}

static bool check_supported_feat_reg(struct kvm_vcpu *vcpu, __u64 reg)
{
	int i, ret;
	__u64 data, feat_val;

	for (i = 0; i < ARRAY_SIZE(feat_id_regs); i++) {
		if (feat_id_regs[i].reg == reg) {
			ret = __vcpu_get_reg(vcpu, feat_id_regs[i].id_reg, &data);
			if (ret < 0)
				return false;

			feat_val = ((data >> feat_id_regs[i].feat_shift) & 0xf);
			return feat_val >= feat_id_regs[i].feat_min;
		}
	}

	return true;
}

bool check_supported_reg(struct kvm_vcpu *vcpu, __u64 reg)
{
	return check_supported_feat_reg(vcpu, reg);
}

bool check_reject_set(int err)
{
	return err == EPERM;
}

void finalize_vcpu(struct kvm_vcpu *vcpu, struct vcpu_reg_list *c)
{
	struct vcpu_reg_sublist *s;
	int feature;

	for_each_sublist(c, s) {
		if (s->finalize) {
			feature = s->feature;
			vcpu_ioctl(vcpu, KVM_ARM_VCPU_FINALIZE, &feature);
		}
	}
}

#define REG_MASK (KVM_REG_ARCH_MASK | KVM_REG_SIZE_MASK | KVM_REG_ARM_COPROC_MASK)

#define CORE_REGS_XX_NR_WORDS	2
#define CORE_SPSR_XX_NR_WORDS	2
#define CORE_FPREGS_XX_NR_WORDS	4

static const char *core_id_to_str(const char *prefix, __u64 id)
{
	__u64 core_off = id & ~REG_MASK, idx;

	/*
	 * core_off is the offset into struct kvm_regs
	 */
	switch (core_off) {
	case KVM_REG_ARM_CORE_REG(regs.regs[0]) ...
	     KVM_REG_ARM_CORE_REG(regs.regs[30]):
		idx = (core_off - KVM_REG_ARM_CORE_REG(regs.regs[0])) / CORE_REGS_XX_NR_WORDS;
		TEST_ASSERT(idx < 31, "%s: Unexpected regs.regs index: %lld", prefix, idx);
		return strdup_printf("KVM_REG_ARM_CORE_REG(regs.regs[%lld])", idx);
	case KVM_REG_ARM_CORE_REG(regs.sp):
		return "KVM_REG_ARM_CORE_REG(regs.sp)";
	case KVM_REG_ARM_CORE_REG(regs.pc):
		return "KVM_REG_ARM_CORE_REG(regs.pc)";
	case KVM_REG_ARM_CORE_REG(regs.pstate):
		return "KVM_REG_ARM_CORE_REG(regs.pstate)";
	case KVM_REG_ARM_CORE_REG(sp_el1):
		return "KVM_REG_ARM_CORE_REG(sp_el1)";
	case KVM_REG_ARM_CORE_REG(elr_el1):
		return "KVM_REG_ARM_CORE_REG(elr_el1)";
	case KVM_REG_ARM_CORE_REG(spsr[0]) ...
	     KVM_REG_ARM_CORE_REG(spsr[KVM_NR_SPSR - 1]):
		idx = (core_off - KVM_REG_ARM_CORE_REG(spsr[0])) / CORE_SPSR_XX_NR_WORDS;
		TEST_ASSERT(idx < KVM_NR_SPSR, "%s: Unexpected spsr index: %lld", prefix, idx);
		return strdup_printf("KVM_REG_ARM_CORE_REG(spsr[%lld])", idx);
	case KVM_REG_ARM_CORE_REG(fp_regs.vregs[0]) ...
	     KVM_REG_ARM_CORE_REG(fp_regs.vregs[31]):
		idx = (core_off - KVM_REG_ARM_CORE_REG(fp_regs.vregs[0])) / CORE_FPREGS_XX_NR_WORDS;
		TEST_ASSERT(idx < 32, "%s: Unexpected fp_regs.vregs index: %lld", prefix, idx);
		return strdup_printf("KVM_REG_ARM_CORE_REG(fp_regs.vregs[%lld])", idx);
	case KVM_REG_ARM_CORE_REG(fp_regs.fpsr):
		return "KVM_REG_ARM_CORE_REG(fp_regs.fpsr)";
	case KVM_REG_ARM_CORE_REG(fp_regs.fpcr):
		return "KVM_REG_ARM_CORE_REG(fp_regs.fpcr)";
	}

	TEST_FAIL("%s: Unknown core reg id: 0x%llx", prefix, id);
	return NULL;
}

static const char *sve_id_to_str(const char *prefix, __u64 id)
{
	__u64 sve_off, n, i;

	if (id == KVM_REG_ARM64_SVE_VLS)
		return "KVM_REG_ARM64_SVE_VLS";

	sve_off = id & ~(REG_MASK | ((1ULL << 5) - 1));
	i = id & (KVM_ARM64_SVE_MAX_SLICES - 1);

	TEST_ASSERT(i == 0, "%s: Currently we don't expect slice > 0, reg id 0x%llx", prefix, id);

	switch (sve_off) {
	case KVM_REG_ARM64_SVE_ZREG_BASE ...
	     KVM_REG_ARM64_SVE_ZREG_BASE + (1ULL << 5) * KVM_ARM64_SVE_NUM_ZREGS - 1:
		n = (id >> 5) & (KVM_ARM64_SVE_NUM_ZREGS - 1);
		TEST_ASSERT(id == KVM_REG_ARM64_SVE_ZREG(n, 0),
			    "%s: Unexpected bits set in SVE ZREG id: 0x%llx", prefix, id);
		return strdup_printf("KVM_REG_ARM64_SVE_ZREG(%lld, 0)", n);
	case KVM_REG_ARM64_SVE_PREG_BASE ...
	     KVM_REG_ARM64_SVE_PREG_BASE + (1ULL << 5) * KVM_ARM64_SVE_NUM_PREGS - 1:
		n = (id >> 5) & (KVM_ARM64_SVE_NUM_PREGS - 1);
		TEST_ASSERT(id == KVM_REG_ARM64_SVE_PREG(n, 0),
			    "%s: Unexpected bits set in SVE PREG id: 0x%llx", prefix, id);
		return strdup_printf("KVM_REG_ARM64_SVE_PREG(%lld, 0)", n);
	case KVM_REG_ARM64_SVE_FFR_BASE:
		TEST_ASSERT(id == KVM_REG_ARM64_SVE_FFR(0),
			    "%s: Unexpected bits set in SVE FFR id: 0x%llx", prefix, id);
		return "KVM_REG_ARM64_SVE_FFR(0)";
	}

	return NULL;
}

void print_reg(const char *prefix, __u64 id)
{
	unsigned op0, op1, crn, crm, op2;
	const char *reg_size = NULL;

	TEST_ASSERT((id & KVM_REG_ARCH_MASK) == KVM_REG_ARM64,
		    "%s: KVM_REG_ARM64 missing in reg id: 0x%llx", prefix, id);

	switch (id & KVM_REG_SIZE_MASK) {
	case KVM_REG_SIZE_U8:
		reg_size = "KVM_REG_SIZE_U8";
		break;
	case KVM_REG_SIZE_U16:
		reg_size = "KVM_REG_SIZE_U16";
		break;
	case KVM_REG_SIZE_U32:
		reg_size = "KVM_REG_SIZE_U32";
		break;
	case KVM_REG_SIZE_U64:
		reg_size = "KVM_REG_SIZE_U64";
		break;
	case KVM_REG_SIZE_U128:
		reg_size = "KVM_REG_SIZE_U128";
		break;
	case KVM_REG_SIZE_U256:
		reg_size = "KVM_REG_SIZE_U256";
		break;
	case KVM_REG_SIZE_U512:
		reg_size = "KVM_REG_SIZE_U512";
		break;
	case KVM_REG_SIZE_U1024:
		reg_size = "KVM_REG_SIZE_U1024";
		break;
	case KVM_REG_SIZE_U2048:
		reg_size = "KVM_REG_SIZE_U2048";
		break;
	default:
		TEST_FAIL("%s: Unexpected reg size: 0x%llx in reg id: 0x%llx",
			  prefix, (id & KVM_REG_SIZE_MASK) >> KVM_REG_SIZE_SHIFT, id);
	}

	switch (id & KVM_REG_ARM_COPROC_MASK) {
	case KVM_REG_ARM_CORE:
		printf("\tKVM_REG_ARM64 | %s | KVM_REG_ARM_CORE | %s,\n", reg_size, core_id_to_str(prefix, id));
		break;
	case KVM_REG_ARM_DEMUX:
		TEST_ASSERT(!(id & ~(REG_MASK | KVM_REG_ARM_DEMUX_ID_MASK | KVM_REG_ARM_DEMUX_VAL_MASK)),
			    "%s: Unexpected bits set in DEMUX reg id: 0x%llx", prefix, id);
		printf("\tKVM_REG_ARM64 | %s | KVM_REG_ARM_DEMUX | KVM_REG_ARM_DEMUX_ID_CCSIDR | %lld,\n",
		       reg_size, id & KVM_REG_ARM_DEMUX_VAL_MASK);
		break;
	case KVM_REG_ARM64_SYSREG:
		op0 = (id & KVM_REG_ARM64_SYSREG_OP0_MASK) >> KVM_REG_ARM64_SYSREG_OP0_SHIFT;
		op1 = (id & KVM_REG_ARM64_SYSREG_OP1_MASK) >> KVM_REG_ARM64_SYSREG_OP1_SHIFT;
		crn = (id & KVM_REG_ARM64_SYSREG_CRN_MASK) >> KVM_REG_ARM64_SYSREG_CRN_SHIFT;
		crm = (id & KVM_REG_ARM64_SYSREG_CRM_MASK) >> KVM_REG_ARM64_SYSREG_CRM_SHIFT;
		op2 = (id & KVM_REG_ARM64_SYSREG_OP2_MASK) >> KVM_REG_ARM64_SYSREG_OP2_SHIFT;
		TEST_ASSERT(id == ARM64_SYS_REG(op0, op1, crn, crm, op2),
			    "%s: Unexpected bits set in SYSREG reg id: 0x%llx", prefix, id);
		printf("\tARM64_SYS_REG(%d, %d, %d, %d, %d),\n", op0, op1, crn, crm, op2);
		break;
	case KVM_REG_ARM_FW:
		TEST_ASSERT(id == KVM_REG_ARM_FW_REG(id & 0xffff),
			    "%s: Unexpected bits set in FW reg id: 0x%llx", prefix, id);
		printf("\tKVM_REG_ARM_FW_REG(%lld),\n", id & 0xffff);
		break;
	case KVM_REG_ARM_FW_FEAT_BMAP:
		TEST_ASSERT(id == KVM_REG_ARM_FW_FEAT_BMAP_REG(id & 0xffff),
			    "%s: Unexpected bits set in the bitmap feature FW reg id: 0x%llx", prefix, id);
		printf("\tKVM_REG_ARM_FW_FEAT_BMAP_REG(%lld),\n", id & 0xffff);
		break;
	case KVM_REG_ARM64_SVE:
		printf("\t%s,\n", sve_id_to_str(prefix, id));
		break;
	default:
		TEST_FAIL("%s: Unexpected coproc type: 0x%llx in reg id: 0x%llx",
			  prefix, (id & KVM_REG_ARM_COPROC_MASK) >> KVM_REG_ARM_COPROC_SHIFT, id);
	}
}

/*
 * The original blessed list was primed with the output of kernel version
 * v4.15 with --core-reg-fixup and then later updated with new registers.
 * (The --core-reg-fixup option and it's fixup function have been removed
 * from the test, as it's unlikely to use this type of test on a kernel
 * older than v5.2.)
 *
 * The blessed list is up to date with kernel version v6.4 (or so we hope)
 */
static __u64 base_regs[] = {
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[0]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[1]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[2]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[3]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[4]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[5]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[6]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[7]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[8]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[9]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[10]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[11]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[12]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[13]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[14]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[15]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[16]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[17]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[18]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[19]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[20]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[21]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[22]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[23]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[24]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[25]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[26]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[27]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[28]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[29]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[30]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.sp),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.pc),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.pstate),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(sp_el1),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(elr_el1),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(spsr[0]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(spsr[1]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(spsr[2]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(spsr[3]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(spsr[4]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U32 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.fpsr),
	KVM_REG_ARM64 | KVM_REG_SIZE_U32 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.fpcr),
	KVM_REG_ARM_FW_REG(0),		/* KVM_REG_ARM_PSCI_VERSION */
	KVM_REG_ARM_FW_REG(1),		/* KVM_REG_ARM_SMCCC_ARCH_WORKAROUND_1 */
	KVM_REG_ARM_FW_REG(2),		/* KVM_REG_ARM_SMCCC_ARCH_WORKAROUND_2 */
	KVM_REG_ARM_FW_REG(3),		/* KVM_REG_ARM_SMCCC_ARCH_WORKAROUND_3 */
	KVM_REG_ARM_FW_FEAT_BMAP_REG(0),	/* KVM_REG_ARM_STD_BMAP */
	KVM_REG_ARM_FW_FEAT_BMAP_REG(1),	/* KVM_REG_ARM_STD_HYP_BMAP */
	KVM_REG_ARM_FW_FEAT_BMAP_REG(2),	/* KVM_REG_ARM_VENDOR_HYP_BMAP */
	ARM64_SYS_REG(3, 3, 14, 3, 1),	/* CNTV_CTL_EL0 */
	ARM64_SYS_REG(3, 3, 14, 3, 2),	/* CNTV_CVAL_EL0 */
	ARM64_SYS_REG(3, 3, 14, 0, 2),
	ARM64_SYS_REG(3, 0, 0, 0, 0),	/* MIDR_EL1 */
	ARM64_SYS_REG(3, 0, 0, 0, 6),	/* REVIDR_EL1 */
	ARM64_SYS_REG(3, 1, 0, 0, 1),	/* CLIDR_EL1 */
	ARM64_SYS_REG(3, 1, 0, 0, 7),	/* AIDR_EL1 */
	ARM64_SYS_REG(3, 3, 0, 0, 1),	/* CTR_EL0 */
	ARM64_SYS_REG(2, 0, 0, 0, 4),
	ARM64_SYS_REG(2, 0, 0, 0, 5),
	ARM64_SYS_REG(2, 0, 0, 0, 6),
	ARM64_SYS_REG(2, 0, 0, 0, 7),
	ARM64_SYS_REG(2, 0, 0, 1, 4),
	ARM64_SYS_REG(2, 0, 0, 1, 5),
	ARM64_SYS_REG(2, 0, 0, 1, 6),
	ARM64_SYS_REG(2, 0, 0, 1, 7),
	ARM64_SYS_REG(2, 0, 0, 2, 0),	/* MDCCINT_EL1 */
	ARM64_SYS_REG(2, 0, 0, 2, 2),	/* MDSCR_EL1 */
	ARM64_SYS_REG(2, 0, 0, 2, 4),
	ARM64_SYS_REG(2, 0, 0, 2, 5),
	ARM64_SYS_REG(2, 0, 0, 2, 6),
	ARM64_SYS_REG(2, 0, 0, 2, 7),
	ARM64_SYS_REG(2, 0, 0, 3, 4),
	ARM64_SYS_REG(2, 0, 0, 3, 5),
	ARM64_SYS_REG(2, 0, 0, 3, 6),
	ARM64_SYS_REG(2, 0, 0, 3, 7),
	ARM64_SYS_REG(2, 0, 0, 4, 4),
	ARM64_SYS_REG(2, 0, 0, 4, 5),
	ARM64_SYS_REG(2, 0, 0, 4, 6),
	ARM64_SYS_REG(2, 0, 0, 4, 7),
	ARM64_SYS_REG(2, 0, 0, 5, 4),
	ARM64_SYS_REG(2, 0, 0, 5, 5),
	ARM64_SYS_REG(2, 0, 0, 5, 6),
	ARM64_SYS_REG(2, 0, 0, 5, 7),
	ARM64_SYS_REG(2, 0, 0, 6, 4),
	ARM64_SYS_REG(2, 0, 0, 6, 5),
	ARM64_SYS_REG(2, 0, 0, 6, 6),
	ARM64_SYS_REG(2, 0, 0, 6, 7),
	ARM64_SYS_REG(2, 0, 0, 7, 4),
	ARM64_SYS_REG(2, 0, 0, 7, 5),
	ARM64_SYS_REG(2, 0, 0, 7, 6),
	ARM64_SYS_REG(2, 0, 0, 7, 7),
	ARM64_SYS_REG(2, 0, 0, 8, 4),
	ARM64_SYS_REG(2, 0, 0, 8, 5),
	ARM64_SYS_REG(2, 0, 0, 8, 6),
	ARM64_SYS_REG(2, 0, 0, 8, 7),
	ARM64_SYS_REG(2, 0, 0, 9, 4),
	ARM64_SYS_REG(2, 0, 0, 9, 5),
	ARM64_SYS_REG(2, 0, 0, 9, 6),
	ARM64_SYS_REG(2, 0, 0, 9, 7),
	ARM64_SYS_REG(2, 0, 0, 10, 4),
	ARM64_SYS_REG(2, 0, 0, 10, 5),
	ARM64_SYS_REG(2, 0, 0, 10, 6),
	ARM64_SYS_REG(2, 0, 0, 10, 7),
	ARM64_SYS_REG(2, 0, 0, 11, 4),
	ARM64_SYS_REG(2, 0, 0, 11, 5),
	ARM64_SYS_REG(2, 0, 0, 11, 6),
	ARM64_SYS_REG(2, 0, 0, 11, 7),
	ARM64_SYS_REG(2, 0, 0, 12, 4),
	ARM64_SYS_REG(2, 0, 0, 12, 5),
	ARM64_SYS_REG(2, 0, 0, 12, 6),
	ARM64_SYS_REG(2, 0, 0, 12, 7),
	ARM64_SYS_REG(2, 0, 0, 13, 4),
	ARM64_SYS_REG(2, 0, 0, 13, 5),
	ARM64_SYS_REG(2, 0, 0, 13, 6),
	ARM64_SYS_REG(2, 0, 0, 13, 7),
	ARM64_SYS_REG(2, 0, 0, 14, 4),
	ARM64_SYS_REG(2, 0, 0, 14, 5),
	ARM64_SYS_REG(2, 0, 0, 14, 6),
	ARM64_SYS_REG(2, 0, 0, 14, 7),
	ARM64_SYS_REG(2, 0, 0, 15, 4),
	ARM64_SYS_REG(2, 0, 0, 15, 5),
	ARM64_SYS_REG(2, 0, 0, 15, 6),
	ARM64_SYS_REG(2, 0, 0, 15, 7),
	ARM64_SYS_REG(2, 0, 1, 1, 4),	/* OSLSR_EL1 */
	ARM64_SYS_REG(2, 4, 0, 7, 0),	/* DBGVCR32_EL2 */
	ARM64_SYS_REG(3, 0, 0, 0, 5),	/* MPIDR_EL1 */
	ARM64_SYS_REG(3, 0, 0, 1, 0),	/* ID_PFR0_EL1 */
	ARM64_SYS_REG(3, 0, 0, 1, 1),	/* ID_PFR1_EL1 */
	ARM64_SYS_REG(3, 0, 0, 1, 2),	/* ID_DFR0_EL1 */
	ARM64_SYS_REG(3, 0, 0, 1, 3),	/* ID_AFR0_EL1 */
	ARM64_SYS_REG(3, 0, 0, 1, 4),	/* ID_MMFR0_EL1 */
	ARM64_SYS_REG(3, 0, 0, 1, 5),	/* ID_MMFR1_EL1 */
	ARM64_SYS_REG(3, 0, 0, 1, 6),	/* ID_MMFR2_EL1 */
	ARM64_SYS_REG(3, 0, 0, 1, 7),	/* ID_MMFR3_EL1 */
	ARM64_SYS_REG(3, 0, 0, 2, 0),	/* ID_ISAR0_EL1 */
	ARM64_SYS_REG(3, 0, 0, 2, 1),	/* ID_ISAR1_EL1 */
	ARM64_SYS_REG(3, 0, 0, 2, 2),	/* ID_ISAR2_EL1 */
	ARM64_SYS_REG(3, 0, 0, 2, 3),	/* ID_ISAR3_EL1 */
	ARM64_SYS_REG(3, 0, 0, 2, 4),	/* ID_ISAR4_EL1 */
	ARM64_SYS_REG(3, 0, 0, 2, 5),	/* ID_ISAR5_EL1 */
	ARM64_SYS_REG(3, 0, 0, 2, 6),	/* ID_MMFR4_EL1 */
	ARM64_SYS_REG(3, 0, 0, 2, 7),	/* ID_ISAR6_EL1 */
	ARM64_SYS_REG(3, 0, 0, 3, 0),	/* MVFR0_EL1 */
	ARM64_SYS_REG(3, 0, 0, 3, 1),	/* MVFR1_EL1 */
	ARM64_SYS_REG(3, 0, 0, 3, 2),	/* MVFR2_EL1 */
	ARM64_SYS_REG(3, 0, 0, 3, 3),
	ARM64_SYS_REG(3, 0, 0, 3, 4),	/* ID_PFR2_EL1 */
	ARM64_SYS_REG(3, 0, 0, 3, 5),	/* ID_DFR1_EL1 */
	ARM64_SYS_REG(3, 0, 0, 3, 6),	/* ID_MMFR5_EL1 */
	ARM64_SYS_REG(3, 0, 0, 3, 7),
	ARM64_SYS_REG(3, 0, 0, 4, 0),	/* ID_AA64PFR0_EL1 */
	ARM64_SYS_REG(3, 0, 0, 4, 1),	/* ID_AA64PFR1_EL1 */
	ARM64_SYS_REG(3, 0, 0, 4, 2),	/* ID_AA64PFR2_EL1 */
	ARM64_SYS_REG(3, 0, 0, 4, 3),
	ARM64_SYS_REG(3, 0, 0, 4, 4),	/* ID_AA64ZFR0_EL1 */
	ARM64_SYS_REG(3, 0, 0, 4, 5),	/* ID_AA64SMFR0_EL1 */
	ARM64_SYS_REG(3, 0, 0, 4, 6),
	ARM64_SYS_REG(3, 0, 0, 4, 7),
	ARM64_SYS_REG(3, 0, 0, 5, 0),	/* ID_AA64DFR0_EL1 */
	ARM64_SYS_REG(3, 0, 0, 5, 1),	/* ID_AA64DFR1_EL1 */
	ARM64_SYS_REG(3, 0, 0, 5, 2),
	ARM64_SYS_REG(3, 0, 0, 5, 3),
	ARM64_SYS_REG(3, 0, 0, 5, 4),	/* ID_AA64AFR0_EL1 */
	ARM64_SYS_REG(3, 0, 0, 5, 5),	/* ID_AA64AFR1_EL1 */
	ARM64_SYS_REG(3, 0, 0, 5, 6),
	ARM64_SYS_REG(3, 0, 0, 5, 7),
	ARM64_SYS_REG(3, 0, 0, 6, 0),	/* ID_AA64ISAR0_EL1 */
	ARM64_SYS_REG(3, 0, 0, 6, 1),	/* ID_AA64ISAR1_EL1 */
	ARM64_SYS_REG(3, 0, 0, 6, 2),	/* ID_AA64ISAR2_EL1 */
	ARM64_SYS_REG(3, 0, 0, 6, 3),
	ARM64_SYS_REG(3, 0, 0, 6, 4),
	ARM64_SYS_REG(3, 0, 0, 6, 5),
	ARM64_SYS_REG(3, 0, 0, 6, 6),
	ARM64_SYS_REG(3, 0, 0, 6, 7),
	ARM64_SYS_REG(3, 0, 0, 7, 0),	/* ID_AA64MMFR0_EL1 */
	ARM64_SYS_REG(3, 0, 0, 7, 1),	/* ID_AA64MMFR1_EL1 */
	ARM64_SYS_REG(3, 0, 0, 7, 2),	/* ID_AA64MMFR2_EL1 */
	ARM64_SYS_REG(3, 0, 0, 7, 3),	/* ID_AA64MMFR3_EL1 */
	ARM64_SYS_REG(3, 0, 0, 7, 4),	/* ID_AA64MMFR4_EL1 */
	ARM64_SYS_REG(3, 0, 0, 7, 5),
	ARM64_SYS_REG(3, 0, 0, 7, 6),
	ARM64_SYS_REG(3, 0, 0, 7, 7),
	ARM64_SYS_REG(3, 0, 1, 0, 0),	/* SCTLR_EL1 */
	ARM64_SYS_REG(3, 0, 1, 0, 1),	/* ACTLR_EL1 */
	ARM64_SYS_REG(3, 0, 1, 0, 2),	/* CPACR_EL1 */
	ARM64_SYS_REG(3, 0, 2, 0, 0),	/* TTBR0_EL1 */
	ARM64_SYS_REG(3, 0, 2, 0, 1),	/* TTBR1_EL1 */
	ARM64_SYS_REG(3, 0, 2, 0, 2),	/* TCR_EL1 */
	ARM64_SYS_REG(3, 0, 2, 0, 3),	/* TCR2_EL1 */
	ARM64_SYS_REG(3, 0, 5, 1, 0),	/* AFSR0_EL1 */
	ARM64_SYS_REG(3, 0, 5, 1, 1),	/* AFSR1_EL1 */
	ARM64_SYS_REG(3, 0, 5, 2, 0),	/* ESR_EL1 */
	ARM64_SYS_REG(3, 0, 6, 0, 0),	/* FAR_EL1 */
	ARM64_SYS_REG(3, 0, 7, 4, 0),	/* PAR_EL1 */
	ARM64_SYS_REG(3, 0, 10, 2, 0),	/* MAIR_EL1 */
	ARM64_SYS_REG(3, 0, 10, 2, 2),	/* PIRE0_EL1 */
	ARM64_SYS_REG(3, 0, 10, 2, 3),	/* PIR_EL1 */
	ARM64_SYS_REG(3, 0, 10, 2, 4),	/* POR_EL1 */
	ARM64_SYS_REG(3, 0, 10, 3, 0),	/* AMAIR_EL1 */
	ARM64_SYS_REG(3, 0, 12, 0, 0),	/* VBAR_EL1 */
	ARM64_SYS_REG(3, 0, 12, 1, 1),	/* DISR_EL1 */
	ARM64_SYS_REG(3, 0, 13, 0, 1),	/* CONTEXTIDR_EL1 */
	ARM64_SYS_REG(3, 0, 13, 0, 4),	/* TPIDR_EL1 */
	ARM64_SYS_REG(3, 0, 14, 1, 0),	/* CNTKCTL_EL1 */
	ARM64_SYS_REG(3, 2, 0, 0, 0),	/* CSSELR_EL1 */
	ARM64_SYS_REG(3, 3, 10, 2, 4),	/* POR_EL0 */
	ARM64_SYS_REG(3, 3, 13, 0, 2),	/* TPIDR_EL0 */
	ARM64_SYS_REG(3, 3, 13, 0, 3),	/* TPIDRRO_EL0 */
	ARM64_SYS_REG(3, 3, 14, 0, 1),	/* CNTPCT_EL0 */
	ARM64_SYS_REG(3, 3, 14, 2, 1),	/* CNTP_CTL_EL0 */
	ARM64_SYS_REG(3, 3, 14, 2, 2),	/* CNTP_CVAL_EL0 */
	ARM64_SYS_REG(3, 4, 3, 0, 0),	/* DACR32_EL2 */
	ARM64_SYS_REG(3, 4, 5, 0, 1),	/* IFSR32_EL2 */
	ARM64_SYS_REG(3, 4, 5, 3, 0),	/* FPEXC32_EL2 */
};

static __u64 pmu_regs[] = {
	ARM64_SYS_REG(3, 0, 9, 14, 1),	/* PMINTENSET_EL1 */
	ARM64_SYS_REG(3, 0, 9, 14, 2),	/* PMINTENCLR_EL1 */
	ARM64_SYS_REG(3, 3, 9, 12, 0),	/* PMCR_EL0 */
	ARM64_SYS_REG(3, 3, 9, 12, 1),	/* PMCNTENSET_EL0 */
	ARM64_SYS_REG(3, 3, 9, 12, 2),	/* PMCNTENCLR_EL0 */
	ARM64_SYS_REG(3, 3, 9, 12, 3),	/* PMOVSCLR_EL0 */
	ARM64_SYS_REG(3, 3, 9, 12, 4),	/* PMSWINC_EL0 */
	ARM64_SYS_REG(3, 3, 9, 12, 5),	/* PMSELR_EL0 */
	ARM64_SYS_REG(3, 3, 9, 13, 0),	/* PMCCNTR_EL0 */
	ARM64_SYS_REG(3, 3, 9, 14, 0),	/* PMUSERENR_EL0 */
	ARM64_SYS_REG(3, 3, 9, 14, 3),	/* PMOVSSET_EL0 */
	ARM64_SYS_REG(3, 3, 14, 8, 0),
	ARM64_SYS_REG(3, 3, 14, 8, 1),
	ARM64_SYS_REG(3, 3, 14, 8, 2),
	ARM64_SYS_REG(3, 3, 14, 8, 3),
	ARM64_SYS_REG(3, 3, 14, 8, 4),
	ARM64_SYS_REG(3, 3, 14, 8, 5),
	ARM64_SYS_REG(3, 3, 14, 8, 6),
	ARM64_SYS_REG(3, 3, 14, 8, 7),
	ARM64_SYS_REG(3, 3, 14, 9, 0),
	ARM64_SYS_REG(3, 3, 14, 9, 1),
	ARM64_SYS_REG(3, 3, 14, 9, 2),
	ARM64_SYS_REG(3, 3, 14, 9, 3),
	ARM64_SYS_REG(3, 3, 14, 9, 4),
	ARM64_SYS_REG(3, 3, 14, 9, 5),
	ARM64_SYS_REG(3, 3, 14, 9, 6),
	ARM64_SYS_REG(3, 3, 14, 9, 7),
	ARM64_SYS_REG(3, 3, 14, 10, 0),
	ARM64_SYS_REG(3, 3, 14, 10, 1),
	ARM64_SYS_REG(3, 3, 14, 10, 2),
	ARM64_SYS_REG(3, 3, 14, 10, 3),
	ARM64_SYS_REG(3, 3, 14, 10, 4),
	ARM64_SYS_REG(3, 3, 14, 10, 5),
	ARM64_SYS_REG(3, 3, 14, 10, 6),
	ARM64_SYS_REG(3, 3, 14, 10, 7),
	ARM64_SYS_REG(3, 3, 14, 11, 0),
	ARM64_SYS_REG(3, 3, 14, 11, 1),
	ARM64_SYS_REG(3, 3, 14, 11, 2),
	ARM64_SYS_REG(3, 3, 14, 11, 3),
	ARM64_SYS_REG(3, 3, 14, 11, 4),
	ARM64_SYS_REG(3, 3, 14, 11, 5),
	ARM64_SYS_REG(3, 3, 14, 11, 6),
	ARM64_SYS_REG(3, 3, 14, 12, 0),
	ARM64_SYS_REG(3, 3, 14, 12, 1),
	ARM64_SYS_REG(3, 3, 14, 12, 2),
	ARM64_SYS_REG(3, 3, 14, 12, 3),
	ARM64_SYS_REG(3, 3, 14, 12, 4),
	ARM64_SYS_REG(3, 3, 14, 12, 5),
	ARM64_SYS_REG(3, 3, 14, 12, 6),
	ARM64_SYS_REG(3, 3, 14, 12, 7),
	ARM64_SYS_REG(3, 3, 14, 13, 0),
	ARM64_SYS_REG(3, 3, 14, 13, 1),
	ARM64_SYS_REG(3, 3, 14, 13, 2),
	ARM64_SYS_REG(3, 3, 14, 13, 3),
	ARM64_SYS_REG(3, 3, 14, 13, 4),
	ARM64_SYS_REG(3, 3, 14, 13, 5),
	ARM64_SYS_REG(3, 3, 14, 13, 6),
	ARM64_SYS_REG(3, 3, 14, 13, 7),
	ARM64_SYS_REG(3, 3, 14, 14, 0),
	ARM64_SYS_REG(3, 3, 14, 14, 1),
	ARM64_SYS_REG(3, 3, 14, 14, 2),
	ARM64_SYS_REG(3, 3, 14, 14, 3),
	ARM64_SYS_REG(3, 3, 14, 14, 4),
	ARM64_SYS_REG(3, 3, 14, 14, 5),
	ARM64_SYS_REG(3, 3, 14, 14, 6),
	ARM64_SYS_REG(3, 3, 14, 14, 7),
	ARM64_SYS_REG(3, 3, 14, 15, 0),
	ARM64_SYS_REG(3, 3, 14, 15, 1),
	ARM64_SYS_REG(3, 3, 14, 15, 2),
	ARM64_SYS_REG(3, 3, 14, 15, 3),
	ARM64_SYS_REG(3, 3, 14, 15, 4),
	ARM64_SYS_REG(3, 3, 14, 15, 5),
	ARM64_SYS_REG(3, 3, 14, 15, 6),
	ARM64_SYS_REG(3, 3, 14, 15, 7),	/* PMCCFILTR_EL0 */
};

static __u64 vregs[] = {
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[0]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[1]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[2]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[3]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[4]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[5]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[6]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[7]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[8]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[9]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[10]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[11]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[12]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[13]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[14]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[15]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[16]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[17]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[18]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[19]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[20]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[21]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[22]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[23]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[24]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[25]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[26]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[27]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[28]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[29]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[30]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[31]),
};

static __u64 sve_regs[] = {
	KVM_REG_ARM64_SVE_VLS,
	KVM_REG_ARM64_SVE_ZREG(0, 0),
	KVM_REG_ARM64_SVE_ZREG(1, 0),
	KVM_REG_ARM64_SVE_ZREG(2, 0),
	KVM_REG_ARM64_SVE_ZREG(3, 0),
	KVM_REG_ARM64_SVE_ZREG(4, 0),
	KVM_REG_ARM64_SVE_ZREG(5, 0),
	KVM_REG_ARM64_SVE_ZREG(6, 0),
	KVM_REG_ARM64_SVE_ZREG(7, 0),
	KVM_REG_ARM64_SVE_ZREG(8, 0),
	KVM_REG_ARM64_SVE_ZREG(9, 0),
	KVM_REG_ARM64_SVE_ZREG(10, 0),
	KVM_REG_ARM64_SVE_ZREG(11, 0),
	KVM_REG_ARM64_SVE_ZREG(12, 0),
	KVM_REG_ARM64_SVE_ZREG(13, 0),
	KVM_REG_ARM64_SVE_ZREG(14, 0),
	KVM_REG_ARM64_SVE_ZREG(15, 0),
	KVM_REG_ARM64_SVE_ZREG(16, 0),
	KVM_REG_ARM64_SVE_ZREG(17, 0),
	KVM_REG_ARM64_SVE_ZREG(18, 0),
	KVM_REG_ARM64_SVE_ZREG(19, 0),
	KVM_REG_ARM64_SVE_ZREG(20, 0),
	KVM_REG_ARM64_SVE_ZREG(21, 0),
	KVM_REG_ARM64_SVE_ZREG(22, 0),
	KVM_REG_ARM64_SVE_ZREG(23, 0),
	KVM_REG_ARM64_SVE_ZREG(24, 0),
	KVM_REG_ARM64_SVE_ZREG(25, 0),
	KVM_REG_ARM64_SVE_ZREG(26, 0),
	KVM_REG_ARM64_SVE_ZREG(27, 0),
	KVM_REG_ARM64_SVE_ZREG(28, 0),
	KVM_REG_ARM64_SVE_ZREG(29, 0),
	KVM_REG_ARM64_SVE_ZREG(30, 0),
	KVM_REG_ARM64_SVE_ZREG(31, 0),
	KVM_REG_ARM64_SVE_PREG(0, 0),
	KVM_REG_ARM64_SVE_PREG(1, 0),
	KVM_REG_ARM64_SVE_PREG(2, 0),
	KVM_REG_ARM64_SVE_PREG(3, 0),
	KVM_REG_ARM64_SVE_PREG(4, 0),
	KVM_REG_ARM64_SVE_PREG(5, 0),
	KVM_REG_ARM64_SVE_PREG(6, 0),
	KVM_REG_ARM64_SVE_PREG(7, 0),
	KVM_REG_ARM64_SVE_PREG(8, 0),
	KVM_REG_ARM64_SVE_PREG(9, 0),
	KVM_REG_ARM64_SVE_PREG(10, 0),
	KVM_REG_ARM64_SVE_PREG(11, 0),
	KVM_REG_ARM64_SVE_PREG(12, 0),
	KVM_REG_ARM64_SVE_PREG(13, 0),
	KVM_REG_ARM64_SVE_PREG(14, 0),
	KVM_REG_ARM64_SVE_PREG(15, 0),
	KVM_REG_ARM64_SVE_FFR(0),
	ARM64_SYS_REG(3, 0, 1, 2, 0),   /* ZCR_EL1 */
};

static __u64 sve_rejects_set[] = {
	KVM_REG_ARM64_SVE_VLS,
};

static __u64 pauth_addr_regs[] = {
	ARM64_SYS_REG(3, 0, 2, 1, 0),	/* APIAKEYLO_EL1 */
	ARM64_SYS_REG(3, 0, 2, 1, 1),	/* APIAKEYHI_EL1 */
	ARM64_SYS_REG(3, 0, 2, 1, 2),	/* APIBKEYLO_EL1 */
	ARM64_SYS_REG(3, 0, 2, 1, 3),	/* APIBKEYHI_EL1 */
	ARM64_SYS_REG(3, 0, 2, 2, 0),	/* APDAKEYLO_EL1 */
	ARM64_SYS_REG(3, 0, 2, 2, 1),	/* APDAKEYHI_EL1 */
	ARM64_SYS_REG(3, 0, 2, 2, 2),	/* APDBKEYLO_EL1 */
	ARM64_SYS_REG(3, 0, 2, 2, 3)	/* APDBKEYHI_EL1 */
};

static __u64 pauth_generic_regs[] = {
	ARM64_SYS_REG(3, 0, 2, 3, 0),	/* APGAKEYLO_EL1 */
	ARM64_SYS_REG(3, 0, 2, 3, 1),	/* APGAKEYHI_EL1 */
};

#define BASE_SUBLIST \
	{ "base", .regs = base_regs, .regs_n = ARRAY_SIZE(base_regs), }
#define VREGS_SUBLIST \
	{ "vregs", .regs = vregs, .regs_n = ARRAY_SIZE(vregs), }
#define PMU_SUBLIST \
	{ "pmu", .capability = KVM_CAP_ARM_PMU_V3, .feature = KVM_ARM_VCPU_PMU_V3, \
	  .regs = pmu_regs, .regs_n = ARRAY_SIZE(pmu_regs), }
#define SVE_SUBLIST \
	{ "sve", .capability = KVM_CAP_ARM_SVE, .feature = KVM_ARM_VCPU_SVE, .finalize = true, \
	  .regs = sve_regs, .regs_n = ARRAY_SIZE(sve_regs), \
	  .rejects_set = sve_rejects_set, .rejects_set_n = ARRAY_SIZE(sve_rejects_set), }
#define PAUTH_SUBLIST							\
	{								\
		.name 		= "pauth_address",			\
		.capability	= KVM_CAP_ARM_PTRAUTH_ADDRESS,		\
		.feature	= KVM_ARM_VCPU_PTRAUTH_ADDRESS,		\
		.regs		= pauth_addr_regs,			\
		.regs_n		= ARRAY_SIZE(pauth_addr_regs),		\
	},								\
	{								\
		.name 		= "pauth_generic",			\
		.capability	= KVM_CAP_ARM_PTRAUTH_GENERIC,		\
		.feature	= KVM_ARM_VCPU_PTRAUTH_GENERIC,		\
		.regs		= pauth_generic_regs,			\
		.regs_n		= ARRAY_SIZE(pauth_generic_regs),	\
	}

static struct vcpu_reg_list vregs_config = {
	.sublists = {
	BASE_SUBLIST,
	VREGS_SUBLIST,
	{0},
	},
};
static struct vcpu_reg_list vregs_pmu_config = {
	.sublists = {
	BASE_SUBLIST,
	VREGS_SUBLIST,
	PMU_SUBLIST,
	{0},
	},
};
static struct vcpu_reg_list sve_config = {
	.sublists = {
	BASE_SUBLIST,
	SVE_SUBLIST,
	{0},
	},
};
static struct vcpu_reg_list sve_pmu_config = {
	.sublists = {
	BASE_SUBLIST,
	SVE_SUBLIST,
	PMU_SUBLIST,
	{0},
	},
};
static struct vcpu_reg_list pauth_config = {
	.sublists = {
	BASE_SUBLIST,
	VREGS_SUBLIST,
	PAUTH_SUBLIST,
	{0},
	},
};
static struct vcpu_reg_list pauth_pmu_config = {
	.sublists = {
	BASE_SUBLIST,
	VREGS_SUBLIST,
	PAUTH_SUBLIST,
	PMU_SUBLIST,
	{0},
	},
};

struct vcpu_reg_list *vcpu_configs[] = {
	&vregs_config,
	&vregs_pmu_config,
	&sve_config,
	&sve_pmu_config,
	&pauth_config,
	&pauth_pmu_config,
};
int vcpu_configs_n = ARRAY_SIZE(vcpu_configs);
