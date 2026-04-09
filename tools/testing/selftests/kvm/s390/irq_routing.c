// SPDX-License-Identifier: GPL-2.0-only
/*
 * IRQ routing offset tests.
 *
 * Copyright IBM Corp. 2026
 *
 * Authors:
 *  Janosch Frank <frankja@linux.ibm.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "test_util.h"
#include "kvm_util.h"
#include "kselftest.h"
#include "ucall_common.h"

extern char guest_code[];
asm("guest_code:\n"
    "diag %r0,%r0,0\n"
    "j .\n");

static void test(void)
{
	struct kvm_irq_routing *routing;
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	vm_paddr_t mem;
	int ret;

	struct kvm_irq_routing_entry ue = {
		.type = KVM_IRQ_ROUTING_S390_ADAPTER,
		.gsi = 1,
	};

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);
	mem = vm_phy_pages_alloc(vm, 2, 4096 * 42, 0);

	routing = kvm_gsi_routing_create();
	routing->nr = 1;
	routing->entries[0] = ue;
	routing->entries[0].u.adapter.summary_addr = (uintptr_t)mem;
	routing->entries[0].u.adapter.ind_addr = (uintptr_t)mem;

	routing->entries[0].u.adapter.summary_offset = 4096 * 8;
	ret = __vm_ioctl(vm, KVM_SET_GSI_ROUTING, routing);
	ksft_test_result(ret == -1 && errno == EINVAL, "summary offset outside of page\n");

	routing->entries[0].u.adapter.summary_offset -= 4;
	ret = __vm_ioctl(vm, KVM_SET_GSI_ROUTING, routing);
	ksft_test_result(ret == 0, "summary offset inside of page\n");

	routing->entries[0].u.adapter.ind_offset = 4096 * 8;
	ret = __vm_ioctl(vm, KVM_SET_GSI_ROUTING, routing);
	ksft_test_result(ret == -1 && errno == EINVAL, "ind offset outside of page\n");

	routing->entries[0].u.adapter.ind_offset -= 4;
	ret = __vm_ioctl(vm, KVM_SET_GSI_ROUTING, routing);
	ksft_test_result(ret == 0, "ind offset inside of page\n");

	kvm_vm_free(vm);
}

int main(int argc, char *argv[])
{
	TEST_REQUIRE(kvm_has_cap(KVM_CAP_IRQ_ROUTING));

	ksft_print_header();
	ksft_set_plan(4);
	test();

	ksft_finished();	/* Print results and exit() accordingly */
}
