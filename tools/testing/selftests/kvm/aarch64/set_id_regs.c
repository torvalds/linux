// SPDX-License-Identifier: GPL-2.0-only
/*
 * set_id_regs - Test for setting ID register from usersapce.
 *
 * Copyright (c) 2023 Google LLC.
 *
 *
 * Test that KVM supports setting ID registers from userspace and handles the
 * feature set correctly.
 */

#include <stdint.h>
#include "kvm_util.h"
#include "processor.h"
#include "test_util.h"
#include <linux/bitfield.h>

enum ftr_type {
	FTR_EXACT,			/* Use a predefined safe value */
	FTR_LOWER_SAFE,			/* Smaller value is safe */
	FTR_HIGHER_SAFE,		/* Bigger value is safe */
	FTR_HIGHER_OR_ZERO_SAFE,	/* Bigger value is safe, but 0 is biggest */
	FTR_END,			/* Mark the last ftr bits */
};

#define FTR_SIGNED	true	/* Value should be treated as signed */
#define FTR_UNSIGNED	false	/* Value should be treated as unsigned */

struct reg_ftr_bits {
	char *name;
	bool sign;
	enum ftr_type type;
	uint8_t shift;
	uint64_t mask;
	/*
	 * For FTR_EXACT, safe_val is used as the exact safe value.
	 * For FTR_LOWER_SAFE, safe_val is used as the minimal safe value.
	 */
	int64_t safe_val;
};

struct test_feature_reg {
	uint32_t reg;
	const struct reg_ftr_bits *ftr_bits;
};

#define __REG_FTR_BITS(NAME, SIGNED, TYPE, SHIFT, MASK, SAFE_VAL)	\
	{								\
		.name = #NAME,						\
		.sign = SIGNED,						\
		.type = TYPE,						\
		.shift = SHIFT,						\
		.mask = MASK,						\
		.safe_val = SAFE_VAL,					\
	}

#define REG_FTR_BITS(type, reg, field, safe_val) \
	__REG_FTR_BITS(reg##_##field, FTR_UNSIGNED, type, reg##_##field##_SHIFT, \
		       reg##_##field##_MASK, safe_val)

#define S_REG_FTR_BITS(type, reg, field, safe_val) \
	__REG_FTR_BITS(reg##_##field, FTR_SIGNED, type, reg##_##field##_SHIFT, \
		       reg##_##field##_MASK, safe_val)

#define REG_FTR_END					\
	{						\
		.type = FTR_END,			\
	}

static const struct reg_ftr_bits ftr_id_aa64dfr0_el1[] = {
	S_REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64DFR0_EL1, DoubleLock, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64DFR0_EL1, WRPs, 0),
	S_REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64DFR0_EL1, PMUVer, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64DFR0_EL1, DebugVer, ID_AA64DFR0_EL1_DebugVer_IMP),
	REG_FTR_END,
};

static const struct reg_ftr_bits ftr_id_dfr0_el1[] = {
	S_REG_FTR_BITS(FTR_LOWER_SAFE, ID_DFR0_EL1, PerfMon, ID_DFR0_EL1_PerfMon_PMUv3),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_DFR0_EL1, CopDbg, ID_DFR0_EL1_CopDbg_Armv8),
	REG_FTR_END,
};

static const struct reg_ftr_bits ftr_id_aa64isar0_el1[] = {
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ISAR0_EL1, RNDR, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ISAR0_EL1, TLB, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ISAR0_EL1, TS, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ISAR0_EL1, FHM, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ISAR0_EL1, DP, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ISAR0_EL1, SM4, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ISAR0_EL1, SM3, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ISAR0_EL1, SHA3, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ISAR0_EL1, RDM, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ISAR0_EL1, TME, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ISAR0_EL1, ATOMIC, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ISAR0_EL1, CRC32, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ISAR0_EL1, SHA2, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ISAR0_EL1, SHA1, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ISAR0_EL1, AES, 0),
	REG_FTR_END,
};

static const struct reg_ftr_bits ftr_id_aa64isar1_el1[] = {
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ISAR1_EL1, LS64, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ISAR1_EL1, XS, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ISAR1_EL1, I8MM, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ISAR1_EL1, DGH, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ISAR1_EL1, BF16, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ISAR1_EL1, SPECRES, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ISAR1_EL1, SB, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ISAR1_EL1, FRINTTS, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ISAR1_EL1, LRCPC, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ISAR1_EL1, FCMA, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ISAR1_EL1, JSCVT, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ISAR1_EL1, DPB, 0),
	REG_FTR_END,
};

