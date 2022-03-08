// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Test for s390x KVM_S390_MEM_OP
 *
 * Copyright (C) 2019, Red Hat, Inc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "test_util.h"
#include "kvm_util.h"

#define VCPU_ID 1

static uint8_t mem1[65536];
static uint8_t mem2[65536];

struct test_default {
	struct kvm_vm *kvm_vm;
	struct kvm_run *run;
	int size;
};

static struct test_default test_default_init(void *guest_code)
{
	struct test_default t;

	t.size = min((size_t)kvm_check_cap(KVM_CAP_S390_MEM_OP), sizeof(mem1));
	t.kvm_vm = vm_create_default(VCPU_ID, 0, guest_code);
	t.run = vcpu_state(t.kvm_vm, VCPU_ID);
	return t;
}

static void guest_copy(void)
{
	memcpy(&mem2, &mem1, sizeof(mem2));
	GUEST_SYNC(0);
}

static void test_copy(void)
{
	struct test_default t = test_default_init(guest_copy);
	struct kvm_s390_mem_op ksmo;
	int i;

	for (i = 0; i < sizeof(mem1); i++)
		mem1[i] = i * i + i;

	/* Set the first array */
	ksmo.gaddr = addr_gva2gpa(t.kvm_vm, (uintptr_t)mem1);
	ksmo.flags = 0;
	ksmo.size = t.size;
	ksmo.op = KVM_S390_MEMOP_LOGICAL_WRITE;
	ksmo.buf = (uintptr_t)mem1;
	ksmo.ar = 0;
	vcpu_ioctl(t.kvm_vm, VCPU_ID, KVM_S390_MEM_OP, &ksmo);

	/* Let the guest code copy the first array to the second */
	vcpu_run(t.kvm_vm, VCPU_ID);
	TEST_ASSERT(t.run->exit_reason == KVM_EXIT_S390_SIEIC,
		    "Unexpected exit reason: %u (%s)\n",
		    t.run->exit_reason,
		    exit_reason_str(t.run->exit_reason));

	memset(mem2, 0xaa, sizeof(mem2));

	/* Get the second array */
	ksmo.gaddr = (uintptr_t)mem2;
	ksmo.flags = 0;
	ksmo.size = t.size;
	ksmo.op = KVM_S390_MEMOP_LOGICAL_READ;
	ksmo.buf = (uintptr_t)mem2;
	ksmo.ar = 0;
	vcpu_ioctl(t.kvm_vm, VCPU_ID, KVM_S390_MEM_OP, &ksmo);

	TEST_ASSERT(!memcmp(mem1, mem2, t.size),
		    "Memory contents do not match!");

	kvm_vm_free(t.kvm_vm);
}

static void guest_idle(void)
{
	for (;;)
		GUEST_SYNC(0);
}

