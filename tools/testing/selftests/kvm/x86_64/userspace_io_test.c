// SPDX-License-Identifier: GPL-2.0
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "test_util.h"

#include "kvm_util.h"
#include "processor.h"

static void guest_ins_port80(uint8_t *buffer, unsigned int count)
{
	unsigned long end;

	if (count == 2)
		end = (unsigned long)buffer + 1;
	else
		end = (unsigned long)buffer + 8192;

	asm volatile("cld; rep; insb" : "+D"(buffer), "+c"(count) : "d"(0x80) : "memory");
	GUEST_ASSERT_EQ(count, 0);
	GUEST_ASSERT_EQ((unsigned long)buffer, end);
}

static void guest_code(void)
{
	uint8_t buffer[8192];
	int i;

	/*
	 * Special case tests.  main() will adjust RCX 2 => 1 and 3 => 8192 to
	 * test that KVM doesn't explode when userspace modifies the "count" on
	 * a userspace I/O exit.  KVM isn't required to play nice with the I/O
	 * itself as KVM doesn't support manipulating the count, it just needs
	 * to not explode or overflow a buffer.
	 */
	guest_ins_port80(buffer, 2);
	guest_ins_port80(buffer, 3);

	/* Verify KVM fills the buffer correctly when not stuffing RCX. */
	memset(buffer, 0, sizeof(buffer));
	guest_ins_port80(buffer, 8192);
	for (i = 0; i < 8192; i++)
		__GUEST_ASSERT(buffer[i] == 0xaa,
			       "Expected '0xaa', got '0x%x' at buffer[%u]",
			       buffer[i], i);

	GUEST_DONE();
}

int main(int argc, char *argv[])
{
	struct kvm_vcpu *vcpu;
	struct kvm_regs regs;
	struct kvm_run *run;
	struct kvm_vm *vm;
	struct ucall uc;

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);
	run = vcpu->run;

	memset(&regs, 0, sizeof(regs));

	while (1) {
		vcpu_run(vcpu);
		TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);

		if (get_ucall(vcpu, &uc))
			break;

		TEST_ASSERT(run->io.port == 0x80,
			    "Expected I/O at port 0x80, got port 0x%x", run->io.port);

		/*
		 * Modify the rep string count in RCX: 2 => 1 and 3 => 8192.
		 * Note, this abuses KVM's batching of rep string I/O to avoid
		 * getting stuck in an infinite loop.  That behavior isn't in
		 * scope from a testing perspective as it's not ABI in any way,
		 * i.e. it really is abusing internal KVM knowledge.
		 */
		vcpu_regs_get(vcpu, &regs);
		if (regs.rcx == 2)
			regs.rcx = 1;
		if (regs.rcx == 3)
			regs.rcx = 8192;
		memset((void *)run + run->io.data_offset, 0xaa, 4096);
		vcpu_regs_set(vcpu, &regs);
	}

	switch (uc.cmd) {
	case UCALL_DONE:
		break;
	case UCALL_ABORT:
		REPORT_GUEST_ASSERT(uc);
	default:
		TEST_FAIL("Unknown ucall %lu", uc.cmd);
	}

	kvm_vm_free(vm);
	return 0;
}
