// SPDX-License-Identifier: GPL-2.0-only
/*
 * Test for KVM_S390_IRQ - s390x IRQ injection API
 *
 * Verifies that VM-level interrupt types are correctly rejected when
 * injected via the vCPU file descriptor, and that invalid arguments
 * to vCPU-level interrupts are also rejected.
 *
 * Copyright IBM Corp. 2026
 * Author: Gabriella Seifert <gseifert@linux.ibm.com>
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include "kvm_util.h"
#include "test_util.h"
#include <linux/kvm.h>

static void test_vm_irq_on_vcpu_fails(struct kvm_vcpu *vcpu, __u64 type)
{
	struct kvm_s390_irq irq = { .type = type };
	int rc;

	rc = __vcpu_ioctl(vcpu, KVM_S390_IRQ, &irq);
	ksft_test_result(rc == -1 && errno == EINVAL,
			 "VM on VCPU IRQ type 0x%llx: expected %d, got %d\n",
			 type, EINVAL, errno);
}

static void test_vcpu_irq_bad_param_fails(struct kvm_vcpu *vcpu,
					  struct kvm_s390_irq *irq)
{
	int rc;

	rc = __vcpu_ioctl(vcpu, KVM_S390_IRQ, irq);
	ksft_test_result(rc == -1 && errno == EINVAL,
			 "VCPU IRQ bad param type 0x%llx: expected %d got %d\n",
			 irq->type, EINVAL, errno);
}

int main(void)
{
	struct kvm_s390_irq irq;
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;

	ksft_print_header();
	ksft_set_plan(6);
	vm = vm_create_with_one_vcpu(&vcpu, NULL);

	test_vm_irq_on_vcpu_fails(vcpu, KVM_S390_INT_VIRTIO);
	test_vm_irq_on_vcpu_fails(vcpu, KVM_S390_INT_SERVICE);
	test_vm_irq_on_vcpu_fails(vcpu, KVM_S390_INT_IO_MIN);

	memset(&irq, 0, sizeof(irq));
	irq.type = KVM_S390_INT_EMERGENCY;
	irq.u.emerg.code = 0xffff;
	test_vcpu_irq_bad_param_fails(vcpu, &irq);

	memset(&irq, 0, sizeof(irq));
	irq.type = KVM_S390_INT_EXTERNAL_CALL;
	irq.u.extcall.code = 0xffff;
	test_vcpu_irq_bad_param_fails(vcpu, &irq);

	memset(&irq, 0, sizeof(irq));
	irq.type = 0xdeadbeef;
	test_vcpu_irq_bad_param_fails(vcpu, &irq);

	kvm_vm_free(vm);
	ksft_finished();
}