static void test_errors(void)
{
	struct test_default t = test_default_init(guest_idle);
	struct kvm_s390_mem_op ksmo;
	int rv;

	/* Check error conditions - first bad size: */
	ksmo.gaddr = (uintptr_t)mem1;
	ksmo.flags = 0;
	ksmo.size = -1;
	ksmo.op = KVM_S390_MEMOP_LOGICAL_WRITE;
	ksmo.buf = (uintptr_t)mem1;
	ksmo.ar = 0;
	rv = _vcpu_ioctl(t.kvm_vm, VCPU_ID, KVM_S390_MEM_OP, &ksmo);
	TEST_ASSERT(rv == -1 && errno == E2BIG, "ioctl allows insane sizes");

	/* Zero size: */
	ksmo.gaddr = (uintptr_t)mem1;
	ksmo.flags = 0;
	ksmo.size = 0;
	ksmo.op = KVM_S390_MEMOP_LOGICAL_WRITE;
	ksmo.buf = (uintptr_t)mem1;
	ksmo.ar = 0;
	rv = _vcpu_ioctl(t.kvm_vm, VCPU_ID, KVM_S390_MEM_OP, &ksmo);
	TEST_ASSERT(rv == -1 && (errno == EINVAL || errno == ENOMEM),
		    "ioctl allows 0 as size");

	/* Bad flags: */
	ksmo.gaddr = (uintptr_t)mem1;
	ksmo.flags = -1;
	ksmo.size = t.size;
	ksmo.op = KVM_S390_MEMOP_LOGICAL_WRITE;
	ksmo.buf = (uintptr_t)mem1;
	ksmo.ar = 0;
	rv = _vcpu_ioctl(t.kvm_vm, VCPU_ID, KVM_S390_MEM_OP, &ksmo);
	TEST_ASSERT(rv == -1 && errno == EINVAL, "ioctl allows all flags");

	/* Bad operation: */
	ksmo.gaddr = (uintptr_t)mem1;
	ksmo.flags = 0;
	ksmo.size = t.size;
	ksmo.op = -1;
	ksmo.buf = (uintptr_t)mem1;
	ksmo.ar = 0;
	rv = _vcpu_ioctl(t.kvm_vm, VCPU_ID, KVM_S390_MEM_OP, &ksmo);
	TEST_ASSERT(rv == -1 && errno == EINVAL, "ioctl allows bad operations");

	/* Bad guest address: */
	ksmo.gaddr = ~0xfffUL;
	ksmo.flags = KVM_S390_MEMOP_F_CHECK_ONLY;
	ksmo.size = t.size;
	ksmo.op = KVM_S390_MEMOP_LOGICAL_WRITE;
	ksmo.buf = (uintptr_t)mem1;
	ksmo.ar = 0;
	rv = _vcpu_ioctl(t.kvm_vm, VCPU_ID, KVM_S390_MEM_OP, &ksmo);
	TEST_ASSERT(rv > 0, "ioctl does not report bad guest memory access");

	/* Bad host address: */
	ksmo.gaddr = (uintptr_t)mem1;
	ksmo.flags = 0;
	ksmo.size = t.size;
	ksmo.op = KVM_S390_MEMOP_LOGICAL_WRITE;
	ksmo.buf = 0;
	ksmo.ar = 0;
	rv = _vcpu_ioctl(t.kvm_vm, VCPU_ID, KVM_S390_MEM_OP, &ksmo);
	TEST_ASSERT(rv == -1 && errno == EFAULT,
		    "ioctl does not report bad host memory address");

	/* Bad access register: */
	t.run->psw_mask &= ~(3UL << (63 - 17));
	t.run->psw_mask |= 1UL << (63 - 17);  /* Enable AR mode */
	vcpu_run(t.kvm_vm, VCPU_ID);              /* To sync new state to SIE block */
	ksmo.gaddr = (uintptr_t)mem1;
	ksmo.flags = 0;
	ksmo.size = t.size;
	ksmo.op = KVM_S390_MEMOP_LOGICAL_WRITE;
	ksmo.buf = (uintptr_t)mem1;
	ksmo.ar = 17;
	rv = _vcpu_ioctl(t.kvm_vm, VCPU_ID, KVM_S390_MEM_OP, &ksmo);
	TEST_ASSERT(rv == -1 && errno == EINVAL, "ioctl allows ARs > 15");
	t.run->psw_mask &= ~(3UL << (63 - 17));   /* Disable AR mode */
	vcpu_run(t.kvm_vm, VCPU_ID);                  /* Run to sync new state */

	/* Check that the SIDA calls are rejected for non-protected guests */
	ksmo.gaddr = 0;
	ksmo.flags = 0;
	ksmo.size = 8;
	ksmo.op = KVM_S390_MEMOP_SIDA_READ;
	ksmo.buf = (uintptr_t)mem1;
	ksmo.sida_offset = 0x1c0;
	rv = _vcpu_ioctl(t.kvm_vm, VCPU_ID, KVM_S390_MEM_OP, &ksmo);
	TEST_ASSERT(rv == -1 && errno == EINVAL,
		    "ioctl does not reject SIDA_READ in non-protected mode");
	ksmo.op = KVM_S390_MEMOP_SIDA_WRITE;
	rv = _vcpu_ioctl(t.kvm_vm, VCPU_ID, KVM_S390_MEM_OP, &ksmo);
	TEST_ASSERT(rv == -1 && errno == EINVAL,
		    "ioctl does not reject SIDA_WRITE in non-protected mode");

	kvm_vm_free(t.kvm_vm);
}

int main(int argc, char *argv[])
{
	int memop_cap;

	setbuf(stdout, NULL);	/* Tell stdout not to buffer its content */

	memop_cap = kvm_check_cap(KVM_CAP_S390_MEM_OP);
	if (!memop_cap) {
		print_skip("CAP_S390_MEM_OP not supported");
		exit(KSFT_SKIP);
	}

	test_copy();
	test_errors();

	return 0;
}