static const struct reg_ftr_bits ftr_id_aa64isar2_el1[] = {
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ISAR2_EL1, BC, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ISAR2_EL1, RPRES, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ISAR2_EL1, WFxT, 0),
	REG_FTR_END,
};

static const struct reg_ftr_bits ftr_id_aa64pfr0_el1[] = {
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64PFR0_EL1, CSV3, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64PFR0_EL1, CSV2, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64PFR0_EL1, DIT, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64PFR0_EL1, SEL2, 0),
	REG_FTR_BITS(FTR_EXACT, ID_AA64PFR0_EL1, GIC, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64PFR0_EL1, EL3, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64PFR0_EL1, EL2, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64PFR0_EL1, EL1, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64PFR0_EL1, EL0, 0),
	REG_FTR_END,
};

static const struct reg_ftr_bits ftr_id_aa64pfr1_el1[] = {
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64PFR1_EL1, CSV2_frac, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64PFR1_EL1, SSBS, ID_AA64PFR1_EL1_SSBS_NI),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64PFR1_EL1, BT, 0),
	REG_FTR_END,
};

static const struct reg_ftr_bits ftr_id_aa64mmfr0_el1[] = {
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64MMFR0_EL1, ECV, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64MMFR0_EL1, EXS, 0),
	S_REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64MMFR0_EL1, TGRAN4, 0),
	S_REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64MMFR0_EL1, TGRAN64, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64MMFR0_EL1, TGRAN16, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64MMFR0_EL1, BIGENDEL0, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64MMFR0_EL1, SNSMEM, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64MMFR0_EL1, BIGEND, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64MMFR0_EL1, ASIDBITS, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64MMFR0_EL1, PARANGE, 0),
	REG_FTR_END,
};

static const struct reg_ftr_bits ftr_id_aa64mmfr1_el1[] = {
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64MMFR1_EL1, TIDCP1, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64MMFR1_EL1, AFP, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64MMFR1_EL1, ETS, 0),
	REG_FTR_BITS(FTR_HIGHER_SAFE, ID_AA64MMFR1_EL1, SpecSEI, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64MMFR1_EL1, PAN, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64MMFR1_EL1, LO, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64MMFR1_EL1, HPDS, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64MMFR1_EL1, HAFDBS, 0),
	REG_FTR_END,
};

static const struct reg_ftr_bits ftr_id_aa64mmfr2_el1[] = {
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64MMFR2_EL1, E0PD, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64MMFR2_EL1, BBM, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64MMFR2_EL1, TTL, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64MMFR2_EL1, AT, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64MMFR2_EL1, ST, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64MMFR2_EL1, VARange, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64MMFR2_EL1, IESB, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64MMFR2_EL1, LSM, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64MMFR2_EL1, UAO, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64MMFR2_EL1, CnP, 0),
	REG_FTR_END,
};

static const struct reg_ftr_bits ftr_id_aa64zfr0_el1[] = {
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ZFR0_EL1, F64MM, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ZFR0_EL1, F32MM, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ZFR0_EL1, I8MM, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ZFR0_EL1, SM4, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ZFR0_EL1, SHA3, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ZFR0_EL1, BF16, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ZFR0_EL1, BitPerm, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ZFR0_EL1, AES, 0),
	REG_FTR_BITS(FTR_LOWER_SAFE, ID_AA64ZFR0_EL1, SVEver, 0),
	REG_FTR_END,
};

#define TEST_REG(id, table)			\
	{					\
		.reg = id,			\
		.ftr_bits = &((table)[0]),	\
	}

