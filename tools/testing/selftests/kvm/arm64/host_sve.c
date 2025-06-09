// SPDX-License-Identifier: GPL-2.0-only

/*
 * Host SVE: Check FPSIMD/SVE/SME save/restore over KVM_RUN ioctls.
 *
 * Copyright 2025 Arm, Ltd
 */

#include <errno.h>
#include <signal.h>
#include <sys/auxv.h>
#include <asm/kvm.h>
#include <kvm_util.h>

#include "ucall_common.h"

static void guest_code(void)
{
	for (int i = 0; i < 10; i++) {
		GUEST_UCALL_NONE();
	}

	GUEST_DONE();
}

void handle_sigill(int sig, siginfo_t *info, void *ctx)
{
	ucontext_t *uctx = ctx;

	printf("  < host signal %d >\n", sig);

	/*
	 * Skip the UDF
	 */
	uctx->uc_mcontext.pc += 4;
}

void register_sigill_handler(void)
{
	struct sigaction sa = {
		.sa_sigaction = handle_sigill,
		.sa_flags = SA_SIGINFO,
	};
	sigaction(SIGILL, &sa, NULL);
}

static void do_sve_roundtrip(void)
{
	unsigned long before, after;

	/*
	 * Set all bits in a predicate register, force a save/restore via a
	 * SIGILL (which handle_sigill() will recover from), then report
	 * whether the value has changed.
	 */
	asm volatile(
	"	.arch_extension sve\n"
	"	ptrue	p0.B\n"
	"	cntp	%[before], p0, p0.B\n"
	"	udf #0\n"
	"	cntp	%[after], p0, p0.B\n"
	: [before] "=r" (before),
	  [after] "=r" (after)
	:
	: "p0"
	);

	if (before != after) {
		TEST_FAIL("Signal roundtrip discarded predicate bits (%ld => %ld)\n",
			  before, after);
	} else {
		printf("Signal roundtrip preserved predicate bits (%ld => %ld)\n",
		       before, after);
	}
}

static void test_run(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct ucall uc;
	bool guest_done = false;

	register_sigill_handler();

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);

	do_sve_roundtrip();

	while (!guest_done) {

		printf("Running VCPU...\n");
		vcpu_run(vcpu);

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_NONE:
			do_sve_roundtrip();
			do_sve_roundtrip();
			break;
		case UCALL_DONE:
			guest_done = true;
			break;
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
			break;
		default:
			TEST_FAIL("Unexpected guest exit");
		}
	}

	kvm_vm_free(vm);
}

int main(void)
{
	/*
	 * This is testing the host environment, we don't care about
	 * guest SVE support.
	 */
	if (!(getauxval(AT_HWCAP) & HWCAP_SVE)) {
		printf("SVE not supported\n");
		return KSFT_SKIP;
	}

	test_run();
	return 0;
}
