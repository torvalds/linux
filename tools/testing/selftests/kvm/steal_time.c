// SPDX-License-Identifier: GPL-2.0
/*
 * steal/stolen time test
 *
 * Copyright (C) 2020, Red Hat, Inc.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <time.h>
#include <sched.h>
#include <pthread.h>
#include <linux/kernel.h>
#include <asm/kvm.h>
#include <asm/kvm_para.h>

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"

#define NR_VCPUS		4
#define ST_GPA_BASE		(1 << 30)

static void *st_gva[NR_VCPUS];
static uint64_t guest_stolen_time[NR_VCPUS];

#if defined(__x86_64__)

/* steal_time must have 64-byte alignment */
#define STEAL_TIME_SIZE		((sizeof(struct kvm_steal_time) + 63) & ~63)

static void check_status(struct kvm_steal_time *st)
{
	GUEST_ASSERT(!(READ_ONCE(st->version) & 1));
	GUEST_ASSERT(READ_ONCE(st->flags) == 0);
	GUEST_ASSERT(READ_ONCE(st->preempted) == 0);
}

static void guest_code(int cpu)
{
	struct kvm_steal_time *st = st_gva[cpu];
	uint32_t version;

	GUEST_ASSERT(rdmsr(MSR_KVM_STEAL_TIME) == ((uint64_t)st_gva[cpu] | KVM_MSR_ENABLED));

	memset(st, 0, sizeof(*st));
	GUEST_SYNC(0);

	check_status(st);
	WRITE_ONCE(guest_stolen_time[cpu], st->steal);
	version = READ_ONCE(st->version);
	check_status(st);
	GUEST_SYNC(1);

	check_status(st);
	GUEST_ASSERT(version < READ_ONCE(st->version));
	WRITE_ONCE(guest_stolen_time[cpu], st->steal);
	check_status(st);
	GUEST_DONE();
}

static void steal_time_init(struct kvm_vm *vm)
{
	int i;

	if (!(kvm_get_supported_cpuid_entry(KVM_CPUID_FEATURES)->eax &
	      KVM_FEATURE_STEAL_TIME)) {
		print_skip("steal-time not supported");
		exit(KSFT_SKIP);
	}

	for (i = 0; i < NR_VCPUS; ++i) {
		int ret;

		/* ST_GPA_BASE is identity mapped */
		st_gva[i] = (void *)(ST_GPA_BASE + i * STEAL_TIME_SIZE);
		sync_global_to_guest(vm, st_gva[i]);

		ret = _vcpu_set_msr(vm, i, MSR_KVM_STEAL_TIME, (ulong)st_gva[i] | KVM_STEAL_RESERVED_MASK);
		TEST_ASSERT(ret == 0, "Bad GPA didn't fail");

		vcpu_set_msr(vm, i, MSR_KVM_STEAL_TIME, (ulong)st_gva[i] | KVM_MSR_ENABLED);
	}
}

static void steal_time_dump(struct kvm_vm *vm, uint32_t vcpuid)
{
	struct kvm_steal_time *st = addr_gva2hva(vm, (ulong)st_gva[vcpuid]);
	int i;

	pr_info("VCPU%d:\n", vcpuid);
	pr_info("    steal:     %lld\n", st->steal);
	pr_info("    version:   %d\n", st->version);
	pr_info("    flags:     %d\n", st->flags);
	pr_info("    preempted: %d\n", st->preempted);
	pr_info("    u8_pad:    ");
	for (i = 0; i < 3; ++i)
		pr_info("%d", st->u8_pad[i]);
	pr_info("\n    pad:       ");
	for (i = 0; i < 11; ++i)
		pr_info("%d", st->pad[i]);
	pr_info("\n");
}

#elif defined(__aarch64__)

/* PV_TIME_ST must have 64-byte alignment */
#define STEAL_TIME_SIZE		((sizeof(struct st_time) + 63) & ~63)

#define SMCCC_ARCH_FEATURES	0x80000001
#define PV_TIME_FEATURES	0xc5000020
#define PV_TIME_ST		0xc5000021

