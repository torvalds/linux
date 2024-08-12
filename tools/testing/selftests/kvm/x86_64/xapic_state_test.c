// SPDX-License-Identifier: GPL-2.0-only
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "apic.h"
#include "kvm_util.h"
#include "processor.h"
#include "test_util.h"

struct xapic_vcpu {
	struct kvm_vcpu *vcpu;
	bool is_x2apic;
};

static void xapic_guest_code(void)
{
	asm volatile("cli");

	xapic_enable();

	while (1) {
		uint64_t val = (u64)xapic_read_reg(APIC_IRR) |
			       (u64)xapic_read_reg(APIC_IRR + 0x10) << 32;

		xapic_write_reg(APIC_ICR2, val >> 32);
		xapic_write_reg(APIC_ICR, val);
		GUEST_SYNC(val);
	}
}

static void x2apic_guest_code(void)
{
	asm volatile("cli");

	x2apic_enable();

	do {
		uint64_t val = x2apic_read_reg(APIC_IRR) |
			       x2apic_read_reg(APIC_IRR + 0x10) << 32;

		x2apic_write_reg(APIC_ICR, val);
		GUEST_SYNC(val);
	} while (1);
}

static void ____test_icr(struct xapic_vcpu *x, uint64_t val)
{
	struct kvm_vcpu *vcpu = x->vcpu;
	struct kvm_lapic_state xapic;
	struct ucall uc;
	uint64_t icr;

	/*
	 * Tell the guest what ICR value to write.  Use the IRR to pass info,
	 * all bits are valid and should not be modified by KVM (ignoring the
	 * fact that vectors 0-15 are technically illegal).
	 */
	vcpu_ioctl(vcpu, KVM_GET_LAPIC, &xapic);
	*((u32 *)&xapic.regs[APIC_IRR]) = val;
	*((u32 *)&xapic.regs[APIC_IRR + 0x10]) = val >> 32;
	vcpu_ioctl(vcpu, KVM_SET_LAPIC, &xapic);

	vcpu_run(vcpu);
	TEST_ASSERT_EQ(get_ucall(vcpu, &uc), UCALL_SYNC);
	TEST_ASSERT_EQ(uc.args[1], val);

	vcpu_ioctl(vcpu, KVM_GET_LAPIC, &xapic);
	icr = (u64)(*((u32 *)&xapic.regs[APIC_ICR])) |
	      (u64)(*((u32 *)&xapic.regs[APIC_ICR2])) << 32;
	if (!x->is_x2apic) {
		val &= (-1u | (0xffull << (32 + 24)));
		TEST_ASSERT_EQ(icr, val & ~APIC_ICR_BUSY);
	} else {
		TEST_ASSERT_EQ(icr & ~APIC_ICR_BUSY, val & ~APIC_ICR_BUSY);
	}
}

#define X2APIC_RSVED_BITS_MASK  (GENMASK_ULL(31,20) | \
				 GENMASK_ULL(17,16) | \
				 GENMASK_ULL(13,13))

static void __test_icr(struct xapic_vcpu *x, uint64_t val)
{
	if (x->is_x2apic) {
		/* Hardware writing vICR register requires reserved bits 31:20,
		 * 17:16 and 13 kept as zero to avoid #GP exception. Data value
		 * written to vICR should mask out those bits above.
		 */
		val &= ~X2APIC_RSVED_BITS_MASK;
	}
	____test_icr(x, val | APIC_ICR_BUSY);
	____test_icr(x, val & ~(u64)APIC_ICR_BUSY);
}

