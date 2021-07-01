// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021, Red Hat Inc.
 *
 * Generic tests for KVM CPUID set/get ioctls
 */
#include <asm/kvm_para.h>
#include <linux/kvm_para.h>
#include <stdint.h>

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"

#define VCPU_ID 0

/* CPUIDs known to differ */
struct {
	u32 function;
	u32 index;
} mangled_cpuids[] = {
	/*
	 * These entries depend on the vCPU's XCR0 register and IA32_XSS MSR,
	 * which are not controlled for by this test.
	 */
	{.function = 0xd, .index = 0},
	{.function = 0xd, .index = 1},
};

static void test_guest_cpuids(struct kvm_cpuid2 *guest_cpuid)
{
	int i;
	u32 eax, ebx, ecx, edx;

	for (i = 0; i < guest_cpuid->nent; i++) {
		eax = guest_cpuid->entries[i].function;
		ecx = guest_cpuid->entries[i].index;

		cpuid(&eax, &ebx, &ecx, &edx);

		GUEST_ASSERT(eax == guest_cpuid->entries[i].eax &&
			     ebx == guest_cpuid->entries[i].ebx &&
			     ecx == guest_cpuid->entries[i].ecx &&
			     edx == guest_cpuid->entries[i].edx);
	}

}

static void test_cpuid_40000000(struct kvm_cpuid2 *guest_cpuid)
{
	u32 eax = 0x40000000, ebx, ecx = 0, edx;

	cpuid(&eax, &ebx, &ecx, &edx);

	GUEST_ASSERT(eax == 0x40000001);
}

static void guest_main(struct kvm_cpuid2 *guest_cpuid)
{
	GUEST_SYNC(1);

	test_guest_cpuids(guest_cpuid);

	GUEST_SYNC(2);

	test_cpuid_40000000(guest_cpuid);

	GUEST_DONE();
}

static bool is_cpuid_mangled(struct kvm_cpuid_entry2 *entrie)
{
	int i;

	for (i = 0; i < sizeof(mangled_cpuids); i++) {
		if (mangled_cpuids[i].function == entrie->function &&
		    mangled_cpuids[i].index == entrie->index)
			return true;
	}

	return false;
}

static void check_cpuid(struct kvm_cpuid2 *cpuid, struct kvm_cpuid_entry2 *entrie)
{
	int i;

	for (i = 0; i < cpuid->nent; i++) {
		if (cpuid->entries[i].function == entrie->function &&
		    cpuid->entries[i].index == entrie->index) {
			if (is_cpuid_mangled(entrie))
				return;

			TEST_ASSERT(cpuid->entries[i].eax == entrie->eax &&
				    cpuid->entries[i].ebx == entrie->ebx &&
				    cpuid->entries[i].ecx == entrie->ecx &&
				    cpuid->entries[i].edx == entrie->edx,
				    "CPUID 0x%x.%x differ: 0x%x:0x%x:0x%x:0x%x vs 0x%x:0x%x:0x%x:0x%x",
				    entrie->function, entrie->index,
				    cpuid->entries[i].eax, cpuid->entries[i].ebx,
				    cpuid->entries[i].ecx, cpuid->entries[i].edx,
				    entrie->eax, entrie->ebx, entrie->ecx, entrie->edx);
			return;
		}
	}

	TEST_ASSERT(false, "CPUID 0x%x.%x not found", entrie->function, entrie->index);
}

static void compare_cpuids(struct kvm_cpuid2 *cpuid1, struct kvm_cpuid2 *cpuid2)
{
	int i;

	for (i = 0; i < cpuid1->nent; i++)
		check_cpuid(cpuid2, &cpuid1->entries[i]);

	for (i = 0; i < cpuid2->nent; i++)
		check_cpuid(cpuid1, &cpuid2->entries[i]);
}

static void run_vcpu(struct kvm_vm *vm, uint32_t vcpuid, int stage)
{
	struct ucall uc;

	_vcpu_run(vm, vcpuid);

	switch (get_ucall(vm, vcpuid, &uc)) {
	case UCALL_SYNC:
		TEST_ASSERT(!strcmp((const char *)uc.args[0], "hello") &&
			    uc.args[1] == stage + 1,
			    "Stage %d: Unexpected register values vmexit, got %lx",
			    stage + 1, (ulong)uc.args[1]);
		return;
	case UCALL_DONE:
		return;
	case UCALL_ABORT:
		TEST_ASSERT(false, "%s at %s:%ld\n\tvalues: %#lx, %#lx", (const char *)uc.args[0],
			    __FILE__, uc.args[1], uc.args[2], uc.args[3]);
	default:
		TEST_ASSERT(false, "Unexpected exit: %s",
			    exit_reason_str(vcpu_state(vm, vcpuid)->exit_reason));
	}
}

struct kvm_cpuid2 *vcpu_alloc_cpuid(struct kvm_vm *vm, vm_vaddr_t *p_gva, struct kvm_cpuid2 *cpuid)
{
	int size = sizeof(*cpuid) + cpuid->nent * sizeof(cpuid->entries[0]);
	vm_vaddr_t gva = vm_vaddr_alloc(vm, size, KVM_UTIL_MIN_VADDR);
	struct kvm_cpuid2 *guest_cpuids = addr_gva2hva(vm, gva);

	memcpy(guest_cpuids, cpuid, size);

	*p_gva = gva;
	return guest_cpuids;
}

int main(void)
{
	struct kvm_cpuid2 *supp_cpuid, *cpuid2;
	vm_vaddr_t cpuid_gva;
	struct kvm_vm *vm;
	int stage;

	vm = vm_create_default(VCPU_ID, 0, guest_main);

	supp_cpuid = kvm_get_supported_cpuid();
	cpuid2 = vcpu_get_cpuid(vm, VCPU_ID);

	compare_cpuids(supp_cpuid, cpuid2);

	vcpu_alloc_cpuid(vm, &cpuid_gva, cpuid2);

	vcpu_args_set(vm, VCPU_ID, 1, cpuid_gva);

	for (stage = 0; stage < 3; stage++)
		run_vcpu(vm, VCPU_ID, stage);

	kvm_vm_free(vm);
}
