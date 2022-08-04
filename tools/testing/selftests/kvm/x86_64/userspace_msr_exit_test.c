// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020, Google LLC.
 *
 * Tests for exiting into userspace on registered MSRs
 */

#define _GNU_SOURCE /* for program_invocation_short_name */
#include <sys/ioctl.h>

#include "test_util.h"
#include "kvm_util.h"
#include "vmx.h"

/* Forced emulation prefix, used to invoke the emulator unconditionally. */
#define KVM_FEP "ud2; .byte 'k', 'v', 'm';"
#define KVM_FEP_LENGTH 5
static int fep_available = 1;

#define VCPU_ID	      1
#define MSR_NON_EXISTENT 0x474f4f00

static u64 deny_bits = 0;
struct kvm_msr_filter filter_allow = {
	.flags = KVM_MSR_FILTER_DEFAULT_ALLOW,
	.ranges = {
		{
			.flags = KVM_MSR_FILTER_READ |
				 KVM_MSR_FILTER_WRITE,
			.nmsrs = 1,
			/* Test an MSR the kernel knows about. */
			.base = MSR_IA32_XSS,
			.bitmap = (uint8_t*)&deny_bits,
		}, {
			.flags = KVM_MSR_FILTER_READ |
				 KVM_MSR_FILTER_WRITE,
			.nmsrs = 1,
			/* Test an MSR the kernel doesn't know about. */
			.base = MSR_IA32_FLUSH_CMD,
			.bitmap = (uint8_t*)&deny_bits,
		}, {
			.flags = KVM_MSR_FILTER_READ |
				 KVM_MSR_FILTER_WRITE,
			.nmsrs = 1,
			/* Test a fabricated MSR that no one knows about. */
			.base = MSR_NON_EXISTENT,
			.bitmap = (uint8_t*)&deny_bits,
		},
	},
};

struct kvm_msr_filter filter_fs = {
	.flags = KVM_MSR_FILTER_DEFAULT_ALLOW,
	.ranges = {
		{
			.flags = KVM_MSR_FILTER_READ,
			.nmsrs = 1,
			.base = MSR_FS_BASE,
			.bitmap = (uint8_t*)&deny_bits,
		},
	},
};

struct kvm_msr_filter filter_gs = {
	.flags = KVM_MSR_FILTER_DEFAULT_ALLOW,
	.ranges = {
		{
			.flags = KVM_MSR_FILTER_READ,
			.nmsrs = 1,
			.base = MSR_GS_BASE,
			.bitmap = (uint8_t*)&deny_bits,
		},
	},
};

static uint64_t msr_non_existent_data;
static int guest_exception_count;
static u32 msr_reads, msr_writes;

static u8 bitmap_00000000[KVM_MSR_FILTER_MAX_BITMAP_SIZE];
static u8 bitmap_00000000_write[KVM_MSR_FILTER_MAX_BITMAP_SIZE];
static u8 bitmap_40000000[KVM_MSR_FILTER_MAX_BITMAP_SIZE];
static u8 bitmap_c0000000[KVM_MSR_FILTER_MAX_BITMAP_SIZE];
static u8 bitmap_c0000000_read[KVM_MSR_FILTER_MAX_BITMAP_SIZE];
static u8 bitmap_deadbeef[1] = { 0x1 };

static void deny_msr(uint8_t *bitmap, u32 msr)
{
	u32 idx = msr & (KVM_MSR_FILTER_MAX_BITMAP_SIZE - 1);

	bitmap[idx / 8] &= ~(1 << (idx % 8));
}

static void prepare_bitmaps(void)
{
	memset(bitmap_00000000, 0xff, sizeof(bitmap_00000000));
	memset(bitmap_00000000_write, 0xff, sizeof(bitmap_00000000_write));
	memset(bitmap_40000000, 0xff, sizeof(bitmap_40000000));
	memset(bitmap_c0000000, 0xff, sizeof(bitmap_c0000000));
	memset(bitmap_c0000000_read, 0xff, sizeof(bitmap_c0000000_read));

	deny_msr(bitmap_00000000_write, MSR_IA32_POWER_CTL);
	deny_msr(bitmap_c0000000_read, MSR_SYSCALL_MASK);
	deny_msr(bitmap_c0000000_read, MSR_GS_BASE);
}