static struct test_feature_reg test_regs[] = {
	TEST_REG(SYS_ID_AA64DFR0_EL1, ftr_id_aa64dfr0_el1),
	TEST_REG(SYS_ID_DFR0_EL1, ftr_id_dfr0_el1),
	TEST_REG(SYS_ID_AA64ISAR0_EL1, ftr_id_aa64isar0_el1),
	TEST_REG(SYS_ID_AA64ISAR1_EL1, ftr_id_aa64isar1_el1),
	TEST_REG(SYS_ID_AA64ISAR2_EL1, ftr_id_aa64isar2_el1),
	TEST_REG(SYS_ID_AA64PFR0_EL1, ftr_id_aa64pfr0_el1),
	TEST_REG(SYS_ID_AA64PFR1_EL1, ftr_id_aa64pfr1_el1),
	TEST_REG(SYS_ID_AA64MMFR0_EL1, ftr_id_aa64mmfr0_el1),
	TEST_REG(SYS_ID_AA64MMFR1_EL1, ftr_id_aa64mmfr1_el1),
	TEST_REG(SYS_ID_AA64MMFR2_EL1, ftr_id_aa64mmfr2_el1),
	TEST_REG(SYS_ID_AA64ZFR0_EL1, ftr_id_aa64zfr0_el1),
};

#define GUEST_REG_SYNC(id) GUEST_SYNC_ARGS(0, id, read_sysreg_s(id), 0, 0);

static void guest_code(void)
{
	GUEST_REG_SYNC(SYS_ID_AA64DFR0_EL1);
	GUEST_REG_SYNC(SYS_ID_DFR0_EL1);
	GUEST_REG_SYNC(SYS_ID_AA64ISAR0_EL1);
	GUEST_REG_SYNC(SYS_ID_AA64ISAR1_EL1);
	GUEST_REG_SYNC(SYS_ID_AA64ISAR2_EL1);
	GUEST_REG_SYNC(SYS_ID_AA64PFR0_EL1);
	GUEST_REG_SYNC(SYS_ID_AA64MMFR0_EL1);
	GUEST_REG_SYNC(SYS_ID_AA64MMFR1_EL1);
	GUEST_REG_SYNC(SYS_ID_AA64MMFR2_EL1);
	GUEST_REG_SYNC(SYS_ID_AA64ZFR0_EL1);
	GUEST_REG_SYNC(SYS_CTR_EL0);

	GUEST_DONE();
}

/* Return a safe value to a given ftr_bits an ftr value */
uint64_t get_safe_value(const struct reg_ftr_bits *ftr_bits, uint64_t ftr)
{
	uint64_t ftr_max = GENMASK_ULL(ARM64_FEATURE_FIELD_BITS - 1, 0);

	if (ftr_bits->sign == FTR_UNSIGNED) {
		switch (ftr_bits->type) {
		case FTR_EXACT:
			ftr = ftr_bits->safe_val;
			break;
		case FTR_LOWER_SAFE:
			if (ftr > ftr_bits->safe_val)
				ftr--;
			break;
		case FTR_HIGHER_SAFE:
			if (ftr < ftr_max)
				ftr++;
			break;
		case FTR_HIGHER_OR_ZERO_SAFE:
			if (ftr == ftr_max)
				ftr = 0;
			else if (ftr != 0)
				ftr++;
			break;
		default:
			break;
		}
	} else if (ftr != ftr_max) {
		switch (ftr_bits->type) {
		case FTR_EXACT:
			ftr = ftr_bits->safe_val;
			break;
		case FTR_LOWER_SAFE:
			if (ftr > ftr_bits->safe_val)
				ftr--;
			break;
		case FTR_HIGHER_SAFE:
			if (ftr < ftr_max - 1)
				ftr++;
			break;
		case FTR_HIGHER_OR_ZERO_SAFE:
			if (ftr != 0 && ftr != ftr_max - 1)
				ftr++;
			break;
		default:
			break;
		}
	}

	return ftr;
}