static void test_icr(struct xapic_vcpu *x)
{
	struct kvm_vcpu *vcpu = x->vcpu;
	uint64_t icr, i, j;

	icr = APIC_DEST_SELF | APIC_INT_ASSERT | APIC_DM_FIXED;
	for (i = 0; i <= 0xff; i++)
		__test_icr(x, icr | i);

	icr = APIC_INT_ASSERT | APIC_DM_FIXED;
	for (i = 0; i <= 0xff; i++)
		__test_icr(x, icr | i);

	/*
	 * Send all flavors of IPIs to non-existent vCPUs.  TODO: use number of
	 * vCPUs, not vcpu.id + 1.  Arbitrarily use vector 0xff.
	 */
	icr = APIC_INT_ASSERT | 0xff;
	for (i = 0; i < 0xff; i++) {
		if (i == vcpu->id)
			continue;
		for (j = 0; j < 8; j++)
			__test_icr(x, i << (32 + 24) | icr | (j << 8));
	}

	/* And again with a shorthand destination for all types of IPIs. */
	icr = APIC_DEST_ALLBUT | APIC_INT_ASSERT;
	for (i = 0; i < 8; i++)
		__test_icr(x, icr | (i << 8));

	/* And a few garbage value, just make sure it's an IRQ (blocked). */
	__test_icr(x, 0xa5a5a5a5a5a5a5a5 & ~APIC_DM_FIXED_MASK);
	__test_icr(x, 0x5a5a5a5a5a5a5a5a & ~APIC_DM_FIXED_MASK);
	__test_icr(x, -1ull & ~APIC_DM_FIXED_MASK);
}

static void __test_apic_id(struct kvm_vcpu *vcpu, uint64_t apic_base)
{
	uint32_t apic_id, expected;
	struct kvm_lapic_state xapic;

	vcpu_set_msr(vcpu, MSR_IA32_APICBASE, apic_base);

	vcpu_ioctl(vcpu, KVM_GET_LAPIC, &xapic);

	expected = apic_base & X2APIC_ENABLE ? vcpu->id : vcpu->id << 24;
	apic_id = *((u32 *)&xapic.regs[APIC_ID]);

	TEST_ASSERT(apic_id == expected,
		    "APIC_ID not set back to %s format; wanted = %x, got = %x",
		    (apic_base & X2APIC_ENABLE) ? "x2APIC" : "xAPIC",
		    expected, apic_id);
}

/*
 * Verify that KVM switches the APIC_ID between xAPIC and x2APIC when userspace
 * stuffs MSR_IA32_APICBASE.  Setting the APIC_ID when x2APIC is enabled and
 * when the APIC transitions for DISABLED to ENABLED is architectural behavior
 * (on Intel), whereas the x2APIC => xAPIC transition behavior is KVM ABI since
 * attempted to transition from x2APIC to xAPIC without disabling the APIC is
 * architecturally disallowed.
 */
static void test_apic_id(void)
{
	const uint32_t NR_VCPUS = 3;
	struct kvm_vcpu *vcpus[NR_VCPUS];
	uint64_t apic_base;
	struct kvm_vm *vm;
	int i;

	vm = vm_create_with_vcpus(NR_VCPUS, NULL, vcpus);
	vm_enable_cap(vm, KVM_CAP_X2APIC_API, KVM_X2APIC_API_USE_32BIT_IDS);

	for (i = 0; i < NR_VCPUS; i++) {
		apic_base = vcpu_get_msr(vcpus[i], MSR_IA32_APICBASE);

		TEST_ASSERT(apic_base & MSR_IA32_APICBASE_ENABLE,
			    "APIC not in ENABLED state at vCPU RESET");
		TEST_ASSERT(!(apic_base & X2APIC_ENABLE),
			    "APIC not in xAPIC mode at vCPU RESET");

		__test_apic_id(vcpus[i], apic_base);
		__test_apic_id(vcpus[i], apic_base | X2APIC_ENABLE);
		__test_apic_id(vcpus[i], apic_base);
	}

	kvm_vm_free(vm);
}

int main(int argc, char *argv[])
{
	struct xapic_vcpu x = {
		.vcpu = NULL,
		.is_x2apic = true,
	};
	struct kvm_vm *vm;

	vm = vm_create_with_one_vcpu(&x.vcpu, x2apic_guest_code);
	test_icr(&x);
	kvm_vm_free(vm);

	/*
	 * Use a second VM for the xAPIC test so that x2APIC can be hidden from
	 * the guest in order to test AVIC.  KVM disallows changing CPUID after
	 * KVM_RUN and AVIC is disabled if _any_ vCPU is allowed to use x2APIC.
	 */
	vm = vm_create_with_one_vcpu(&x.vcpu, xapic_guest_code);
	x.is_x2apic = false;

	vcpu_clear_cpuid_feature(x.vcpu, X86_FEATURE_X2APIC);

	virt_pg_map(vm, APIC_DEFAULT_GPA, APIC_DEFAULT_GPA);
	test_icr(&x);
	kvm_vm_free(vm);

	test_apic_id();
}