struct kvm_msr_filter filter_deny = {
	.flags = KVM_MSR_FILTER_DEFAULT_DENY,
	.ranges = {
		{
			.flags = KVM_MSR_FILTER_READ,
			.base = 0x00000000,
			.nmsrs = KVM_MSR_FILTER_MAX_BITMAP_SIZE * BITS_PER_BYTE,
			.bitmap = bitmap_00000000,
		}, {
			.flags = KVM_MSR_FILTER_WRITE,
			.base = 0x00000000,
			.nmsrs = KVM_MSR_FILTER_MAX_BITMAP_SIZE * BITS_PER_BYTE,
			.bitmap = bitmap_00000000_write,
		}, {
			.flags = KVM_MSR_FILTER_READ | KVM_MSR_FILTER_WRITE,
			.base = 0x40000000,
			.nmsrs = KVM_MSR_FILTER_MAX_BITMAP_SIZE * BITS_PER_BYTE,
			.bitmap = bitmap_40000000,
		}, {
			.flags = KVM_MSR_FILTER_READ,
			.base = 0xc0000000,
			.nmsrs = KVM_MSR_FILTER_MAX_BITMAP_SIZE * BITS_PER_BYTE,
			.bitmap = bitmap_c0000000_read,
		}, {
			.flags = KVM_MSR_FILTER_WRITE,
			.base = 0xc0000000,
			.nmsrs = KVM_MSR_FILTER_MAX_BITMAP_SIZE * BITS_PER_BYTE,
			.bitmap = bitmap_c0000000,
		}, {
			.flags = KVM_MSR_FILTER_WRITE | KVM_MSR_FILTER_READ,
			.base = 0xdeadbeef,
			.nmsrs = 1,
			.bitmap = bitmap_deadbeef,
		},
	},
};

struct kvm_msr_filter no_filter_deny = {
	.flags = KVM_MSR_FILTER_DEFAULT_ALLOW,
};

/*
 * Note: Force test_rdmsr() to not be inlined to prevent the labels,
 * rdmsr_start and rdmsr_end, from being defined multiple times.
 */
static noinline uint64_t test_rdmsr(uint32_t msr)
{
	uint32_t a, d;

	guest_exception_count = 0;

	__asm__ __volatile__("rdmsr_start: rdmsr; rdmsr_end:" :
			"=a"(a), "=d"(d) : "c"(msr) : "memory");

	return a | ((uint64_t) d << 32);
}

/*
 * Note: Force test_wrmsr() to not be inlined to prevent the labels,
 * wrmsr_start and wrmsr_end, from being defined multiple times.
 */
static noinline void test_wrmsr(uint32_t msr, uint64_t value)
{
	uint32_t a = value;
	uint32_t d = value >> 32;

	guest_exception_count = 0;

	__asm__ __volatile__("wrmsr_start: wrmsr; wrmsr_end:" ::
			"a"(a), "d"(d), "c"(msr) : "memory");
}

extern char rdmsr_start, rdmsr_end;
extern char wrmsr_start, wrmsr_end;

/*
 * Note: Force test_em_rdmsr() to not be inlined to prevent the labels,
 * rdmsr_start and rdmsr_end, from being defined multiple times.
 */
static noinline uint64_t test_em_rdmsr(uint32_t msr)
{
	uint32_t a, d;

	guest_exception_count = 0;

	__asm__ __volatile__(KVM_FEP "em_rdmsr_start: rdmsr; em_rdmsr_end:" :
			"=a"(a), "=d"(d) : "c"(msr) : "memory");

	return a | ((uint64_t) d << 32);
}

/*
 * Note: Force test_em_wrmsr() to not be inlined to prevent the labels,
 * wrmsr_start and wrmsr_end, from being defined multiple times.
 */
static noinline void test_em_wrmsr(uint32_t msr, uint64_t value)
{
	uint32_t a = value;
	uint32_t d = value >> 32;

	guest_exception_count = 0;

	__asm__ __volatile__(KVM_FEP "em_wrmsr_start: wrmsr; em_wrmsr_end:" ::
			"a"(a), "d"(d), "c"(msr) : "memory");
}