/* Return an invalid value to a given ftr_bits an ftr value */
uint64_t get_invalid_value(const struct reg_ftr_bits *ftr_bits, uint64_t ftr)
{
	uint64_t ftr_max = GENMASK_ULL(ARM64_FEATURE_FIELD_BITS - 1, 0);

	if (ftr_bits->sign == FTR_UNSIGNED) {
		switch (ftr_bits->type) {
		case FTR_EXACT:
			ftr = max((uint64_t)ftr_bits->safe_val + 1, ftr + 1);
			break;
		case FTR_LOWER_SAFE:
			ftr++;
			break;
		case FTR_HIGHER_SAFE:
			ftr--;
			break;
		case FTR_HIGHER_OR_ZERO_SAFE:
			if (ftr == 0)
				ftr = ftr_max;
			else
				ftr--;
			break;
		default:
			break;
		}
	} else if (ftr != ftr_max) {
		switch (ftr_bits->type) {
		case FTR_EXACT:
			ftr = max((uint64_t)ftr_bits->safe_val + 1, ftr + 1);
			break;
		case FTR_LOWER_SAFE:
			ftr++;
			break;
		case FTR_HIGHER_SAFE:
			ftr--;
			break;
		case FTR_HIGHER_OR_ZERO_SAFE:
			if (ftr == 0)
				ftr = ftr_max - 1;
			else
				ftr--;
			break;
		default:
			break;
		}
	} else {
		ftr = 0;
	}

	return ftr;
}

static uint64_t test_reg_set_success(struct kvm_vcpu *vcpu, uint64_t reg,
				     const struct reg_ftr_bits *ftr_bits)
{
	uint8_t shift = ftr_bits->shift;
	uint64_t mask = ftr_bits->mask;
	uint64_t val, new_val, ftr;

	vcpu_get_reg(vcpu, reg, &val);
	ftr = (val & mask) >> shift;

	ftr = get_safe_value(ftr_bits, ftr);

	ftr <<= shift;
	val &= ~mask;
	val |= ftr;

	vcpu_set_reg(vcpu, reg, val);
	vcpu_get_reg(vcpu, reg, &new_val);
	TEST_ASSERT_EQ(new_val, val);

	return new_val;
}

static void test_reg_set_fail(struct kvm_vcpu *vcpu, uint64_t reg,
			      const struct reg_ftr_bits *ftr_bits)
{
	uint8_t shift = ftr_bits->shift;
	uint64_t mask = ftr_bits->mask;
	uint64_t val, old_val, ftr;
	int r;

	vcpu_get_reg(vcpu, reg, &val);
	ftr = (val & mask) >> shift;

	ftr = get_invalid_value(ftr_bits, ftr);

	old_val = val;
	ftr <<= shift;
	val &= ~mask;
	val |= ftr;

	r = __vcpu_set_reg(vcpu, reg, val);
	TEST_ASSERT(r < 0 && errno == EINVAL,
		    "Unexpected KVM_SET_ONE_REG error: r=%d, errno=%d", r, errno);

	vcpu_get_reg(vcpu, reg, &val);
	TEST_ASSERT_EQ(val, old_val);
}

static uint64_t test_reg_vals[KVM_ARM_FEATURE_ID_RANGE_SIZE];

#define encoding_to_range_idx(encoding)							\
	KVM_ARM_FEATURE_ID_RANGE_IDX(sys_reg_Op0(encoding), sys_reg_Op1(encoding),	\
				     sys_reg_CRn(encoding), sys_reg_CRm(encoding),	\
				     sys_reg_Op2(encoding))


static void test_vm_ftr_id_regs(struct kvm_vcpu *vcpu, bool aarch64_only)
{
	uint64_t masks[KVM_ARM_FEATURE_ID_RANGE_SIZE];
	struct reg_mask_range range = {
		.addr = (__u64)masks,
	};
	int ret;

	/* KVM should return error when reserved field is not zero */
	range.reserved[0] = 1;
	ret = __vm_ioctl(vcpu->vm, KVM_ARM_GET_REG_WRITABLE_MASKS, &range);
	TEST_ASSERT(ret, "KVM doesn't check invalid parameters.");

	/* Get writable masks for feature ID registers */
	memset(range.reserved, 0, sizeof(range.reserved));
	vm_ioctl(vcpu->vm, KVM_ARM_GET_REG_WRITABLE_MASKS, &range);

	for (int i = 0; i < ARRAY_SIZE(test_regs); i++) {
		const struct reg_ftr_bits *ftr_bits = test_regs[i].ftr_bits;
		uint32_t reg_id = test_regs[i].reg;
		uint64_t reg = KVM_ARM64_SYS_REG(reg_id);
		int idx;

		/* Get the index to masks array for the idreg */
		idx = encoding_to_range_idx(reg_id);

		for (int j = 0;  ftr_bits[j].type != FTR_END; j++) {
			/* Skip aarch32 reg on aarch64 only system, since they are RAZ/WI. */
			if (aarch64_only && sys_reg_CRm(reg_id) < 4) {
				ksft_test_result_skip("%s on AARCH64 only system\n",
						      ftr_bits[j].name);
				continue;
			}

			/* Make sure the feature field is writable */
			TEST_ASSERT_EQ(masks[idx] & ftr_bits[j].mask, ftr_bits[j].mask);

			test_reg_set_fail(vcpu, reg, &ftr_bits[j]);

			test_reg_vals[idx] = test_reg_set_success(vcpu, reg,
								  &ftr_bits[j]);

			ksft_test_result_pass("%s\n", ftr_bits[j].name);
		}
	}
}