struct st_time {
	uint32_t rev;
	uint32_t attr;
	uint64_t st_time;
};

static int64_t smccc(uint32_t func, uint64_t arg)
{
	struct arm_smccc_res res;

	smccc_hvc(func, arg, 0, 0, 0, 0, 0, 0, &res);
	return res.a0;
}

static void check_status(struct st_time *st)
{
	GUEST_ASSERT(READ_ONCE(st->rev) == 0);
	GUEST_ASSERT(READ_ONCE(st->attr) == 0);
}

static void guest_code(int cpu)
{
	struct st_time *st;
	int64_t status;

	status = smccc(SMCCC_ARCH_FEATURES, PV_TIME_FEATURES);
	GUEST_ASSERT(status == 0);
	status = smccc(PV_TIME_FEATURES, PV_TIME_FEATURES);
	GUEST_ASSERT(status == 0);
	status = smccc(PV_TIME_FEATURES, PV_TIME_ST);
	GUEST_ASSERT(status == 0);

	status = smccc(PV_TIME_ST, 0);
	GUEST_ASSERT(status != -1);
	GUEST_ASSERT(status == (ulong)st_gva[cpu]);

	st = (struct st_time *)status;
	GUEST_SYNC(0);

	check_status(st);
	WRITE_ONCE(guest_stolen_time[cpu], st->st_time);
	GUEST_SYNC(1);

	check_status(st);
	WRITE_ONCE(guest_stolen_time[cpu], st->st_time);
	GUEST_DONE();
}

static void steal_time_init(struct kvm_vm *vm)
{
	struct kvm_device_attr dev = {
		.group = KVM_ARM_VCPU_PVTIME_CTRL,
		.attr = KVM_ARM_VCPU_PVTIME_IPA,
	};
	int i, ret;

	ret = _vcpu_ioctl(vm, 0, KVM_HAS_DEVICE_ATTR, &dev);
	if (ret != 0 && errno == ENXIO) {
		print_skip("steal-time not supported");
		exit(KSFT_SKIP);
	}

	for (i = 0; i < NR_VCPUS; ++i) {
		uint64_t st_ipa;

		vcpu_ioctl(vm, i, KVM_HAS_DEVICE_ATTR, &dev);

		dev.addr = (uint64_t)&st_ipa;

		/* ST_GPA_BASE is identity mapped */
		st_gva[i] = (void *)(ST_GPA_BASE + i * STEAL_TIME_SIZE);
		sync_global_to_guest(vm, st_gva[i]);

		st_ipa = (ulong)st_gva[i] | 1;
		ret = _vcpu_ioctl(vm, i, KVM_SET_DEVICE_ATTR, &dev);
		TEST_ASSERT(ret == -1 && errno == EINVAL, "Bad IPA didn't report EINVAL");

		st_ipa = (ulong)st_gva[i];
		vcpu_ioctl(vm, i, KVM_SET_DEVICE_ATTR, &dev);

		ret = _vcpu_ioctl(vm, i, KVM_SET_DEVICE_ATTR, &dev);
		TEST_ASSERT(ret == -1 && errno == EEXIST, "Set IPA twice without EEXIST");

	}
}

static void steal_time_dump(struct kvm_vm *vm, uint32_t vcpuid)
{
	struct st_time *st = addr_gva2hva(vm, (ulong)st_gva[vcpuid]);

	pr_info("VCPU%d:\n", vcpuid);
	pr_info("    rev:     %d\n", st->rev);
	pr_info("    attr:    %d\n", st->attr);
	pr_info("    st_time: %ld\n", st->st_time);
}

#endif

static void *do_steal_time(void *arg)
{
	struct timespec ts, stop;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	stop = timespec_add_ns(ts, MIN_RUN_DELAY_NS);

	while (1) {
		clock_gettime(CLOCK_MONOTONIC, &ts);
		if (timespec_to_ns(timespec_sub(ts, stop)) >= 0)
			break;
	}

	return NULL;
}