extern char em_rdmsr_start, em_rdmsr_end;
extern char em_wrmsr_start, em_wrmsr_end;

static void guest_code_filter_allow(void)
{
	uint64_t data;

	/*
	 * Test userspace intercepting rdmsr / wrmsr for MSR_IA32_XSS.
	 *
	 * A GP is thrown if anything other than 0 is written to
	 * MSR_IA32_XSS.
	 */
	data = test_rdmsr(MSR_IA32_XSS);
	GUEST_ASSERT(data == 0);
	GUEST_ASSERT(guest_exception_count == 0);

	test_wrmsr(MSR_IA32_XSS, 0);
	GUEST_ASSERT(guest_exception_count == 0);

	test_wrmsr(MSR_IA32_XSS, 1);
	GUEST_ASSERT(guest_exception_count == 1);

	/*
	 * Test userspace intercepting rdmsr / wrmsr for MSR_IA32_FLUSH_CMD.
	 *
	 * A GP is thrown if MSR_IA32_FLUSH_CMD is read
	 * from or if a value other than 1 is written to it.
	 */
	test_rdmsr(MSR_IA32_FLUSH_CMD);
	GUEST_ASSERT(guest_exception_count == 1);

	test_wrmsr(MSR_IA32_FLUSH_CMD, 0);
	GUEST_ASSERT(guest_exception_count == 1);

	test_wrmsr(MSR_IA32_FLUSH_CMD, 1);
	GUEST_ASSERT(guest_exception_count == 0);

	/*
	 * Test userspace intercepting rdmsr / wrmsr for MSR_NON_EXISTENT.
	 *
	 * Test that a fabricated MSR can pass through the kernel
	 * and be handled in userspace.
	 */
	test_wrmsr(MSR_NON_EXISTENT, 2);
	GUEST_ASSERT(guest_exception_count == 0);

	data = test_rdmsr(MSR_NON_EXISTENT);
	GUEST_ASSERT(data == 2);
	GUEST_ASSERT(guest_exception_count == 0);

	/*
	 * Test to see if the instruction emulator is available (ie: the module
	 * parameter 'kvm.force_emulation_prefix=1' is set).  This instruction
	 * will #UD if it isn't available.
	 */
	__asm__ __volatile__(KVM_FEP "nop");

	if (fep_available) {
		/* Let userspace know we aren't done. */
		GUEST_SYNC(0);

		/*
		 * Now run the same tests with the instruction emulator.
		 */
		data = test_em_rdmsr(MSR_IA32_XSS);
		GUEST_ASSERT(data == 0);
		GUEST_ASSERT(guest_exception_count == 0);
		test_em_wrmsr(MSR_IA32_XSS, 0);
		GUEST_ASSERT(guest_exception_count == 0);
		test_em_wrmsr(MSR_IA32_XSS, 1);
		GUEST_ASSERT(guest_exception_count == 1);

		test_em_rdmsr(MSR_IA32_FLUSH_CMD);
		GUEST_ASSERT(guest_exception_count == 1);
		test_em_wrmsr(MSR_IA32_FLUSH_CMD, 0);
		GUEST_ASSERT(guest_exception_count == 1);
		test_em_wrmsr(MSR_IA32_FLUSH_CMD, 1);
		GUEST_ASSERT(guest_exception_count == 0);

		test_em_wrmsr(MSR_NON_EXISTENT, 2);
		GUEST_ASSERT(guest_exception_count == 0);
		data = test_em_rdmsr(MSR_NON_EXISTENT);
		GUEST_ASSERT(data == 2);
		GUEST_ASSERT(guest_exception_count == 0);
	}

	GUEST_DONE();
}

