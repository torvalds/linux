// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021, Google LLC.
 *
 * Tests for adjusting the KVM clock from userspace
 */
#include <asm/kvm_para.h>
#include <asm/pvclock.h>
#include <asm/pvclock-abi.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"

struct test_case {
	uint64_t kvmclock_base;
	int64_t realtime_offset;
};

static struct test_case test_cases[] = {
	{ .kvmclock_base = 0 },
	{ .kvmclock_base = 180 * NSEC_PER_SEC },
	{ .kvmclock_base = 0, .realtime_offset = -180 * NSEC_PER_SEC },
	{ .kvmclock_base = 0, .realtime_offset = 180 * NSEC_PER_SEC },
};

#define GUEST_SYNC_CLOCK(__stage, __val)			\
		GUEST_SYNC_ARGS(__stage, __val, 0, 0, 0)

static void guest_main(vm_paddr_t pvti_pa, struct pvclock_vcpu_time_info *pvti)
{
	int i;

	wrmsr(MSR_KVM_SYSTEM_TIME_NEW, pvti_pa | KVM_MSR_ENABLED);
	for (i = 0; i < ARRAY_SIZE(test_cases); i++)
		GUEST_SYNC_CLOCK(i, __pvclock_read_cycles(pvti, rdtsc()));
}

#define EXPECTED_FLAGS (KVM_CLOCK_REALTIME | KVM_CLOCK_HOST_TSC)

static inline void assert_flags(struct kvm_clock_data *data)
{
	TEST_ASSERT((data->flags & EXPECTED_FLAGS) == EXPECTED_FLAGS,
		    "unexpected clock data flags: %x (want set: %x)",
		    data->flags, EXPECTED_FLAGS);
}

static void handle_sync(struct ucall *uc, struct kvm_clock_data *start,
			struct kvm_clock_data *end)
{
	uint64_t obs, exp_lo, exp_hi;

	obs = uc->args[2];
	exp_lo = start->clock;
	exp_hi = end->clock;

	assert_flags(start);
	assert_flags(end);

	TEST_ASSERT(exp_lo <= obs && obs <= exp_hi,
		    "unexpected kvm-clock value: %"PRIu64" expected range: [%"PRIu64", %"PRIu64"]",
		    obs, exp_lo, exp_hi);

	pr_info("kvm-clock value: %"PRIu64" expected range [%"PRIu64", %"PRIu64"]\n",
		obs, exp_lo, exp_hi);
}

static void handle_abort(struct ucall *uc)
{
	REPORT_GUEST_ASSERT(*uc);
}

static void setup_clock(struct kvm_vm *vm, struct test_case *test_case)
{
	struct kvm_clock_data data;

	memset(&data, 0, sizeof(data));

	data.clock = test_case->kvmclock_base;
	if (test_case->realtime_offset) {
		struct timespec ts;
		int r;

		data.flags |= KVM_CLOCK_REALTIME;
		do {
			r = clock_gettime(CLOCK_REALTIME, &ts);
			if (!r)
				break;
		} while (errno == EINTR);

		TEST_ASSERT(!r, "clock_gettime() failed: %d\n", r);

		data.realtime = ts.tv_sec * NSEC_PER_SEC;
		data.realtime += ts.tv_nsec;
		data.realtime += test_case->realtime_offset;
	}

	vm_ioctl(vm, KVM_SET_CLOCK, &data);
}

static void enter_guest(struct kvm_vcpu *vcpu)
{
	struct kvm_clock_data start, end;
	struct kvm_vm *vm = vcpu->vm;
	struct ucall uc;
	int i;

	for (i = 0; i < ARRAY_SIZE(test_cases); i++) {
		setup_clock(vm, &test_cases[i]);

		vm_ioctl(vm, KVM_GET_CLOCK, &start);

		vcpu_run(vcpu);
		vm_ioctl(vm, KVM_GET_CLOCK, &end);

		TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_SYNC:
			handle_sync(&uc, &start, &end);
			break;
		case UCALL_ABORT:
			handle_abort(&uc);
			return;
		default:
			TEST_ASSERT(0, "unhandled ucall: %ld\n", uc.cmd);
		}
	}
}

#define CLOCKSOURCE_PATH "/sys/devices/system/clocksource/clocksource0/current_clocksource"

static void check_clocksource(void)
{
	char *clk_name;
	struct stat st;
	FILE *fp;

	fp = fopen(CLOCKSOURCE_PATH, "r");
	if (!fp) {
		pr_info("failed to open clocksource file: %d; assuming TSC.\n",
			errno);
		return;
	}

	if (fstat(fileno(fp), &st)) {
		pr_info("failed to stat clocksource file: %d; assuming TSC.\n",
			errno);
		goto out;
	}

	clk_name = malloc(st.st_size);
	TEST_ASSERT(clk_name, "failed to allocate buffer to read file\n");

	if (!fgets(clk_name, st.st_size, fp)) {
		pr_info("failed to read clocksource file: %d; assuming TSC.\n",
			ferror(fp));
		goto out;
	}

	TEST_ASSERT(!strncmp(clk_name, "tsc\n", st.st_size),
		    "clocksource not supported: %s", clk_name);
out:
	fclose(fp);
}

int main(void)
{
	struct kvm_vcpu *vcpu;
	vm_vaddr_t pvti_gva;
	vm_paddr_t pvti_gpa;
	struct kvm_vm *vm;
	int flags;

	flags = kvm_check_cap(KVM_CAP_ADJUST_CLOCK);
	TEST_REQUIRE(flags & KVM_CLOCK_REALTIME);

	check_clocksource();

	vm = vm_create_with_one_vcpu(&vcpu, guest_main);

	pvti_gva = vm_vaddr_alloc(vm, getpagesize(), 0x10000);
	pvti_gpa = addr_gva2gpa(vm, pvti_gva);
	vcpu_args_set(vcpu, 2, pvti_gpa, pvti_gva);

	enter_guest(vcpu);
	kvm_vm_free(vm);
}
