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

static void guest_code(void)
{
	int i;

	for (;;) {
		for (i = 0; i < sizeof(mem2); i++)
			mem2[i] = mem1[i];
		GUEST_SYNC(0);
	}
}

int main(int argc, char *argv[])
{
	struct kvm_vm *vm;
	struct kvm_run *run;
	struct kvm_s390_mem_op ksmo;
	int rv, i, maxsize;

	setbuf(stdout, NULL);	/* Tell stdout not to buffer its content */

	maxsize = kvm_check_cap(KVM_CAP_S390_MEM_OP);
	if (!maxsize) {
		fprintf(stderr, "CAP_S390_MEM_OP not supported -> skip test\n");
		exit(KSFT_SKIP);
	}
	if (maxsize > sizeof(mem1))
		maxsize = sizeof(mem1);

	/* Create VM */
	vm = vm_create_default(VCPU_ID, 0, guest_code);
	run = vcpu_state(vm, VCPU_ID);

	for (i = 0; i < sizeof(mem1); i++)
		mem1[i] = i * i + i;

	/* Set the first array */
	ksmo.gaddr = addr_gva2gpa(vm, (uintptr_t)mem1);
	ksmo.flags = 0;
	ksmo.size = maxsize;
	ksmo.op = KVM_S390_MEMOP_LOGICAL_WRITE;
	ksmo.buf = (uintptr_t)mem1;
	ksmo.ar = 0;
	vcpu_ioctl(vm, VCPU_ID, KVM_S390_MEM_OP, &ksmo);

	/* Let the guest code copy the first array to the second */
	vcpu_run(vm, VCPU_ID);
	TEST_ASSERT(run->exit_reason == KVM_EXIT_S390_SIEIC,
		    "Unexpected exit reason: %u (%s)\n",
		    run->exit_reason,
		    exit_reason_str(run->exit_reason));

	memset(mem2, 0xaa, sizeof(mem2));

	/* Get the second array */
	ksmo.gaddr = (uintptr_t)mem2;
	ksmo.flags = 0;
	ksmo.size = maxsize;
	ksmo.op = KVM_S390_MEMOP_LOGICAL_READ;
	ksmo.buf = (uintptr_t)mem2;
	ksmo.ar = 0;
	vcpu_ioctl(vm, VCPU_ID, KVM_S390_MEM_OP, &ksmo);

	TEST_ASSERT(!memcmp(mem1, mem2, maxsize),
		    "Memory contents do not match!");

	/* Check error conditions - first bad size: */
	ksmo.gaddr = (uintptr_t)mem1;
	ksmo.flags = 0;
	ksmo.size = -1;
	ksmo.op = KVM_S390_MEMOP_LOGICAL_WRITE;
	ksmo.buf = (uintptr_t)mem1;
	ksmo.ar = 0;
	rv = _vcpu_ioctl(vm, VCPU_ID, KVM_S390_MEM_OP, &ksmo);
	TEST_ASSERT(rv == -1 && errno == E2BIG, "ioctl allows insane sizes");

	/* Zero size: */
	ksmo.gaddr = (uintptr_t)mem1;
	ksmo.flags = 0;
	ksmo.size = 0;
	ksmo.op = KVM_S390_MEMOP_LOGICAL_WRITE;
	ksmo.buf = (uintptr_t)mem1;
	ksmo.ar = 0;
	rv = _vcpu_ioctl(vm, VCPU_ID, KVM_S390_MEM_OP, &ksmo);
	TEST_ASSERT(rv == -1 && (errno == EINVAL || errno == ENOMEM),
		    "ioctl allows 0 as size");

	/* Bad flags: */
	ksmo.gaddr = (uintptr_t)mem1;
	ksmo.flags = -1;
	ksmo.size = maxsize;
	ksmo.op = KVM_S390_MEMOP_LOGICAL_WRITE;
	ksmo.buf = (uintptr_t)mem1;
	ksmo.ar = 0;
	rv = _vcpu_ioctl(vm, VCPU_ID, KVM_S390_MEM_OP, &ksmo);
	TEST_ASSERT(rv == -1 && errno == EINVAL, "ioctl allows all flags");

	/* Bad operation: */
	ksmo.gaddr = (uintptr_t)mem1;
	ksmo.flags = 0;
	ksmo.size = maxsize;
	ksmo.op = -1;
	ksmo.buf = (uintptr_t)mem1;
	ksmo.ar = 0;
	rv = _vcpu_ioctl(vm, VCPU_ID, KVM_S390_MEM_OP, &ksmo);
	TEST_ASSERT(rv == -1 && errno == EINVAL, "ioctl allows bad operations");

	/* Bad guest address: */
	ksmo.gaddr = ~0xfffUL;
	ksmo.flags = KVM_S390_MEMOP_F_CHECK_ONLY;
	ksmo.size = maxsize;
	ksmo.op = KVM_S390_MEMOP_LOGICAL_WRITE;
	ksmo.buf = (uintptr_t)mem1;
	ksmo.ar = 0;
	rv = _vcpu_ioctl(vm, VCPU_ID, KVM_S390_MEM_OP, &ksmo);
	TEST_ASSERT(rv > 0, "ioctl does not report bad guest memory access");

	/* Bad host address: */
	ksmo.gaddr = (uintptr_t)mem1;
	ksmo.flags = 0;
	ksmo.size = maxsize;
	ksmo.op = KVM_S390_MEMOP_LOGICAL_WRITE;
	ksmo.buf = 0;
	ksmo.ar = 0;
	rv = _vcpu_ioctl(vm, VCPU_ID, KVM_S390_MEM_OP, &ksmo);
	TEST_ASSERT(rv == -1 && errno == EFAULT,
		    "ioctl does not report bad host memory address");

	/* Bad access register: */
	run->psw_mask &= ~(3UL << (63 - 17));
	run->psw_mask |= 1UL << (63 - 17);  /* Enable AR mode */
	vcpu_run(vm, VCPU_ID);              /* To sync new state to SIE block */
	ksmo.gaddr = (uintptr_t)mem1;
	ksmo.flags = 0;
	ksmo.size = maxsize;
	ksmo.op = KVM_S390_MEMOP_LOGICAL_WRITE;
	ksmo.buf = (uintptr_t)mem1;
	ksmo.ar = 17;
	rv = _vcpu_ioctl(vm, VCPU_ID, KVM_S390_MEM_OP, &ksmo);
	TEST_ASSERT(rv == -1 && errno == EINVAL, "ioctl allows ARs > 15");
	run->psw_mask &= ~(3UL << (63 - 17));   /* Disable AR mode */
	vcpu_run(vm, VCPU_ID);                  /* Run to sync new state */

	kvm_vm_free(vm);

	return 0;
}