static void guest_msr_calls(bool trapped)
{
	/* This goes into the in-kernel emulation */
	wrmsr(MSR_SYSCALL_MASK, 0);

	if (trapped) {
		/* This goes into user space emulation */
		GUEST_ASSERT(rdmsr(MSR_SYSCALL_MASK) == MSR_SYSCALL_MASK);
		GUEST_ASSERT(rdmsr(MSR_GS_BASE) == MSR_GS_BASE);
	} else {
		GUEST_ASSERT(rdmsr(MSR_SYSCALL_MASK) != MSR_SYSCALL_MASK);
		GUEST_ASSERT(rdmsr(MSR_GS_BASE) != MSR_GS_BASE);
	}

	/* If trapped == true, this goes into user space emulation */
	wrmsr(MSR_IA32_POWER_CTL, 0x1234);

	/* This goes into the in-kernel emulation */
	rdmsr(MSR_IA32_POWER_CTL);

	/* Invalid MSR, should always be handled by user space exit */
	GUEST_ASSERT(rdmsr(0xdeadbeef) == 0xdeadbeef);
	wrmsr(0xdeadbeef, 0x1234);
}

static void guest_code_filter_deny(void)
{
	guest_msr_calls(true);

	/*
	 * Disable msr filtering, so that the kernel
	 * handles everything in the next round
	 */
	GUEST_SYNC(0);

	guest_msr_calls(false);

	GUEST_DONE();
}

static void guest_code_permission_bitmap(void)
{
	uint64_t data;

	data = test_rdmsr(MSR_FS_BASE);
	GUEST_ASSERT(data == MSR_FS_BASE);
	data = test_rdmsr(MSR_GS_BASE);
	GUEST_ASSERT(data != MSR_GS_BASE);

	/* Let userspace know to switch the filter */
	GUEST_SYNC(0);

	data = test_rdmsr(MSR_FS_BASE);
	GUEST_ASSERT(data != MSR_FS_BASE);
	data = test_rdmsr(MSR_GS_BASE);
	GUEST_ASSERT(data == MSR_GS_BASE);

	GUEST_DONE();
}

static void __guest_gp_handler(struct ex_regs *regs,
			       char *r_start, char *r_end,
			       char *w_start, char *w_end)
{
	if (regs->rip == (uintptr_t)r_start) {
		regs->rip = (uintptr_t)r_end;
		regs->rax = 0;
		regs->rdx = 0;
	} else if (regs->rip == (uintptr_t)w_start) {
		regs->rip = (uintptr_t)w_end;
	} else {
		GUEST_ASSERT(!"RIP is at an unknown location!");
	}

	++guest_exception_count;
}

static void guest_gp_handler(struct ex_regs *regs)
{
	__guest_gp_handler(regs, &rdmsr_start, &rdmsr_end,
			   &wrmsr_start, &wrmsr_end);
}

static void guest_fep_gp_handler(struct ex_regs *regs)
{
	__guest_gp_handler(regs, &em_rdmsr_start, &em_rdmsr_end,
			   &em_wrmsr_start, &em_wrmsr_end);
}

static void guest_ud_handler(struct ex_regs *regs)
{
	fep_available = 0;
	regs->rip += KVM_FEP_LENGTH;
}

static void run_guest(struct kvm_vm *vm)
{
	int rc;

	rc = _vcpu_run(vm, VCPU_ID);
	TEST_ASSERT(rc == 0, "vcpu_run failed: %d\n", rc);
}

static void check_for_guest_assert(struct kvm_vm *vm)
{
	struct kvm_run *run = vcpu_state(vm, VCPU_ID);
	struct ucall uc;

	if (run->exit_reason == KVM_EXIT_IO &&
		get_ucall(vm, VCPU_ID, &uc) == UCALL_ABORT) {
			TEST_FAIL("%s at %s:%ld", (const char *)uc.args[0],
				__FILE__, uc.args[1]);
	}
}

static void process_rdmsr(struct kvm_vm *vm, uint32_t msr_index)
{
	struct kvm_run *run = vcpu_state(vm, VCPU_ID);

	check_for_guest_assert(vm);

	TEST_ASSERT(run->exit_reason == KVM_EXIT_X86_RDMSR,
		    "Unexpected exit reason: %u (%s),\n",
		    run->exit_reason,
		    exit_reason_str(run->exit_reason));
	TEST_ASSERT(run->msr.index == msr_index,
			"Unexpected msr (0x%04x), expected 0x%04x",
			run->msr.index, msr_index);

	switch (run->msr.index) {
	case MSR_IA32_XSS:
		run->msr.data = 0;
		break;
	case MSR_IA32_FLUSH_CMD:
		run->msr.error = 1;
		break;
	case MSR_NON_EXISTENT:
		run->msr.data = msr_non_existent_data;
		break;
	case MSR_FS_BASE:
		run->msr.data = MSR_FS_BASE;
		break;
	case MSR_GS_BASE:
		run->msr.data = MSR_GS_BASE;
		break;
	default:
		TEST_ASSERT(false, "Unexpected MSR: 0x%04x", run->msr.index);
	}
}

