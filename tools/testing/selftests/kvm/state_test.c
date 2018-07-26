/*
 * KVM_GET/SET_* tests
 *
 * Copyright (C) 2018, Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 *
 * Tests for vCPU state save/restore, including nested guest state.
 */
#define _GNU_SOURCE /* for program_invocation_short_name */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "test_util.h"

#include "kvm_util.h"
#include "x86.h"

#define VCPU_ID		5
#define PORT_SYNC	0x1000
#define PORT_ABORT	0x1001
#define PORT_DONE	0x1002

static inline void __exit_to_l0(uint16_t port, uint64_t arg0, uint64_t arg1)
{
	__asm__ __volatile__("in %[port], %%al"
			     :
			     : [port]"d"(port), "D"(arg0), "S"(arg1)
			     : "rax");
}

#define exit_to_l0(_port, _arg0, _arg1) \
	__exit_to_l0(_port, (uint64_t) (_arg0), (uint64_t) (_arg1))

#define GUEST_ASSERT(_condition) do { \
	if (!(_condition)) \
		exit_to_l0(PORT_ABORT, "Failed guest assert: " #_condition, __LINE__);\
} while (0)

#define GUEST_SYNC(stage) \
	exit_to_l0(PORT_SYNC, "hello", stage);

static bool have_nested_state;

void guest_code(void)
{
	GUEST_SYNC(1);
	GUEST_SYNC(2);

	exit_to_l0(PORT_DONE, 0, 0);
}

int main(int argc, char *argv[])
{
	struct kvm_regs regs1, regs2;
	struct kvm_vm *vm;
	struct kvm_run *run;
	struct kvm_x86_state *state;
	int stage;

	struct kvm_cpuid_entry2 *entry = kvm_get_supported_cpuid_entry(1);

	/* Create VM */
	vm = vm_create_default(VCPU_ID, guest_code);
	vcpu_set_cpuid(vm, VCPU_ID, kvm_get_supported_cpuid());
	run = vcpu_state(vm, VCPU_ID);

	vcpu_regs_get(vm, VCPU_ID, &regs1);
	for (stage = 1;; stage++) {
		_vcpu_run(vm, VCPU_ID);
		TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
			    "Unexpected exit reason: %u (%s),\n",
			    run->exit_reason,
			    exit_reason_str(run->exit_reason));

		memset(&regs1, 0, sizeof(regs1));
		vcpu_regs_get(vm, VCPU_ID, &regs1);
		switch (run->io.port) {
		case PORT_ABORT:
			TEST_ASSERT(false, "%s at %s:%d", (const char *) regs1.rdi,
				    __FILE__, regs1.rsi);
			/* NOT REACHED */
		case PORT_SYNC:
			break;
		case PORT_DONE:
			goto done;
		default:
			TEST_ASSERT(false, "Unknown port 0x%x.", run->io.port);
		}

		/* PORT_SYNC is handled here.  */
		TEST_ASSERT(!strcmp((const char *)regs1.rdi, "hello") &&
			    regs1.rsi == stage, "Unexpected register values vmexit #%lx, got %lx",
			    stage, (ulong) regs1.rsi);

		state = vcpu_save_state(vm, VCPU_ID);
		kvm_vm_release(vm);

		/* Restore state in a new VM.  */
		kvm_vm_restart(vm, O_RDWR);
		vm_vcpu_add(vm, VCPU_ID, 0, 0);
		vcpu_set_cpuid(vm, VCPU_ID, kvm_get_supported_cpuid());
		vcpu_load_state(vm, VCPU_ID, state);
		run = vcpu_state(vm, VCPU_ID);
		free(state);

		memset(&regs2, 0, sizeof(regs2));
		vcpu_regs_get(vm, VCPU_ID, &regs2);
		TEST_ASSERT(!memcmp(&regs1, &regs2, sizeof(regs2)),
			    "Unexpected register values after vcpu_load_state; rdi: %lx rsi: %lx",
			    (ulong) regs2.rdi, (ulong) regs2.rsi);
	}

done:
	kvm_vm_free(vm);
}
