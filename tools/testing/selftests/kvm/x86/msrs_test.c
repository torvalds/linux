// SPDX-License-Identifier: GPL-2.0-only
#include <asm/msr-index.h>

#include <stdint.h>

#include "kvm_util.h"
#include "processor.h"

/* Use HYPERVISOR for MSRs that are emulated unconditionally (as is HYPERVISOR). */
#define X86_FEATURE_NONE X86_FEATURE_HYPERVISOR

struct kvm_msr {
	const struct kvm_x86_cpu_feature feature;
	const struct kvm_x86_cpu_feature feature2;
	const char *name;
	const u64 reset_val;
	const u64 write_val;
	const u64 rsvd_val;
	const u32 index;
	const bool is_kvm_defined;
};

#define ____MSR_TEST(msr, str, val, rsvd, reset, feat, f2, is_kvm)	\
{									\
	.index = msr,							\
	.name = str,							\
	.write_val = val,						\
	.rsvd_val = rsvd,						\
	.reset_val = reset,						\
	.feature = X86_FEATURE_ ##feat,					\
	.feature2 = X86_FEATURE_ ##f2,					\
	.is_kvm_defined = is_kvm,					\
}

#define __MSR_TEST(msr, str, val, rsvd, reset, feat)			\
	____MSR_TEST(msr, str, val, rsvd, reset, feat, feat, false)