static void process_wrmsr(struct kvm_vm *vm, uint32_t msr_index)
{
	struct kvm_run *run = vcpu_state(vm, VCPU_ID);

	check_for_guest_assert(vm);

	TEST_ASSERT(run->exit_reason == KVM_EXIT_X86_WRMSR,
		    "Unexpected exit reason: %u (%s),\n",
		    run->exit_reason,
		    exit_reason_str(run->exit_reason));
	TEST_ASSERT(run->msr.index == msr_index,
			"Unexpected msr (0x%04x), expected 0x%04x",
			run->msr.index, msr_index);

	switch (run->msr.index) {
	case MSR_IA32_XSS:
		if (run->msr.data != 0)
			run->msr.error = 1;
		break;
	case MSR_IA32_FLUSH_CMD:
		if (run->msr.data != 1)
			run->msr.error = 1;
		break;
	case MSR_NON_EXISTENT:
		msr_non_existent_data = run->msr.data;
		break;
	default:
		TEST_ASSERT(false, "Unexpected MSR: 0x%04x", run->msr.index);
	}
}

static void process_ucall_done(struct kvm_vm *vm)
{
	struct kvm_run *run = vcpu_state(vm, VCPU_ID);
	struct ucall uc;

	check_for_guest_assert(vm);

	TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
		    "Unexpected exit reason: %u (%s)",
		    run->exit_reason,
		    exit_reason_str(run->exit_reason));

	TEST_ASSERT(get_ucall(vm, VCPU_ID, &uc) == UCALL_DONE,
		    "Unexpected ucall command: %lu, expected UCALL_DONE (%d)",
		    uc.cmd, UCALL_DONE);
}

static uint64_t process_ucall(struct kvm_vm *vm)
{
	struct kvm_run *run = vcpu_state(vm, VCPU_ID);
	struct ucall uc = {};

	check_for_guest_assert(vm);

	TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
		    "Unexpected exit reason: %u (%s)",
		    run->exit_reason,
		    exit_reason_str(run->exit_reason));

	switch (get_ucall(vm, VCPU_ID, &uc)) {
	case UCALL_SYNC:
		break;
	case UCALL_ABORT:
		check_for_guest_assert(vm);
		break;
	case UCALL_DONE:
		process_ucall_done(vm);
		break;
	default:
		TEST_ASSERT(false, "Unexpected ucall");
	}

	return uc.cmd;
}

static void run_guest_then_process_rdmsr(struct kvm_vm *vm, uint32_t msr_index)
{
	run_guest(vm);
	process_rdmsr(vm, msr_index);
}

static void run_guest_then_process_wrmsr(struct kvm_vm *vm, uint32_t msr_index)
{
	run_guest(vm);
	process_wrmsr(vm, msr_index);
}

static uint64_t run_guest_then_process_ucall(struct kvm_vm *vm)
{
	run_guest(vm);
	return process_ucall(vm);
}

static void run_guest_then_process_ucall_done(struct kvm_vm *vm)
{
	run_guest(vm);
	process_ucall_done(vm);
}

