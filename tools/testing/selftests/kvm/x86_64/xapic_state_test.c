// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE /* for program_invocation_short_name */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "apic.h"
#include "kvm_util.h"
#include "processor.h"
#include "test_util.h"

struct kvm_vcpu {
	uint32_t id;
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

static void ____test_icr(struct kvm_vm *vm, struct kvm_vcpu *vcpu, uint64_t val)
{
	struct kvm_lapic_state xapic;
	struct ucall uc;
	uint64_t icr;

	/*
	 * Tell the guest what ICR value to write.  Use the IRR to pass info,
	 * all bits are valid and should not be modified by KVM (ignoring the
	 * fact that vectors 0-15 are technically illegal).
	 */
	vcpu_ioctl(vm, vcpu->id, KVM_GET_LAPIC, &xapic);
	*((u32 *)&xapic.regs[APIC_IRR]) = val;
	*((u32 *)&xapic.regs[APIC_IRR + 0x10]) = val >> 32;
	vcpu_ioctl(vm, vcpu->id, KVM_SET_LAPIC, &xapic);

	vcpu_run(vm, vcpu->id);
	ASSERT_EQ(get_ucall(vm, vcpu->id, &uc), UCALL_SYNC);
	ASSERT_EQ(uc.args[1], val);

	vcpu_ioctl(vm, vcpu->id, KVM_GET_LAPIC, &xapic);
	icr = (u64)(*((u32 *)&xapic.regs[APIC_ICR])) |
	      (u64)(*((u32 *)&xapic.regs[APIC_ICR2])) << 32;
	if (!vcpu->is_x2apic)
		val &= (-1u | (0xffull << (32 + 24)));
	ASSERT_EQ(icr, val & ~APIC_ICR_BUSY);
}

static void __test_icr(struct kvm_vm *vm, struct kvm_vcpu *vcpu, uint64_t val)
{
	____test_icr(vm, vcpu, val | APIC_ICR_BUSY);
	____test_icr(vm, vcpu, val & ~(u64)APIC_ICR_BUSY);
}

static void test_icr(struct kvm_vm *vm, struct kvm_vcpu *vcpu)
{
	uint64_t icr, i, j;

	icr = APIC_DEST_SELF | APIC_INT_ASSERT | APIC_DM_FIXED;
	for (i = 0; i <= 0xff; i++)
		__test_icr(vm, vcpu, icr | i);

	icr = APIC_INT_ASSERT | APIC_DM_FIXED;
	for (i = 0; i <= 0xff; i++)
		__test_icr(vm, vcpu, icr | i);

	/*
	 * Send all flavors of IPIs to non-existent vCPUs.  TODO: use number of
	 * vCPUs, not vcpu.id + 1.  Arbitrarily use vector 0xff.
	 */
	icr = APIC_INT_ASSERT | 0xff;
	for (i = vcpu->id + 1; i < 0xff; i++) {
		for (j = 0; j < 8; j++)
			__test_icr(vm, vcpu, i << (32 + 24) | APIC_INT_ASSERT | (j << 8));
	}

	/* And again with a shorthand destination for all types of IPIs. */
	icr = APIC_DEST_ALLBUT | APIC_INT_ASSERT;
	for (i = 0; i < 8; i++)
		__test_icr(vm, vcpu, icr | (i << 8));

	/* And a few garbage value, just make sure it's an IRQ (blocked). */
	__test_icr(vm, vcpu, 0xa5a5a5a5a5a5a5a5 & ~APIC_DM_FIXED_MASK);
	__test_icr(vm, vcpu, 0x5a5a5a5a5a5a5a5a & ~APIC_DM_FIXED_MASK);
	__test_icr(vm, vcpu, -1ull & ~APIC_DM_FIXED_MASK);
}

int main(int argc, char *argv[])
{
	struct kvm_vcpu vcpu = {
		.id = 0,
		.is_x2apic = true,
	};
	struct kvm_cpuid2 *cpuid;
	struct kvm_vm *vm;
	int i;

	vm = vm_create_default(vcpu.id, 0, x2apic_guest_code);
	test_icr(vm, &vcpu);
	kvm_vm_free(vm);

	/*
	 * Use a second VM for the xAPIC test so that x2APIC can be hidden from
	 * the guest in order to test AVIC.  KVM disallows changing CPUID after
	 * KVM_RUN and AVIC is disabled if _any_ vCPU is allowed to use x2APIC.
	 */
	vm = vm_create_default(vcpu.id, 0, xapic_guest_code);
	vcpu.is_x2apic = false;

	cpuid = vcpu_get_cpuid(vm, vcpu.id);
	for (i = 0; i < cpuid->nent; i++) {
		if (cpuid->entries[i].function == 1)
			break;
	}
	cpuid->entries[i].ecx &= ~BIT(21);
	vcpu_set_cpuid(vm, vcpu.id, cpuid);

	virt_pg_map(vm, APIC_DEFAULT_GPA, APIC_DEFAULT_GPA);
	test_icr(vm, &vcpu);
	kvm_vm_free(vm);
}
