// SPDX-License-Identifier: GPL-2.0-only
#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"

#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include "kselftest.h"

static void guest_ud_handler(struct ex_regs *regs)
{
	/* Loop on the ud2 until guest state is made invalid. */
}

static void guest_code(void)
{
	asm volatile("ud2");
}

static void __run_vcpu_with_invalid_state(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;

	vcpu_run(vcpu);

	TEST_ASSERT(run->exit_reason == KVM_EXIT_INTERNAL_ERROR,
		    "Expected KVM_EXIT_INTERNAL_ERROR, got %d (%s)\n",
		    run->exit_reason, exit_reason_str(run->exit_reason));
	TEST_ASSERT(run->emulation_failure.suberror == KVM_INTERNAL_ERROR_EMULATION,
		    "Expected emulation failure, got %d\n",
		    run->emulation_failure.suberror);
}

static void run_vcpu_with_invalid_state(struct kvm_vcpu *vcpu)
{
	/*
	 * Always run twice to verify KVM handles the case where _KVM_ queues
	 * an exception with invalid state and then exits to userspace, i.e.
	 * that KVM doesn't explode if userspace ignores the initial error.
	 */
	__run_vcpu_with_invalid_state(vcpu);
	__run_vcpu_with_invalid_state(vcpu);
}

static void set_timer(void)
{
	struct itimerval timer;

	timer.it_value.tv_sec  = 0;
	timer.it_value.tv_usec = 200;
	timer.it_interval = timer.it_value;
	ASSERT_EQ(setitimer(ITIMER_REAL, &timer, NULL), 0);
}

static void set_or_clear_invalid_guest_state(struct kvm_vcpu *vcpu, bool set)
{
	static struct kvm_sregs sregs;

	if (!sregs.cr0)
		vcpu_sregs_get(vcpu, &sregs);
	sregs.tr.unusable = !!set;
	vcpu_sregs_set(vcpu, &sregs);
}

static void set_invalid_guest_state(struct kvm_vcpu *vcpu)
{
	set_or_clear_invalid_guest_state(vcpu, true);
}

static void clear_invalid_guest_state(struct kvm_vcpu *vcpu)
{
	set_or_clear_invalid_guest_state(vcpu, false);
}

static struct kvm_vcpu *get_set_sigalrm_vcpu(struct kvm_vcpu *__vcpu)
{
	static struct kvm_vcpu *vcpu = NULL;

	if (__vcpu)
		vcpu = __vcpu;
	return vcpu;
}

static void sigalrm_handler(int sig)
{
	struct kvm_vcpu *vcpu = get_set_sigalrm_vcpu(NULL);
	struct kvm_vcpu_events events;

	TEST_ASSERT(sig == SIGALRM, "Unexpected signal = %d", sig);

	vcpu_events_get(vcpu, &events);

	/*
	 * If an exception is pending, attempt KVM_RUN with invalid guest,
	 * otherwise rearm the timer and keep doing so until the timer fires
	 * between KVM queueing an exception and re-entering the guest.
	 */
	if (events.exception.pending) {
		set_invalid_guest_state(vcpu);
		run_vcpu_with_invalid_state(vcpu);
	} else {
		set_timer();
	}
}

int main(int argc, char *argv[])
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;

	TEST_REQUIRE(is_intel_cpu());
	TEST_REQUIRE(!vm_is_unrestricted_guest(NULL));

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);
	get_set_sigalrm_vcpu(vcpu);

	vm_init_descriptor_tables(vm);
	vcpu_init_descriptor_tables(vcpu);

	vm_install_exception_handler(vm, UD_VECTOR, guest_ud_handler);

	/*
	 * Stuff invalid guest state for L2 by making TR unusuable.  The next
	 * KVM_RUN should induce a TRIPLE_FAULT in L2 as KVM doesn't support
	 * emulating invalid guest state for L2.
	 */
	set_invalid_guest_state(vcpu);
	run_vcpu_with_invalid_state(vcpu);

	/*
	 * Verify KVM also handles the case where userspace gains control while
	 * an exception is pending and stuffs invalid state.  Run with valid
	 * guest state and a timer firing every 200us, and attempt to enter the
	 * guest with invalid state when the handler interrupts KVM with an
	 * exception pending.
	 */
	clear_invalid_guest_state(vcpu);
	TEST_ASSERT(signal(SIGALRM, sigalrm_handler) != SIG_ERR,
		    "Failed to register SIGALRM handler, errno = %d (%s)",
		    errno, strerror(errno));

	set_timer();
	run_vcpu_with_invalid_state(vcpu);
}