static void test_msr_filter_allow(void) {
	struct kvm_enable_cap cap = {
		.cap = KVM_CAP_X86_USER_SPACE_MSR,
		.args[0] = KVM_MSR_EXIT_REASON_FILTER,
	};
	struct kvm_vm *vm;
	int rc;

	/* Create VM */
	vm = vm_create_default(VCPU_ID, 0, guest_code_filter_allow);
	vcpu_set_cpuid(vm, VCPU_ID, kvm_get_supported_cpuid());

	rc = kvm_check_cap(KVM_CAP_X86_USER_SPACE_MSR);
	TEST_ASSERT(rc, "KVM_CAP_X86_USER_SPACE_MSR is available");
	vm_enable_cap(vm, &cap);

	rc = kvm_check_cap(KVM_CAP_X86_MSR_FILTER);
	TEST_ASSERT(rc, "KVM_CAP_X86_MSR_FILTER is available");

	vm_ioctl(vm, KVM_X86_SET_MSR_FILTER, &filter_allow);

	vm_init_descriptor_tables(vm);
	vcpu_init_descriptor_tables(vm, VCPU_ID);

	vm_handle_exception(vm, GP_VECTOR, guest_gp_handler);

	/* Process guest code userspace exits. */
	run_guest_then_process_rdmsr(vm, MSR_IA32_XSS);
	run_guest_then_process_wrmsr(vm, MSR_IA32_XSS);
	run_guest_then_process_wrmsr(vm, MSR_IA32_XSS);

	run_guest_then_process_rdmsr(vm, MSR_IA32_FLUSH_CMD);
	run_guest_then_process_wrmsr(vm, MSR_IA32_FLUSH_CMD);
	run_guest_then_process_wrmsr(vm, MSR_IA32_FLUSH_CMD);

	run_guest_then_process_wrmsr(vm, MSR_NON_EXISTENT);
	run_guest_then_process_rdmsr(vm, MSR_NON_EXISTENT);

	vm_handle_exception(vm, UD_VECTOR, guest_ud_handler);
	run_guest(vm);
	vm_handle_exception(vm, UD_VECTOR, NULL);

	if (process_ucall(vm) != UCALL_DONE) {
		vm_handle_exception(vm, GP_VECTOR, guest_fep_gp_handler);

		/* Process emulated rdmsr and wrmsr instructions. */
		run_guest_then_process_rdmsr(vm, MSR_IA32_XSS);
		run_guest_then_process_wrmsr(vm, MSR_IA32_XSS);
		run_guest_then_process_wrmsr(vm, MSR_IA32_XSS);

		run_guest_then_process_rdmsr(vm, MSR_IA32_FLUSH_CMD);
		run_guest_then_process_wrmsr(vm, MSR_IA32_FLUSH_CMD);
		run_guest_then_process_wrmsr(vm, MSR_IA32_FLUSH_CMD);

		run_guest_then_process_wrmsr(vm, MSR_NON_EXISTENT);
		run_guest_then_process_rdmsr(vm, MSR_NON_EXISTENT);

		/* Confirm the guest completed without issues. */
		run_guest_then_process_ucall_done(vm);
	} else {
		printf("To run the instruction emulated tests set the module parameter 'kvm.force_emulation_prefix=1'\n");
	}

	kvm_vm_free(vm);
}

static int handle_ucall(struct kvm_vm *vm)
{
	struct ucall uc;

	switch (get_ucall(vm, VCPU_ID, &uc)) {
	case UCALL_ABORT:
		TEST_FAIL("Guest assertion not met");
		break;
	case UCALL_SYNC:
		vm_ioctl(vm, KVM_X86_SET_MSR_FILTER, &no_filter_deny);
		break;
	case UCALL_DONE:
		return 1;
	default:
		TEST_FAIL("Unknown ucall %lu", uc.cmd);
	}

	return 0;
}

static void handle_rdmsr(struct kvm_run *run)
{
	run->msr.data = run->msr.index;
	msr_reads++;

	if (run->msr.index == MSR_SYSCALL_MASK ||
	    run->msr.index == MSR_GS_BASE) {
		TEST_ASSERT(run->msr.reason == KVM_MSR_EXIT_REASON_FILTER,
			    "MSR read trap w/o access fault");
	}

	if (run->msr.index == 0xdeadbeef) {
		TEST_ASSERT(run->msr.reason == KVM_MSR_EXIT_REASON_UNKNOWN,
			    "MSR deadbeef read trap w/o inval fault");
	}
}

static void handle_wrmsr(struct kvm_run *run)
{
	/* ignore */
	msr_writes++;

	if (run->msr.index == MSR_IA32_POWER_CTL) {
		TEST_ASSERT(run->msr.data == 0x1234,
			    "MSR data for MSR_IA32_POWER_CTL incorrect");
		TEST_ASSERT(run->msr.reason == KVM_MSR_EXIT_REASON_FILTER,
			    "MSR_IA32_POWER_CTL trap w/o access fault");
	}

	if (run->msr.index == 0xdeadbeef) {
		TEST_ASSERT(run->msr.data == 0x1234,
			    "MSR data for deadbeef incorrect");
		TEST_ASSERT(run->msr.reason == KVM_MSR_EXIT_REASON_UNKNOWN,
			    "deadbeef trap w/o inval fault");
	}
}

