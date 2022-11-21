// SPDX-License-Identifier: GPL-2.0
/*
 * Test for x86 KVM_CAP_HYPERV_CPUID
 *
 * Copyright (C) 2018, Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 *
 */

#define _GNU_SOURCE /* for program_invocation_short_name */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "vmx.h"

#define VCPU_ID 0

static void guest_code(void)
{
}

static bool smt_possible(void)
{
	char buf[16];
	FILE *f;
	bool res = true;

	f = fopen("/sys/devices/system/cpu/smt/control", "r");
	if (f) {
		if (fread(buf, sizeof(*buf), sizeof(buf), f) > 0) {
			if (!strncmp(buf, "forceoff", 8) ||
			    !strncmp(buf, "notsupported", 12))
				res = false;
		}
		fclose(f);
	}

	return res;
}

static void test_hv_cpuid(struct kvm_cpuid2 *hv_cpuid_entries,
			  bool evmcs_expected)
{
	int i;
	int nent_expected = 10;
	u32 test_val;

	TEST_ASSERT(hv_cpuid_entries->nent == nent_expected,
		    "KVM_GET_SUPPORTED_HV_CPUID should return %d entries"
		    " (returned %d)",
		    nent_expected, hv_cpuid_entries->nent);

	for (i = 0; i < hv_cpuid_entries->nent; i++) {
		struct kvm_cpuid_entry2 *entry = &hv_cpuid_entries->entries[i];

		TEST_ASSERT((entry->function >= 0x40000000) &&
			    (entry->function <= 0x40000082),
			    "function %x is our of supported range",
			    entry->function);

		TEST_ASSERT(entry->index == 0,
			    ".index field should be zero");

		TEST_ASSERT(entry->flags == 0,
			    ".flags field should be zero");

		TEST_ASSERT(!entry->padding[0] && !entry->padding[1] &&
			    !entry->padding[2], "padding should be zero");

		switch (entry->function) {
		case 0x40000000:
			test_val = 0x40000082;

			TEST_ASSERT(entry->eax == test_val,
				    "Wrong max leaf report in 0x40000000.EAX: %x"
				    " (evmcs=%d)",
				    entry->eax, evmcs_expected
				);
			break;
		case 0x40000004:
			test_val = entry->eax & (1UL << 18);

			TEST_ASSERT(!!test_val == !smt_possible(),
				    "NoNonArchitecturalCoreSharing bit"
				    " doesn't reflect SMT setting");
			break;
		case 0x4000000A:
			TEST_ASSERT(entry->eax & (1UL << 19),
				    "Enlightened MSR-Bitmap should always be supported"
				    " 0x40000000.EAX: %x", entry->eax);
			if (evmcs_expected)
				TEST_ASSERT((entry->eax & 0xffff) == 0x101,
				    "Supported Enlightened VMCS version range is supposed to be 1:1"
				    " 0x40000000.EAX: %x", entry->eax);

			break;
		default:
			break;

		}
		/*
		 * If needed for debug:
		 * fprintf(stdout,
		 *	"CPUID%lx EAX=0x%lx EBX=0x%lx ECX=0x%lx EDX=0x%lx\n",
		 *	entry->function, entry->eax, entry->ebx, entry->ecx,
		 *	entry->edx);
		 */
	}
}

void test_hv_cpuid_e2big(struct kvm_vm *vm, bool system)
{
	static struct kvm_cpuid2 cpuid = {.nent = 0};
	int ret;

	if (!system)
		ret = _vcpu_ioctl(vm, VCPU_ID, KVM_GET_SUPPORTED_HV_CPUID, &cpuid);
	else
		ret = _kvm_ioctl(vm, KVM_GET_SUPPORTED_HV_CPUID, &cpuid);

	TEST_ASSERT(ret == -1 && errno == E2BIG,
		    "%s KVM_GET_SUPPORTED_HV_CPUID didn't fail with -E2BIG when"
		    " it should have: %d %d", system ? "KVM" : "vCPU", ret, errno);
}

int main(int argc, char *argv[])
{
	struct kvm_vm *vm;
	struct kvm_cpuid2 *hv_cpuid_entries;

	/* Tell stdout not to buffer its content */
	setbuf(stdout, NULL);

	if (!kvm_check_cap(KVM_CAP_HYPERV_CPUID)) {
		print_skip("KVM_CAP_HYPERV_CPUID not supported");
		exit(KSFT_SKIP);
	}

	vm = vm_create_default(VCPU_ID, 0, guest_code);

	/* Test vCPU ioctl version */
	test_hv_cpuid_e2big(vm, false);

	hv_cpuid_entries = vcpu_get_supported_hv_cpuid(vm, VCPU_ID);
	test_hv_cpuid(hv_cpuid_entries, false);
	free(hv_cpuid_entries);

	if (!nested_vmx_supported() ||
	    !kvm_check_cap(KVM_CAP_HYPERV_ENLIGHTENED_VMCS)) {
		print_skip("Enlightened VMCS is unsupported");
		goto do_sys;
	}
	vcpu_enable_evmcs(vm, VCPU_ID);
	hv_cpuid_entries = vcpu_get_supported_hv_cpuid(vm, VCPU_ID);
	test_hv_cpuid(hv_cpuid_entries, true);
	free(hv_cpuid_entries);

do_sys:
	/* Test system ioctl version */
	if (!kvm_check_cap(KVM_CAP_SYS_HYPERV_CPUID)) {
		print_skip("KVM_CAP_SYS_HYPERV_CPUID not supported");
		goto out;
	}

	test_hv_cpuid_e2big(vm, true);

	hv_cpuid_entries = kvm_get_supported_hv_cpuid();
	test_hv_cpuid(hv_cpuid_entries, nested_vmx_supported());

out:
	kvm_vm_free(vm);

	return 0;
}