#define MPAM_IDREG_TEST	6
static void test_user_set_mpam_reg(struct kvm_vcpu *vcpu)
{
	uint64_t masks[KVM_ARM_FEATURE_ID_RANGE_SIZE];
	struct reg_mask_range range = {
		.addr = (__u64)masks,
	};
	uint64_t val;
	int idx, err;

	/*
	 * If ID_AA64PFR0.MPAM is _not_ officially modifiable and is zero,
	 * check that if it can be set to 1, (i.e. it is supported by the
	 * hardware), that it can't be set to other values.
	 */

	/* Get writable masks for feature ID registers */
	memset(range.reserved, 0, sizeof(range.reserved));
	vm_ioctl(vcpu->vm, KVM_ARM_GET_REG_WRITABLE_MASKS, &range);

	/* Writeable? Nothing to test! */
	idx = encoding_to_range_idx(SYS_ID_AA64PFR0_EL1);
	if ((masks[idx] & ID_AA64PFR0_EL1_MPAM_MASK) == ID_AA64PFR0_EL1_MPAM_MASK) {
		ksft_test_result_skip("ID_AA64PFR0_EL1.MPAM is officially writable, nothing to test\n");
		return;
	}

	/* Get the id register value */
	vcpu_get_reg(vcpu, KVM_ARM64_SYS_REG(SYS_ID_AA64PFR0_EL1), &val);

	/* Try to set MPAM=0. This should always be possible. */
	val &= ~ID_AA64PFR0_EL1_MPAM_MASK;
	val |= FIELD_PREP(ID_AA64PFR0_EL1_MPAM_MASK, 0);
	err = __vcpu_set_reg(vcpu, KVM_ARM64_SYS_REG(SYS_ID_AA64PFR0_EL1), val);
	if (err)
		ksft_test_result_fail("ID_AA64PFR0_EL1.MPAM=0 was not accepted\n");
	else
		ksft_test_result_pass("ID_AA64PFR0_EL1.MPAM=0 worked\n");

	/* Try to set MPAM=1 */
	val &= ~ID_AA64PFR0_EL1_MPAM_MASK;
	val |= FIELD_PREP(ID_AA64PFR0_EL1_MPAM_MASK, 1);
	err = __vcpu_set_reg(vcpu, KVM_ARM64_SYS_REG(SYS_ID_AA64PFR0_EL1), val);
	if (err)
		ksft_test_result_skip("ID_AA64PFR0_EL1.MPAM is not writable, nothing to test\n");
	else
		ksft_test_result_pass("ID_AA64PFR0_EL1.MPAM=1 was writable\n");

	/* Try to set MPAM=2 */
	val &= ~ID_AA64PFR0_EL1_MPAM_MASK;
	val |= FIELD_PREP(ID_AA64PFR0_EL1_MPAM_MASK, 2);
	err = __vcpu_set_reg(vcpu, KVM_ARM64_SYS_REG(SYS_ID_AA64PFR0_EL1), val);
	if (err)
		ksft_test_result_pass("ID_AA64PFR0_EL1.MPAM not arbitrarily modifiable\n");
	else
		ksft_test_result_fail("ID_AA64PFR0_EL1.MPAM value should not be ignored\n");

	/* And again for ID_AA64PFR1_EL1.MPAM_frac */
	idx = encoding_to_range_idx(SYS_ID_AA64PFR1_EL1);
	if ((masks[idx] & ID_AA64PFR1_EL1_MPAM_frac_MASK) == ID_AA64PFR1_EL1_MPAM_frac_MASK) {
		ksft_test_result_skip("ID_AA64PFR1_EL1.MPAM_frac is officially writable, nothing to test\n");
		return;
	}

	/* Get the id register value */
	vcpu_get_reg(vcpu, KVM_ARM64_SYS_REG(SYS_ID_AA64PFR1_EL1), &val);

	/* Try to set MPAM_frac=0. This should always be possible. */
	val &= ~ID_AA64PFR1_EL1_MPAM_frac_MASK;
	val |= FIELD_PREP(ID_AA64PFR1_EL1_MPAM_frac_MASK, 0);
	err = __vcpu_set_reg(vcpu, KVM_ARM64_SYS_REG(SYS_ID_AA64PFR1_EL1), val);
	if (err)
		ksft_test_result_fail("ID_AA64PFR0_EL1.MPAM_frac=0 was not accepted\n");
	else
		ksft_test_result_pass("ID_AA64PFR0_EL1.MPAM_frac=0 worked\n");

	/* Try to set MPAM_frac=1 */
	val &= ~ID_AA64PFR1_EL1_MPAM_frac_MASK;
	val |= FIELD_PREP(ID_AA64PFR1_EL1_MPAM_frac_MASK, 1);
	err = __vcpu_set_reg(vcpu, KVM_ARM64_SYS_REG(SYS_ID_AA64PFR1_EL1), val);
	if (err)
		ksft_test_result_skip("ID_AA64PFR1_EL1.MPAM_frac is not writable, nothing to test\n");
	else
		ksft_test_result_pass("ID_AA64PFR0_EL1.MPAM_frac=1 was writable\n");

	/* Try to set MPAM_frac=2 */
	val &= ~ID_AA64PFR1_EL1_MPAM_frac_MASK;
	val |= FIELD_PREP(ID_AA64PFR1_EL1_MPAM_frac_MASK, 2);
	err = __vcpu_set_reg(vcpu, KVM_ARM64_SYS_REG(SYS_ID_AA64PFR1_EL1), val);
	if (err)
		ksft_test_result_pass("ID_AA64PFR1_EL1.MPAM_frac not arbitrarily modifiable\n");
	else
		ksft_test_result_fail("ID_AA64PFR1_EL1.MPAM_frac value should not be ignored\n");
}