static void test_msr_filter_deny(void) {
	struct kvm_enable_cap cap = {
		.cap = KVM_CAP_X86_USER_SPACE_MSR,
		.args[0] = KVM_MSR_EXIT_REASON_INVAL |
			   KVM_MSR_EXIT_REASON_UNKNOWN |
			   KVM_MSR_EXIT_REASON_FILTER,
	};
	struct kvm_vm *vm;
	struct kvm_run *run;
	int rc;

	/* Create VM */
	vm = vm_create_default(VCPU_ID, 0, guest_code_filter_deny);
	vcpu_set_cpuid(vm, VCPU_ID, kvm_get_supported_cpuid());
	run = vcpu_state(vm, VCPU_ID);

	rc = kvm_check_cap(KVM_CAP_X86_USER_SPACE_MSR);
	TEST_ASSERT(rc, "KVM_CAP_X86_USER_SPACE_MSR is available");
	vm_enable_cap(vm, &cap);

	rc = kvm_check_cap(KVM_CAP_X86_MSR_FILTER);
	TEST_ASSERT(rc, "KVM_CAP_X86_MSR_FILTER is available");

	prepare_bitmaps();
	vm_ioctl(vm, KVM_X86_SET_MSR_FILTER, &filter_deny);

	while (1) {
		rc = _vcpu_run(vm, VCPU_ID);

		TEST_ASSERT(rc == 0, "vcpu_run failed: %d\n", rc);

		switch (run->exit_reason) {
		case KVM_EXIT_X86_RDMSR:
			handle_rdmsr(run);
			break;
		case KVM_EXIT_X86_WRMSR:
			handle_wrmsr(run);
			break;
		case KVM_EXIT_IO:
			if (handle_ucall(vm))
				goto done;
			break;
		}

	}

done:
	TEST_ASSERT(msr_reads == 4, "Handled 4 rdmsr in user space");
	TEST_ASSERT(msr_writes == 3, "Handled 3 wrmsr in user space");

	kvm_vm_free(vm);
}

static void test_msr_permission_bitmap(void) {
	struct kvm_enable_cap cap = {
		.cap = KVM_CAP_X86_USER_SPACE_MSR,
		.args[0] = KVM_MSR_EXIT_REASON_FILTER,
	};
	struct kvm_vm *vm;
	int rc;

	/* Create VM */
	vm = vm_create_default(VCPU_ID, 0, guest_code_permission_bitmap);
	vcpu_set_cpuid(vm, VCPU_ID, kvm_get_supported_cpuid());

	rc = kvm_check_cap(KVM_CAP_X86_USER_SPACE_MSR);
	TEST_ASSERT(rc, "KVM_CAP_X86_USER_SPACE_MSR is available");
	vm_enable_cap(vm, &cap);

	rc = kvm_check_cap(KVM_CAP_X86_MSR_FILTER);
	TEST_ASSERT(rc, "KVM_CAP_X86_MSR_FILTER is available");

	vm_ioctl(vm, KVM_X86_SET_MSR_FILTER, &filter_fs);
	run_guest_then_process_rdmsr(vm, MSR_FS_BASE);
	TEST_ASSERT(run_guest_then_process_ucall(vm) == UCALL_SYNC, "Expected ucall state to be UCALL_SYNC.");
	vm_ioctl(vm, KVM_X86_SET_MSR_FILTER, &filter_gs);
	run_guest_then_process_rdmsr(vm, MSR_GS_BASE);
	run_guest_then_process_ucall_done(vm);

	kvm_vm_free(vm);
}

int main(int argc, char *argv[])
{
	/* Tell stdout not to buffer its content */
	setbuf(stdout, NULL);

	test_msr_filter_allow();

	test_msr_filter_deny();

	test_msr_permission_bitmap();

	return 0;
}