static void run_vcpu(struct kvm_vm *vm, uint32_t vcpuid)
{
	struct ucall uc;

	vcpu_args_set(vm, vcpuid, 1, vcpuid);

	vcpu_ioctl(vm, vcpuid, KVM_RUN, NULL);

	switch (get_ucall(vm, vcpuid, &uc)) {
	case UCALL_SYNC:
	case UCALL_DONE:
		break;
	case UCALL_ABORT:
		TEST_ASSERT(false, "%s at %s:%ld", (const char *)uc.args[0],
			    __FILE__, uc.args[1]);
	default:
		TEST_ASSERT(false, "Unexpected exit: %s",
			    exit_reason_str(vcpu_state(vm, vcpuid)->exit_reason));
	}
}

int main(int ac, char **av)
{
	struct kvm_vm *vm;
	pthread_attr_t attr;
	pthread_t thread;
	cpu_set_t cpuset;
	unsigned int gpages;
	long stolen_time;
	long run_delay;
	bool verbose;
	int i;

	verbose = ac > 1 && (!strncmp(av[1], "-v", 3) || !strncmp(av[1], "--verbose", 10));

	/* Set CPU affinity so we can force preemption of the VCPU */
	CPU_ZERO(&cpuset);
	CPU_SET(0, &cpuset);
	pthread_attr_init(&attr);
	pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpuset);
	pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

	/* Create a one VCPU guest and an identity mapped memslot for the steal time structure */
	vm = vm_create_default(0, 0, guest_code);
	gpages = vm_calc_num_guest_pages(VM_MODE_DEFAULT, STEAL_TIME_SIZE * NR_VCPUS);
	vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS, ST_GPA_BASE, 1, gpages, 0);
	virt_map(vm, ST_GPA_BASE, ST_GPA_BASE, gpages);
	ucall_init(vm, NULL);

	/* Add the rest of the VCPUs */
	for (i = 1; i < NR_VCPUS; ++i)
		vm_vcpu_add_default(vm, i, guest_code);

	steal_time_init(vm);

	/* Run test on each VCPU */
	for (i = 0; i < NR_VCPUS; ++i) {
		/* First VCPU run initializes steal-time */
		run_vcpu(vm, i);

		/* Second VCPU run, expect guest stolen time to be <= run_delay */
		run_vcpu(vm, i);
		sync_global_from_guest(vm, guest_stolen_time[i]);
		stolen_time = guest_stolen_time[i];
		run_delay = get_run_delay();
		TEST_ASSERT(stolen_time <= run_delay,
			    "Expected stolen time <= %ld, got %ld",
			    run_delay, stolen_time);

		/* Steal time from the VCPU. The steal time thread has the same CPU affinity as the VCPUs. */
		run_delay = get_run_delay();
		pthread_create(&thread, &attr, do_steal_time, NULL);
		do
			sched_yield();
		while (get_run_delay() - run_delay < MIN_RUN_DELAY_NS);
		pthread_join(thread, NULL);
		run_delay = get_run_delay() - run_delay;
		TEST_ASSERT(run_delay >= MIN_RUN_DELAY_NS,
			    "Expected run_delay >= %ld, got %ld",
			    MIN_RUN_DELAY_NS, run_delay);

		/* Run VCPU again to confirm stolen time is consistent with run_delay */
		run_vcpu(vm, i);
		sync_global_from_guest(vm, guest_stolen_time[i]);
		stolen_time = guest_stolen_time[i] - stolen_time;
		TEST_ASSERT(stolen_time >= run_delay,
			    "Expected stolen time >= %ld, got %ld",
			    run_delay, stolen_time);

		if (verbose) {
			pr_info("VCPU%d: total-stolen-time=%ld test-stolen-time=%ld", i,
				guest_stolen_time[i], stolen_time);
			if (stolen_time == run_delay)
				pr_info(" (BONUS: guest test-stolen-time even exactly matches test-run_delay)");
			pr_info("\n");
			steal_time_dump(vm, i);
		}
	}

	return 0;
}