#define MSR_TEST_NON_ZERO(msr, val, rsvd, reset, feat)			\
	__MSR_TEST(msr, #msr, val, rsvd, reset, feat)

#define MSR_TEST(msr, val, rsvd, feat)					\
	__MSR_TEST(msr, #msr, val, rsvd, 0, feat)

#define MSR_TEST2(msr, val, rsvd, feat, f2)				\
	____MSR_TEST(msr, #msr, val, rsvd, 0, feat, f2, false)

/*
 * Note, use a page aligned value for the canonical value so that the value
 * is compatible with MSRs that use bits 11:0 for things other than addresses.
 */
static const u64 canonical_val = 0x123456789000ull;

/*
 * Arbitrary value with bits set in every byte, but not all bits set.  This is
 * also a non-canonical value, but that's coincidental (any 64-bit value with
 * an alternating 0s/1s pattern will be non-canonical).
 */
static const u64 u64_val = 0xaaaa5555aaaa5555ull;

#define MSR_TEST_CANONICAL(msr, feat)					\
	__MSR_TEST(msr, #msr, canonical_val, NONCANONICAL, 0, feat)

#define MSR_TEST_KVM(msr, val, rsvd, feat)				\
	____MSR_TEST(KVM_REG_ ##msr, #msr, val, rsvd, 0, feat, feat, true)

/*
 * The main struct must be scoped to a function due to the use of structures to
 * define features.  For the global structure, allocate enough space for the
 * foreseeable future without getting too ridiculous, to minimize maintenance
 * costs (bumping the array size every time an MSR is added is really annoying).
 */
static struct kvm_msr msrs[128];
static int idx;

static bool ignore_unsupported_msrs;

static u64 fixup_rdmsr_val(u32 msr, u64 want)
{
	/*
	 * AMD CPUs drop bits 63:32 on some MSRs that Intel CPUs support.  KVM
	 * is supposed to emulate that behavior based on guest vendor model
	 * (which is the same as the host vendor model for this test).
	 */
	if (!host_cpu_is_amd)
		return want;

	switch (msr) {
	case MSR_IA32_SYSENTER_ESP:
	case MSR_IA32_SYSENTER_EIP:
	case MSR_TSC_AUX:
		return want & GENMASK_ULL(31, 0);
	default:
		return want;
	}
}

static void __rdmsr(u32 msr, u64 want)
{
	u64 val;
	u8 vec;

	vec = rdmsr_safe(msr, &val);
	__GUEST_ASSERT(!vec, "Unexpected %s on RDMSR(0x%x)", ex_str(vec), msr);

	__GUEST_ASSERT(val == want, "Wanted 0x%lx from RDMSR(0x%x), got 0x%lx",
		       want, msr, val);
}

static void __wrmsr(u32 msr, u64 val)
{
	u8 vec;

	vec = wrmsr_safe(msr, val);
	__GUEST_ASSERT(!vec, "Unexpected %s on WRMSR(0x%x, 0x%lx)",
		       ex_str(vec), msr, val);
	__rdmsr(msr, fixup_rdmsr_val(msr, val));
}

static void guest_test_supported_msr(const struct kvm_msr *msr)
{
	__rdmsr(msr->index, msr->reset_val);
	__wrmsr(msr->index, msr->write_val);
	GUEST_SYNC(fixup_rdmsr_val(msr->index, msr->write_val));

	__rdmsr(msr->index, msr->reset_val);
}

static void guest_test_unsupported_msr(const struct kvm_msr *msr)
{
	u64 val;
	u8 vec;

	/*
	 * KVM's ABI with respect to ignore_msrs is a mess and largely beyond
	 * repair, just skip the unsupported MSR tests.
	 */
	if (ignore_unsupported_msrs)
		goto skip_wrmsr_gp;

	/*
	 * {S,U}_CET exist if IBT or SHSTK is supported, but with bits that are
	 * writable only if their associated feature is supported.  Skip the
	 * RDMSR #GP test if the secondary feature is supported, but perform
	 * the WRMSR #GP test as the to-be-written value is tied to the primary
	 * feature.  For all other MSRs, simply do nothing.
	 */
	if (this_cpu_has(msr->feature2)) {
		if  (msr->index != MSR_IA32_U_CET &&
		     msr->index != MSR_IA32_S_CET)
			goto skip_wrmsr_gp;

		goto skip_rdmsr_gp;
	}

	vec = rdmsr_safe(msr->index, &val);
	__GUEST_ASSERT(vec == GP_VECTOR, "Wanted #GP on RDMSR(0x%x), got %s",
		       msr->index, ex_str(vec));

skip_rdmsr_gp:
	vec = wrmsr_safe(msr->index, msr->write_val);
	__GUEST_ASSERT(vec == GP_VECTOR, "Wanted #GP on WRMSR(0x%x, 0x%lx), got %s",
		       msr->index, msr->write_val, ex_str(vec));

skip_wrmsr_gp:
	GUEST_SYNC(0);
}

void guest_test_reserved_val(const struct kvm_msr *msr)
{
	/* Skip reserved value checks as well, ignore_msrs is trully a mess. */
	if (ignore_unsupported_msrs)
		return;

	/*
	 * If the CPU will truncate the written value (e.g. SYSENTER on AMD),
	 * expect success and a truncated value, not #GP.
	 */
	if (!this_cpu_has(msr->feature) ||
	    msr->rsvd_val == fixup_rdmsr_val(msr->index, msr->rsvd_val)) {
		u8 vec = wrmsr_safe(msr->index, msr->rsvd_val);

		__GUEST_ASSERT(vec == GP_VECTOR,
			       "Wanted #GP on WRMSR(0x%x, 0x%lx), got %s",
			       msr->index, msr->rsvd_val, ex_str(vec));
	} else {
		__wrmsr(msr->index, msr->rsvd_val);
		__wrmsr(msr->index, msr->reset_val);
	}
}

static void guest_main(void)
{
	for (;;) {
		const struct kvm_msr *msr = &msrs[READ_ONCE(idx)];

		if (this_cpu_has(msr->feature))
			guest_test_supported_msr(msr);
		else
			guest_test_unsupported_msr(msr);

		if (msr->rsvd_val)
			guest_test_reserved_val(msr);

		GUEST_SYNC(msr->reset_val);
	}
}

static bool has_one_reg;
static bool use_one_reg;

#define KVM_X86_MAX_NR_REGS	1

static bool vcpu_has_reg(struct kvm_vcpu *vcpu, u64 reg)
{
	struct {
		struct kvm_reg_list list;
		u64 regs[KVM_X86_MAX_NR_REGS];
	} regs = {};
	int r, i;

	/*
	 * If KVM_GET_REG_LIST succeeds with n=0, i.e. there are no supported
	 * regs, then the vCPU obviously doesn't support the reg.
	 */
	r = __vcpu_ioctl(vcpu, KVM_GET_REG_LIST, &regs.list);
	if (!r)
		return false;

	TEST_ASSERT_EQ(errno, E2BIG);

	/*
	 * KVM x86 is expected to support enumerating a relative small number
	 * of regs.  The majority of registers supported by KVM_{G,S}ET_ONE_REG
	 * are enumerated via other ioctls, e.g. KVM_GET_MSR_INDEX_LIST.  For
	 * simplicity, hardcode the maximum number of regs and manually update
	 * the test as necessary.
	 */
	TEST_ASSERT(regs.list.n <= KVM_X86_MAX_NR_REGS,
		    "KVM reports %llu regs, test expects at most %u regs, stale test?",
		    regs.list.n, KVM_X86_MAX_NR_REGS);

	vcpu_ioctl(vcpu, KVM_GET_REG_LIST, &regs.list);
	for (i = 0; i < regs.list.n; i++) {
		if (regs.regs[i] == reg)
			return true;
	}

	return false;
}

static void host_test_kvm_reg(struct kvm_vcpu *vcpu)
{
	bool has_reg = vcpu_cpuid_has(vcpu, msrs[idx].feature);
	u64 reset_val = msrs[idx].reset_val;
	u64 write_val = msrs[idx].write_val;
	u64 rsvd_val = msrs[idx].rsvd_val;
	u32 reg = msrs[idx].index;
	u64 val;
	int r;

	if (!use_one_reg)
		return;

	TEST_ASSERT_EQ(vcpu_has_reg(vcpu, KVM_X86_REG_KVM(reg)), has_reg);

	if (!has_reg) {
		r = __vcpu_get_reg(vcpu, KVM_X86_REG_KVM(reg), &val);
		TEST_ASSERT(r && errno == EINVAL,
			    "Expected failure on get_reg(0x%x)", reg);
		rsvd_val = 0;
		goto out;
	}

	val = vcpu_get_reg(vcpu, KVM_X86_REG_KVM(reg));
	TEST_ASSERT(val == reset_val, "Wanted 0x%lx from get_reg(0x%x), got 0x%lx",
		    reset_val, reg, val);

	vcpu_set_reg(vcpu, KVM_X86_REG_KVM(reg), write_val);
	val = vcpu_get_reg(vcpu, KVM_X86_REG_KVM(reg));
	TEST_ASSERT(val == write_val, "Wanted 0x%lx from get_reg(0x%x), got 0x%lx",
		    write_val, reg, val);

out:
	r = __vcpu_set_reg(vcpu, KVM_X86_REG_KVM(reg), rsvd_val);
	TEST_ASSERT(r, "Expected failure on set_reg(0x%x, 0x%lx)", reg, rsvd_val);
}

static void host_test_msr(struct kvm_vcpu *vcpu, u64 guest_val)
{
	u64 reset_val = msrs[idx].reset_val;
	u32 msr = msrs[idx].index;
	u64 val;

	if (!kvm_cpu_has(msrs[idx].feature))
		return;

	val = vcpu_get_msr(vcpu, msr);
	TEST_ASSERT(val == guest_val, "Wanted 0x%lx from get_msr(0x%x), got 0x%lx",
		    guest_val, msr, val);

	if (use_one_reg)
		vcpu_set_reg(vcpu, KVM_X86_REG_MSR(msr), reset_val);
	else
		vcpu_set_msr(vcpu, msr, reset_val);

	val = vcpu_get_msr(vcpu, msr);
	TEST_ASSERT(val == reset_val, "Wanted 0x%lx from get_msr(0x%x), got 0x%lx",
		    reset_val, msr, val);

	if (!has_one_reg)
		return;

	val = vcpu_get_reg(vcpu, KVM_X86_REG_MSR(msr));
	TEST_ASSERT(val == reset_val, "Wanted 0x%lx from get_reg(0x%x), got 0x%lx",
		    reset_val, msr, val);
}

static void do_vcpu_run(struct kvm_vcpu *vcpu)
{
	struct ucall uc;

	for (;;) {
		vcpu_run(vcpu);

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_SYNC:
			host_test_msr(vcpu, uc.args[1]);
			return;
		case UCALL_PRINTF:
			pr_info("%s", uc.buffer);
			break;
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
		case UCALL_DONE:
			TEST_FAIL("Unexpected UCALL_DONE");
		default:
			TEST_FAIL("Unexpected ucall: %lu", uc.cmd);
		}
	}
}

static void vcpus_run(struct kvm_vcpu **vcpus, const int NR_VCPUS)
{
	int i;

	for (i = 0; i < NR_VCPUS; i++)
		do_vcpu_run(vcpus[i]);
}

#define MISC_ENABLES_RESET_VAL (MSR_IA32_MISC_ENABLE_PEBS_UNAVAIL | MSR_IA32_MISC_ENABLE_BTS_UNAVAIL)

static void test_msrs(void)
{
	const struct kvm_msr __msrs[] = {
		MSR_TEST_NON_ZERO(MSR_IA32_MISC_ENABLE,
				  MISC_ENABLES_RESET_VAL | MSR_IA32_MISC_ENABLE_FAST_STRING,
				  MSR_IA32_MISC_ENABLE_FAST_STRING, MISC_ENABLES_RESET_VAL, NONE),
		MSR_TEST_NON_ZERO(MSR_IA32_CR_PAT, 0x07070707, 0, 0x7040600070406, NONE),

		/*
		 * TSC_AUX is supported if RDTSCP *or* RDPID is supported.  Add
		 * entries for each features so that TSC_AUX doesn't exists for
		 * the "unsupported" vCPU, and obviously to test both cases.
		 */
		MSR_TEST2(MSR_TSC_AUX, 0x12345678, u64_val, RDTSCP, RDPID),
		MSR_TEST2(MSR_TSC_AUX, 0x12345678, u64_val, RDPID, RDTSCP),

		MSR_TEST(MSR_IA32_SYSENTER_CS, 0x1234, 0, NONE),
		/*
		 * SYSENTER_{ESP,EIP} are technically non-canonical on Intel,
		 * but KVM doesn't emulate that behavior on emulated writes,
		 * i.e. this test will observe different behavior if the MSR
		 * writes are handed by hardware vs. KVM.  KVM's behavior is
		 * intended (though far from ideal), so don't bother testing
		 * non-canonical values.
		 */
		MSR_TEST(MSR_IA32_SYSENTER_ESP, canonical_val, 0, NONE),
		MSR_TEST(MSR_IA32_SYSENTER_EIP, canonical_val, 0, NONE),

		MSR_TEST_CANONICAL(MSR_FS_BASE, LM),
		MSR_TEST_CANONICAL(MSR_GS_BASE, LM),
		MSR_TEST_CANONICAL(MSR_KERNEL_GS_BASE, LM),
		MSR_TEST_CANONICAL(MSR_LSTAR, LM),
		MSR_TEST_CANONICAL(MSR_CSTAR, LM),
		MSR_TEST(MSR_SYSCALL_MASK, 0xffffffff, 0, LM),

		MSR_TEST2(MSR_IA32_S_CET, CET_SHSTK_EN, CET_RESERVED, SHSTK, IBT),
		MSR_TEST2(MSR_IA32_S_CET, CET_ENDBR_EN, CET_RESERVED, IBT, SHSTK),
		MSR_TEST2(MSR_IA32_U_CET, CET_SHSTK_EN, CET_RESERVED, SHSTK, IBT),
		MSR_TEST2(MSR_IA32_U_CET, CET_ENDBR_EN, CET_RESERVED, IBT, SHSTK),
		MSR_TEST_CANONICAL(MSR_IA32_PL0_SSP, SHSTK),
		MSR_TEST(MSR_IA32_PL0_SSP, canonical_val, canonical_val | 1, SHSTK),
		MSR_TEST_CANONICAL(MSR_IA32_PL1_SSP, SHSTK),
		MSR_TEST(MSR_IA32_PL1_SSP, canonical_val, canonical_val | 1, SHSTK),
		MSR_TEST_CANONICAL(MSR_IA32_PL2_SSP, SHSTK),
		MSR_TEST(MSR_IA32_PL2_SSP, canonical_val, canonical_val | 1, SHSTK),
		MSR_TEST_CANONICAL(MSR_IA32_PL3_SSP, SHSTK),
		MSR_TEST(MSR_IA32_PL3_SSP, canonical_val, canonical_val | 1, SHSTK),

		MSR_TEST_KVM(GUEST_SSP, canonical_val, NONCANONICAL, SHSTK),
	};

	const struct kvm_x86_cpu_feature feat_none = X86_FEATURE_NONE;
	const struct kvm_x86_cpu_feature feat_lm = X86_FEATURE_LM;

	/*
	 * Create three vCPUs, but run them on the same task, to validate KVM's
	 * context switching of MSR state.  Don't pin the task to a pCPU to
	 * also validate KVM's handling of cross-pCPU migration.  Use the full
	 * set of features for the first two vCPUs, but clear all features in
	 * third vCPU in order to test both positive and negative paths.
	 */
	const int NR_VCPUS = 3;
	struct kvm_vcpu *vcpus[NR_VCPUS];
	struct kvm_vm *vm;
	int i;

	kvm_static_assert(sizeof(__msrs) <= sizeof(msrs));
	kvm_static_assert(ARRAY_SIZE(__msrs) <= ARRAY_SIZE(msrs));
	memcpy(msrs, __msrs, sizeof(__msrs));

	ignore_unsupported_msrs = kvm_is_ignore_msrs();

	vm = vm_create_with_vcpus(NR_VCPUS, guest_main, vcpus);

	sync_global_to_guest(vm, msrs);
	sync_global_to_guest(vm, ignore_unsupported_msrs);

	/*
	 * Clear features in the "unsupported features" vCPU.  This needs to be
	 * done before the first vCPU run as KVM's ABI is that guest CPUID is
	 * immutable once the vCPU has been run.
	 */
	for (idx = 0; idx < ARRAY_SIZE(__msrs); idx++) {
		/*
		 * Don't clear LM; selftests are 64-bit only, and KVM doesn't
		 * honor LM=0 for MSRs that are supposed to exist if and only
		 * if the vCPU is a 64-bit model.  Ditto for NONE; clearing a
		 * fake feature flag will result in false failures.
		 */
		if (memcmp(&msrs[idx].feature, &feat_lm, sizeof(feat_lm)) &&
		    memcmp(&msrs[idx].feature, &feat_none, sizeof(feat_none)))
			vcpu_clear_cpuid_feature(vcpus[2], msrs[idx].feature);
	}

	for (idx = 0; idx < ARRAY_SIZE(__msrs); idx++) {
		struct kvm_msr *msr = &msrs[idx];

		if (msr->is_kvm_defined) {
			for (i = 0; i < NR_VCPUS; i++)
				host_test_kvm_reg(vcpus[i]);
			continue;
		}

		/*
		 * Verify KVM_GET_SUPPORTED_CPUID and KVM_GET_MSR_INDEX_LIST
		 * are consistent with respect to MSRs whose existence is
		 * enumerated via CPUID.  Skip the check for FS/GS.base MSRs,
		 * as they aren't reported in the save/restore list since their
		 * state is managed via SREGS.
		 */
		TEST_ASSERT(msr->index == MSR_FS_BASE || msr->index == MSR_GS_BASE ||
			    kvm_msr_is_in_save_restore_list(msr->index) ==
			    (kvm_cpu_has(msr->feature) || kvm_cpu_has(msr->feature2)),
			    "%s %s in save/restore list, but %s according to CPUID", msr->name,
			    kvm_msr_is_in_save_restore_list(msr->index) ? "is" : "isn't",
			    (kvm_cpu_has(msr->feature) || kvm_cpu_has(msr->feature2)) ?
			    "supported" : "unsupported");

		sync_global_to_guest(vm, idx);

		vcpus_run(vcpus, NR_VCPUS);
		vcpus_run(vcpus, NR_VCPUS);
	}

	kvm_vm_free(vm);
}

int main(void)
{
	has_one_reg = kvm_has_cap(KVM_CAP_ONE_REG);

	test_msrs();

	if (has_one_reg) {
		use_one_reg = true;
		test_msrs();
	}
}