static void test_guest_reg_read(struct kvm_vcpu *vcpu)
{
	bool done = false;
	struct ucall uc;

	while (!done) {
		vcpu_run(vcpu);

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
			break;
		case UCALL_SYNC:
			/* Make sure the written values are seen by guest */
			TEST_ASSERT_EQ(test_reg_vals[encoding_to_range_idx(uc.args[2])],
				       uc.args[3]);
			break;
		case UCALL_DONE:
			done = true;
			break;
		default:
			TEST_FAIL("Unexpected ucall: %lu", uc.cmd);
		}
	}
}

/* Politely lifted from arch/arm64/include/asm/cache.h */
/* Ctypen, bits[3(n - 1) + 2 : 3(n - 1)], for n = 1 to 7 */
#define CLIDR_CTYPE_SHIFT(level)	(3 * (level - 1))
#define CLIDR_CTYPE_MASK(level)		(7 << CLIDR_CTYPE_SHIFT(level))
#define CLIDR_CTYPE(clidr, level)	\
	(((clidr) & CLIDR_CTYPE_MASK(level)) >> CLIDR_CTYPE_SHIFT(level))

static void test_clidr(struct kvm_vcpu *vcpu)
{
	uint64_t clidr;
	int level;

	vcpu_get_reg(vcpu, KVM_ARM64_SYS_REG(SYS_CLIDR_EL1), &clidr);

	/* find the first empty level in the cache hierarchy */
	for (level = 1; level < 7; level++) {
		if (!CLIDR_CTYPE(clidr, level))
			break;
	}

	/*
	 * If you have a mind-boggling 7 levels of cache, congratulations, you
	 * get to fix this.
	 */
	TEST_ASSERT(level <= 7, "can't find an empty level in cache hierarchy");

	/* stick in a unified cache level */
	clidr |= BIT(2) << CLIDR_CTYPE_SHIFT(level);

	vcpu_set_reg(vcpu, KVM_ARM64_SYS_REG(SYS_CLIDR_EL1), clidr);
	test_reg_vals[encoding_to_range_idx(SYS_CLIDR_EL1)] = clidr;
}

