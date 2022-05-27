// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020, Google LLC.
 *
 * Tests for KVM paravirtual feature disablement
 */
#include <asm/kvm_para.h>
#include <linux/kvm_para.h>
#include <stdint.h>

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"

extern unsigned char rdmsr_start;
extern unsigned char rdmsr_end;

static u64 do_rdmsr(u32 idx)
{
	u32 lo, hi;

	asm volatile("rdmsr_start: rdmsr;"
		     "rdmsr_end:"
		     : "=a"(lo), "=c"(hi)
		     : "c"(idx));

	return (((u64) hi) << 32) | lo;
}

extern unsigned char wrmsr_start;
extern unsigned char wrmsr_end;

static void do_wrmsr(u32 idx, u64 val)
{
	u32 lo, hi;

	lo = val;
	hi = val >> 32;

	asm volatile("wrmsr_start: wrmsr;"
		     "wrmsr_end:"
		     : : "a"(lo), "c"(idx), "d"(hi));
}

static int nr_gp;

static void guest_gp_handler(struct ex_regs *regs)
{
	unsigned char *rip = (unsigned char *)regs->rip;
	bool r, w;

	r = rip == &rdmsr_start;
	w = rip == &wrmsr_start;
	GUEST_ASSERT(r || w);

	nr_gp++;

	if (r)
		regs->rip = (uint64_t)&rdmsr_end;
	else
		regs->rip = (uint64_t)&wrmsr_end;
}

struct msr_data {
	uint32_t idx;
	const char *name;
};

#define TEST_MSR(msr) { .idx = msr, .name = #msr }
#define UCALL_PR_MSR 0xdeadbeef
#define PR_MSR(msr) ucall(UCALL_PR_MSR, 1, msr)

/*
 * KVM paravirtual msrs to test. Expect a #GP if any of these msrs are read or
 * written, as the KVM_CPUID_FEATURES leaf is cleared.
 */
static struct msr_data msrs_to_test[] = {
	TEST_MSR(MSR_KVM_SYSTEM_TIME),
	TEST_MSR(MSR_KVM_SYSTEM_TIME_NEW),
	TEST_MSR(MSR_KVM_WALL_CLOCK),
	TEST_MSR(MSR_KVM_WALL_CLOCK_NEW),
	TEST_MSR(MSR_KVM_ASYNC_PF_EN),
	TEST_MSR(MSR_KVM_STEAL_TIME),
	TEST_MSR(MSR_KVM_PV_EOI_EN),
	TEST_MSR(MSR_KVM_POLL_CONTROL),
	TEST_MSR(MSR_KVM_ASYNC_PF_INT),
	TEST_MSR(MSR_KVM_ASYNC_PF_ACK),
};

static void test_msr(struct msr_data *msr)
{
	PR_MSR(msr);
	do_rdmsr(msr->idx);
	GUEST_ASSERT(READ_ONCE(nr_gp) == 1);

	nr_gp = 0;
	do_wrmsr(msr->idx, 0);
	GUEST_ASSERT(READ_ONCE(nr_gp) == 1);
	nr_gp = 0;
}

struct hcall_data {
	uint64_t nr;
	const char *name;
};

#define TEST_HCALL(hc) { .nr = hc, .name = #hc }
#define UCALL_PR_HCALL 0xdeadc0de
#define PR_HCALL(hc) ucall(UCALL_PR_HCALL, 1, hc)

/*
 * KVM hypercalls to test. Expect -KVM_ENOSYS when called, as the corresponding
 * features have been cleared in KVM_CPUID_FEATURES.
 */
static struct hcall_data hcalls_to_test[] = {
	TEST_HCALL(KVM_HC_KICK_CPU),
	TEST_HCALL(KVM_HC_SEND_IPI),
	TEST_HCALL(KVM_HC_SCHED_YIELD),
};

static void test_hcall(struct hcall_data *hc)
{
	uint64_t r;

	PR_HCALL(hc);
	r = kvm_hypercall(hc->nr, 0, 0, 0, 0);
	GUEST_ASSERT(r == -KVM_ENOSYS);
}

static void guest_main(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(msrs_to_test); i++) {
		test_msr(&msrs_to_test[i]);
	}

	for (i = 0; i < ARRAY_SIZE(hcalls_to_test); i++) {
		test_hcall(&hcalls_to_test[i]);
	}

	GUEST_DONE();
}

static void clear_kvm_cpuid_features(struct kvm_cpuid2 *cpuid)
{
	struct kvm_cpuid_entry2 ent = {0};

	ent.function = KVM_CPUID_FEATURES;
	TEST_ASSERT(set_cpuid(cpuid, &ent),
		    "failed to clear KVM_CPUID_FEATURES leaf");
}

static void pr_msr(struct ucall *uc)
{
	struct msr_data *msr = (struct msr_data *)uc->args[0];

	pr_info("testing msr: %s (%#x)\n", msr->name, msr->idx);
}

static void pr_hcall(struct ucall *uc)
{
	struct hcall_data *hc = (struct hcall_data *)uc->args[0];

	pr_info("testing hcall: %s (%lu)\n", hc->name, hc->nr);
}

static void handle_abort(struct ucall *uc)
{
	TEST_FAIL("%s at %s:%ld", (const char *)uc->args[0],
		  __FILE__, uc->args[1]);
}

static void enter_guest(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;
	struct ucall uc;

	while (true) {
		vcpu_run(vcpu);
		TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
			    "unexpected exit reason: %u (%s)",
			    run->exit_reason, exit_reason_str(run->exit_reason));

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_PR_MSR:
			pr_msr(&uc);
			break;
		case UCALL_PR_HCALL:
			pr_hcall(&uc);
			break;
		case UCALL_ABORT:
			handle_abort(&uc);
			return;
		case UCALL_DONE:
			return;
		}
	}
}

int main(void)
{
	struct kvm_cpuid2 *best;
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;

	TEST_REQUIRE(kvm_has_cap(KVM_CAP_ENFORCE_PV_FEATURE_CPUID));

	vm = vm_create_with_one_vcpu(&vcpu, guest_main);

	vcpu_enable_cap(vcpu, KVM_CAP_ENFORCE_PV_FEATURE_CPUID, 1);

	best = kvm_get_supported_cpuid();
	clear_kvm_cpuid_features(best);
	vcpu_set_cpuid(vcpu, best);

	vm_init_descriptor_tables(vm);
	vcpu_init_descriptor_tables(vcpu);
	vm_install_exception_handler(vm, GP_VECTOR, guest_gp_handler);

	enter_guest(vcpu);
	kvm_vm_free(vm);
}
