// SPDX-License-Identifier: GPL-2.0-only
/*
 * hidden_features - Check that a feature's instruction runs in the guest when
 * its ID register field is advertised, and is UNDEFINED when userspace clears
 * the field.
 *
 * Copyright (c) 2026 Google LLC
 * Author: Fuad Tabba <fuad.tabba@linux.dev>
 */
#include "kvm_util.h"
#include "processor.h"
#include "test_util.h"

static volatile bool undef;

static void guest_tlbi_os(void)
{
	/* tlbi vmalle1os */
	asm volatile("sys #0, c8, c1, #0\n\tdsb ish\n\tisb" ::: "memory");
}

static void guest_mops(void)
{
	register u64 *d asm("x0");
	register u64 n asm("x1");
	register u64 s asm("x2");
	u64 buf[8];

	d = buf;
	n = sizeof(buf);
	s = 0;
	/* setp [x0]!, x1!, x2; setm; sete */
	asm volatile(".inst 0x19c20420\n\t.inst 0x19c24420\n\t.inst 0x19c28420"
		     : "+r"(d), "+r"(n) : "r"(s) : "cc", "memory");
}

static void guest_tcr2(void)
{
	read_sysreg_s(SYS_TCR2_EL1);
}

static void guest_fpmr(void)
{
	read_sysreg_s(SYS_FPMR);
}

struct feature {
	const char *name;
	u64 id_reg;
	u64 mask;
	u8 shift;
	u64 min;
	void (*insn)(void);
	bool (*trappable)(struct kvm_vcpu *vcpu);
};

/* Without FGT, KVM traps a hidden TLBI OS only through HCR_EL2.TTLBOS (FEAT_EVT2). */
static bool tlbi_os_trappable(struct kvm_vcpu *vcpu)
{
	u64 mmfr0 = vcpu_get_reg(vcpu, KVM_ARM64_SYS_REG(SYS_ID_AA64MMFR0_EL1));
	u64 mmfr2 = vcpu_get_reg(vcpu, KVM_ARM64_SYS_REG(SYS_ID_AA64MMFR2_EL1));

	return SYS_FIELD_GET(ID_AA64MMFR0_EL1, FGT, mmfr0) >= ID_AA64MMFR0_EL1_FGT_IMP ||
	       SYS_FIELD_GET(ID_AA64MMFR2_EL1, EVT, mmfr2) >= ID_AA64MMFR2_EL1_EVT_TTLBxS;
}

#define FEATURE(n, reg, field, min_val, fn, trap)		\
{								\
	.name		= n,					\
	.id_reg		= SYS_##reg,				\
	.mask		= reg##_##field##_MASK,			\
	.shift		= reg##_##field##_SHIFT,		\
	.min		= reg##_##field##_##min_val,		\
	.insn		= fn,					\
	.trappable	= trap,					\
}

static const struct feature features[] = {
	FEATURE("TLBI OS", ID_AA64ISAR0_EL1, TLB, OS, guest_tlbi_os, tlbi_os_trappable),
	FEATURE("MOPS", ID_AA64ISAR2_EL1, MOPS, IMP, guest_mops, NULL),
	FEATURE("TCR2_EL1", ID_AA64MMFR3_EL1, TCRX, IMP, guest_tcr2, NULL),
	FEATURE("FPMR", ID_AA64PFR2_EL1, FPMR, IMP, guest_fpmr, NULL),
};

static void guest_code(const struct feature *feat)
{
	undef = false;
	feat->insn();
	GUEST_SYNC(undef);
	GUEST_DONE();
}

static void guest_undef_handler(struct ex_regs *regs)
{
	undef = true;
	regs->pc += 4;
}

static bool run(const struct feature *feat, bool hide)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct ucall uc;
	bool got = false;
	u64 val;

	vm = vm_create_with_one_vcpu(&vcpu, (void *)guest_code);
	vm_init_descriptor_tables(vm);
	vcpu_init_descriptor_tables(vcpu);
	vm_install_sync_handler(vm, VECTOR_SYNC_CURRENT, ESR_ELx_EC_UNKNOWN, guest_undef_handler);
	vcpu_args_set(vcpu, 1, feat);

	if (hide) {
		val = vcpu_get_reg(vcpu, KVM_ARM64_SYS_REG(feat->id_reg));
		vcpu_set_reg(vcpu, KVM_ARM64_SYS_REG(feat->id_reg), val & ~feat->mask);
	}

	for (;;) {
		vcpu_run(vcpu);
		switch (get_ucall(vcpu, &uc)) {
		case UCALL_SYNC:
			got = uc.args[1];
			break;
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
			break;
		case UCALL_DONE:
			kvm_vm_free(vm);
			return got;
		default:
			TEST_FAIL("Unknown ucall %lu", uc.cmd);
		}
	}
}

static void probe_feature(const struct feature *feat, bool *present, bool *trappable)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	u64 val;

	vm = vm_create_with_one_vcpu(&vcpu, NULL);
	val = vcpu_get_reg(vcpu, KVM_ARM64_SYS_REG(feat->id_reg));
	*present = ((val & feat->mask) >> feat->shift) >= feat->min;
	*trappable = !feat->trappable || feat->trappable(vcpu);
	kvm_vm_free(vm);
}

int main(void)
{
	const struct feature *feat;
	bool present, trappable;
	int i;

	test_disable_default_vgic();

	ksft_print_header();
	ksft_set_plan(ARRAY_SIZE(features) * 2);

	for (i = 0; i < ARRAY_SIZE(features); i++) {
		feat = &features[i];

		probe_feature(feat, &present, &trappable);
		if (!present) {
			ksft_test_result_skip("%s advertised, not supported\n", feat->name);
			ksft_test_result_skip("%s hidden, not supported\n", feat->name);
			continue;
		}

		if (run(feat, false))
			ksft_test_result_fail("%s advertised, UNDEF\n", feat->name);
		else
			ksft_test_result_pass("%s advertised\n", feat->name);

		if (!trappable)
			ksft_test_result_skip("%s hidden, not trappable\n", feat->name);
		else if (run(feat, true))
			ksft_test_result_pass("%s hidden\n", feat->name);
		else
			ksft_test_result_fail("%s hidden, no UNDEF\n", feat->name);
	}

	ksft_finished();
}