static void test_ctr(struct kvm_vcpu *vcpu)
{
	u64 ctr;

	vcpu_get_reg(vcpu, KVM_ARM64_SYS_REG(SYS_CTR_EL0), &ctr);
	ctr &= ~CTR_EL0_DIC_MASK;
	if (ctr & CTR_EL0_IminLine_MASK)
		ctr--;

	vcpu_set_reg(vcpu, KVM_ARM64_SYS_REG(SYS_CTR_EL0), ctr);
	test_reg_vals[encoding_to_range_idx(SYS_CTR_EL0)] = ctr;
}

static void test_vcpu_ftr_id_regs(struct kvm_vcpu *vcpu)
{
	u64 val;

	test_clidr(vcpu);
	test_ctr(vcpu);

	vcpu_get_reg(vcpu, KVM_ARM64_SYS_REG(SYS_MPIDR_EL1), &val);
	val++;
	vcpu_set_reg(vcpu, KVM_ARM64_SYS_REG(SYS_MPIDR_EL1), val);

	test_reg_vals[encoding_to_range_idx(SYS_MPIDR_EL1)] = val;
	ksft_test_result_pass("%s\n", __func__);
}

static void test_assert_id_reg_unchanged(struct kvm_vcpu *vcpu, uint32_t encoding)
{
	size_t idx = encoding_to_range_idx(encoding);
	uint64_t observed;

	vcpu_get_reg(vcpu, KVM_ARM64_SYS_REG(encoding), &observed);
	TEST_ASSERT_EQ(test_reg_vals[idx], observed);
}

static void test_reset_preserves_id_regs(struct kvm_vcpu *vcpu)
{
	/*
	 * Calls KVM_ARM_VCPU_INIT behind the scenes, which will do an
	 * architectural reset of the vCPU.
	 */
	aarch64_vcpu_setup(vcpu, NULL);

	for (int i = 0; i < ARRAY_SIZE(test_regs); i++)
		test_assert_id_reg_unchanged(vcpu, test_regs[i].reg);

	test_assert_id_reg_unchanged(vcpu, SYS_MPIDR_EL1);
	test_assert_id_reg_unchanged(vcpu, SYS_CLIDR_EL1);
	test_assert_id_reg_unchanged(vcpu, SYS_CTR_EL0);

	ksft_test_result_pass("%s\n", __func__);
}

int main(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	bool aarch64_only;
	uint64_t val, el0;
	int test_cnt;

	TEST_REQUIRE(kvm_has_cap(KVM_CAP_ARM_SUPPORTED_REG_MASK_RANGES));

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);

	/* Check for AARCH64 only system */
	vcpu_get_reg(vcpu, KVM_ARM64_SYS_REG(SYS_ID_AA64PFR0_EL1), &val);
	el0 = FIELD_GET(ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_EL0), val);
	aarch64_only = (el0 == ID_AA64PFR0_EL1_ELx_64BIT_ONLY);

	ksft_print_header();

	test_cnt = ARRAY_SIZE(ftr_id_aa64dfr0_el1) + ARRAY_SIZE(ftr_id_dfr0_el1) +
		   ARRAY_SIZE(ftr_id_aa64isar0_el1) + ARRAY_SIZE(ftr_id_aa64isar1_el1) +
		   ARRAY_SIZE(ftr_id_aa64isar2_el1) + ARRAY_SIZE(ftr_id_aa64pfr0_el1) +
		   ARRAY_SIZE(ftr_id_aa64pfr1_el1) + ARRAY_SIZE(ftr_id_aa64mmfr0_el1) +
		   ARRAY_SIZE(ftr_id_aa64mmfr1_el1) + ARRAY_SIZE(ftr_id_aa64mmfr2_el1) +
		   ARRAY_SIZE(ftr_id_aa64zfr0_el1) - ARRAY_SIZE(test_regs) + 2 +
		   MPAM_IDREG_TEST;

	ksft_set_plan(test_cnt);

	test_vm_ftr_id_regs(vcpu, aarch64_only);
	test_vcpu_ftr_id_regs(vcpu);
	test_user_set_mpam_reg(vcpu);

	test_guest_reg_read(vcpu);

	test_reset_preserves_id_regs(vcpu);

	kvm_vm_free(vm);

	ksft_finished();
}
