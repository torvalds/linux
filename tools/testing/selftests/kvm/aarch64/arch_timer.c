// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch_timer.c - Tests the aarch64 timer IRQ functionality
 *
 * The test validates both the virtual and physical timer IRQs using
 * CVAL and TVAL registers. This consitutes the four stages in the test.
 * The guest's main thread configures the timer interrupt for a stage
 * and waits for it to fire, with a timeout equal to the timer period.
 * It asserts that the timeout doesn't exceed the timer period.
 *
 * On the other hand, upon receipt of an interrupt, the guest's interrupt
 * handler validates the interrupt by checking if the architectural state
 * is in compliance with the specifications.
 *
 * The test provides command-line options to configure the timer's
 * period (-p), number of vCPUs (-n), and iterations per stage (-i).
 * To stress-test the timer stack even more, an option to migrate the
 * vCPUs across pCPUs (-m), at a particular rate, is also provided.
 *
 * Copyright (c) 2021, Google LLC.
 */
#define _GNU_SOURCE

#include <stdlib.h>
#include <pthread.h>
#include <linux/kvm.h>
#include <linux/sizes.h>
#include <linux/bitmap.h>
#include <sys/sysinfo.h>

#include "kvm_util.h"
#include "processor.h"
#include "delay.h"
#include "arch_timer.h"
#include "gic.h"
#include "vgic.h"

#define NR_VCPUS_DEF			4
#define NR_TEST_ITERS_DEF		5
#define TIMER_TEST_PERIOD_MS_DEF	10
#define TIMER_TEST_ERR_MARGIN_US	100
#define TIMER_TEST_MIGRATION_FREQ_MS	2

struct test_args {
	int nr_vcpus;
	int nr_iter;
	int timer_period_ms;
	int migration_freq_ms;
	struct kvm_arm_counter_offset offset;
};

static struct test_args test_args = {
	.nr_vcpus = NR_VCPUS_DEF,
	.nr_iter = NR_TEST_ITERS_DEF,
	.timer_period_ms = TIMER_TEST_PERIOD_MS_DEF,
	.migration_freq_ms = TIMER_TEST_MIGRATION_FREQ_MS,
	.offset = { .reserved = 1 },
};

#define msecs_to_usecs(msec)		((msec) * 1000LL)

#define GICD_BASE_GPA			0x8000000ULL
#define GICR_BASE_GPA			0x80A0000ULL

enum guest_stage {
	GUEST_STAGE_VTIMER_CVAL = 1,
	GUEST_STAGE_VTIMER_TVAL,
	GUEST_STAGE_PTIMER_CVAL,
	GUEST_STAGE_PTIMER_TVAL,
	GUEST_STAGE_MAX,
};

/* Shared variables between host and guest */
struct test_vcpu_shared_data {
	int nr_iter;
	enum guest_stage guest_stage;
	uint64_t xcnt;
};

static struct kvm_vcpu *vcpus[KVM_MAX_VCPUS];
static pthread_t pt_vcpu_run[KVM_MAX_VCPUS];
static struct test_vcpu_shared_data vcpu_shared_data[KVM_MAX_VCPUS];

static int vtimer_irq, ptimer_irq;

static unsigned long *vcpu_done_map;
static pthread_mutex_t vcpu_done_map_lock;

static void
guest_configure_timer_action(struct test_vcpu_shared_data *shared_data)
{
	switch (shared_data->guest_stage) {
	case GUEST_STAGE_VTIMER_CVAL:
		timer_set_next_cval_ms(VIRTUAL, test_args.timer_period_ms);
		shared_data->xcnt = timer_get_cntct(VIRTUAL);
		timer_set_ctl(VIRTUAL, CTL_ENABLE);
		break;
	case GUEST_STAGE_VTIMER_TVAL:
		timer_set_next_tval_ms(VIRTUAL, test_args.timer_period_ms);
		shared_data->xcnt = timer_get_cntct(VIRTUAL);
		timer_set_ctl(VIRTUAL, CTL_ENABLE);
		break;
	case GUEST_STAGE_PTIMER_CVAL:
		timer_set_next_cval_ms(PHYSICAL, test_args.timer_period_ms);
		shared_data->xcnt = timer_get_cntct(PHYSICAL);
		timer_set_ctl(PHYSICAL, CTL_ENABLE);
		break;
	case GUEST_STAGE_PTIMER_TVAL:
		timer_set_next_tval_ms(PHYSICAL, test_args.timer_period_ms);
		shared_data->xcnt = timer_get_cntct(PHYSICAL);
		timer_set_ctl(PHYSICAL, CTL_ENABLE);
		break;
	default:
		GUEST_ASSERT(0);
	}
}

static void guest_validate_irq(unsigned int intid,
				struct test_vcpu_shared_data *shared_data)
{
	enum guest_stage stage = shared_data->guest_stage;
	uint64_t xcnt = 0, xcnt_diff_us, cval = 0;
	unsigned long xctl = 0;
	unsigned int timer_irq = 0;
	unsigned int accessor;

	if (intid == IAR_SPURIOUS)
		return;

	switch (stage) {
	case GUEST_STAGE_VTIMER_CVAL:
	case GUEST_STAGE_VTIMER_TVAL:
		accessor = VIRTUAL;
		timer_irq = vtimer_irq;
		break;
	case GUEST_STAGE_PTIMER_CVAL:
	case GUEST_STAGE_PTIMER_TVAL:
		accessor = PHYSICAL;
		timer_irq = ptimer_irq;
		break;
	default:
		GUEST_ASSERT(0);
		return;
	}

	xctl = timer_get_ctl(accessor);
	if ((xctl & CTL_IMASK) || !(xctl & CTL_ENABLE))
		return;

	timer_set_ctl(accessor, CTL_IMASK);
	xcnt = timer_get_cntct(accessor);
	cval = timer_get_cval(accessor);

	xcnt_diff_us = cycles_to_usec(xcnt - shared_data->xcnt);

	/* Make sure we are dealing with the correct timer IRQ */
	GUEST_ASSERT_EQ(intid, timer_irq);

	/* Basic 'timer condition met' check */
	__GUEST_ASSERT(xcnt >= cval,
		       "xcnt = 0x%llx, cval = 0x%llx, xcnt_diff_us = 0x%llx",
		       xcnt, cval, xcnt_diff_us);
	__GUEST_ASSERT(xctl & CTL_ISTATUS, "xcnt = 0x%llx", xcnt);

	WRITE_ONCE(shared_data->nr_iter, shared_data->nr_iter + 1);
}

static void guest_irq_handler(struct ex_regs *regs)
{
	unsigned int intid = gic_get_and_ack_irq();
	uint32_t cpu = guest_get_vcpuid();
	struct test_vcpu_shared_data *shared_data = &vcpu_shared_data[cpu];

	guest_validate_irq(intid, shared_data);

	gic_set_eoi(intid);
}

static void guest_run_stage(struct test_vcpu_shared_data *shared_data,
				enum guest_stage stage)
{
	uint32_t irq_iter, config_iter;

	shared_data->guest_stage = stage;
	shared_data->nr_iter = 0;

	for (config_iter = 0; config_iter < test_args.nr_iter; config_iter++) {
		/* Setup the next interrupt */
		guest_configure_timer_action(shared_data);

		/* Setup a timeout for the interrupt to arrive */
		udelay(msecs_to_usecs(test_args.timer_period_ms) +
			TIMER_TEST_ERR_MARGIN_US);

		irq_iter = READ_ONCE(shared_data->nr_iter);
		GUEST_ASSERT_EQ(config_iter + 1, irq_iter);
	}
}

static void guest_code(void)
{
	uint32_t cpu = guest_get_vcpuid();
	struct test_vcpu_shared_data *shared_data = &vcpu_shared_data[cpu];

	local_irq_disable();

	gic_init(GIC_V3, test_args.nr_vcpus,
		(void *)GICD_BASE_GPA, (void *)GICR_BASE_GPA);

	timer_set_ctl(VIRTUAL, CTL_IMASK);
	timer_set_ctl(PHYSICAL, CTL_IMASK);

	gic_irq_enable(vtimer_irq);
	gic_irq_enable(ptimer_irq);
	local_irq_enable();

	guest_run_stage(shared_data, GUEST_STAGE_VTIMER_CVAL);
	guest_run_stage(shared_data, GUEST_STAGE_VTIMER_TVAL);
	guest_run_stage(shared_data, GUEST_STAGE_PTIMER_CVAL);
	guest_run_stage(shared_data, GUEST_STAGE_PTIMER_TVAL);

	GUEST_DONE();
}

static void *test_vcpu_run(void *arg)
{
	unsigned int vcpu_idx = (unsigned long)arg;
	struct ucall uc;
	struct kvm_vcpu *vcpu = vcpus[vcpu_idx];
	struct kvm_vm *vm = vcpu->vm;
	struct test_vcpu_shared_data *shared_data = &vcpu_shared_data[vcpu_idx];

	vcpu_run(vcpu);

	/* Currently, any exit from guest is an indication of completion */
	pthread_mutex_lock(&vcpu_done_map_lock);
	__set_bit(vcpu_idx, vcpu_done_map);
	pthread_mutex_unlock(&vcpu_done_map_lock);

	switch (get_ucall(vcpu, &uc)) {
	case UCALL_SYNC:
	case UCALL_DONE:
		break;
	case UCALL_ABORT:
		sync_global_from_guest(vm, *shared_data);
		fprintf(stderr, "Guest assert failed,  vcpu %u; stage; %u; iter: %u\n",
			vcpu_idx, shared_data->guest_stage, shared_data->nr_iter);
		REPORT_GUEST_ASSERT(uc);
		break;
	default:
		TEST_FAIL("Unexpected guest exit");
	}

	return NULL;
}

static uint32_t test_get_pcpu(void)
{
	uint32_t pcpu;
	unsigned int nproc_conf;
	cpu_set_t online_cpuset;

	nproc_conf = get_nprocs_conf();
	sched_getaffinity(0, sizeof(cpu_set_t), &online_cpuset);

	/* Randomly find an available pCPU to place a vCPU on */
	do {
		pcpu = rand() % nproc_conf;
	} while (!CPU_ISSET(pcpu, &online_cpuset));

	return pcpu;
}

static int test_migrate_vcpu(unsigned int vcpu_idx)
{
	int ret;
	cpu_set_t cpuset;
	uint32_t new_pcpu = test_get_pcpu();

	CPU_ZERO(&cpuset);
	CPU_SET(new_pcpu, &cpuset);

	pr_debug("Migrating vCPU: %u to pCPU: %u\n", vcpu_idx, new_pcpu);

	ret = pthread_setaffinity_np(pt_vcpu_run[vcpu_idx],
				     sizeof(cpuset), &cpuset);

	/* Allow the error where the vCPU thread is already finished */
	TEST_ASSERT(ret == 0 || ret == ESRCH,
		    "Failed to migrate the vCPU:%u to pCPU: %u; ret: %d",
		    vcpu_idx, new_pcpu, ret);

	return ret;
}

static void *test_vcpu_migration(void *arg)
{
	unsigned int i, n_done;
	bool vcpu_done;

	do {
		usleep(msecs_to_usecs(test_args.migration_freq_ms));

		for (n_done = 0, i = 0; i < test_args.nr_vcpus; i++) {
			pthread_mutex_lock(&vcpu_done_map_lock);
			vcpu_done = test_bit(i, vcpu_done_map);
			pthread_mutex_unlock(&vcpu_done_map_lock);

			if (vcpu_done) {
				n_done++;
				continue;
			}

			test_migrate_vcpu(i);
		}
	} while (test_args.nr_vcpus != n_done);

	return NULL;
}

static void test_run(struct kvm_vm *vm)
{
	pthread_t pt_vcpu_migration;
	unsigned int i;
	int ret;

	pthread_mutex_init(&vcpu_done_map_lock, NULL);
	vcpu_done_map = bitmap_zalloc(test_args.nr_vcpus);
	TEST_ASSERT(vcpu_done_map, "Failed to allocate vcpu done bitmap");

	for (i = 0; i < (unsigned long)test_args.nr_vcpus; i++) {
		ret = pthread_create(&pt_vcpu_run[i], NULL, test_vcpu_run,
				     (void *)(unsigned long)i);
		TEST_ASSERT(!ret, "Failed to create vCPU-%d pthread", i);
	}

	/* Spawn a thread to control the vCPU migrations */
	if (test_args.migration_freq_ms) {
		srand(time(NULL));

		ret = pthread_create(&pt_vcpu_migration, NULL,
					test_vcpu_migration, NULL);
		TEST_ASSERT(!ret, "Failed to create the migration pthread");
	}


	for (i = 0; i < test_args.nr_vcpus; i++)
		pthread_join(pt_vcpu_run[i], NULL);

	if (test_args.migration_freq_ms)
		pthread_join(pt_vcpu_migration, NULL);

	bitmap_free(vcpu_done_map);
}

static void test_init_timer_irq(struct kvm_vm *vm)
{
	/* Timer initid should be same for all the vCPUs, so query only vCPU-0 */
	vcpu_device_attr_get(vcpus[0], KVM_ARM_VCPU_TIMER_CTRL,
			     KVM_ARM_VCPU_TIMER_IRQ_PTIMER, &ptimer_irq);
	vcpu_device_attr_get(vcpus[0], KVM_ARM_VCPU_TIMER_CTRL,
			     KVM_ARM_VCPU_TIMER_IRQ_VTIMER, &vtimer_irq);

	sync_global_to_guest(vm, ptimer_irq);
	sync_global_to_guest(vm, vtimer_irq);

	pr_debug("ptimer_irq: %d; vtimer_irq: %d\n", ptimer_irq, vtimer_irq);
}

static int gic_fd;

static struct kvm_vm *test_vm_create(void)
{
	struct kvm_vm *vm;
	unsigned int i;
	int nr_vcpus = test_args.nr_vcpus;

	vm = vm_create_with_vcpus(nr_vcpus, guest_code, vcpus);

	vm_init_descriptor_tables(vm);
	vm_install_exception_handler(vm, VECTOR_IRQ_CURRENT, guest_irq_handler);

	if (!test_args.offset.reserved) {
		if (kvm_has_cap(KVM_CAP_COUNTER_OFFSET))
			vm_ioctl(vm, KVM_ARM_SET_COUNTER_OFFSET, &test_args.offset);
		else
			TEST_FAIL("no support for global offset");
	}

	for (i = 0; i < nr_vcpus; i++)
		vcpu_init_descriptor_tables(vcpus[i]);

	test_init_timer_irq(vm);
	gic_fd = vgic_v3_setup(vm, nr_vcpus, 64, GICD_BASE_GPA, GICR_BASE_GPA);
	__TEST_REQUIRE(gic_fd >= 0, "Failed to create vgic-v3");

	/* Make all the test's cmdline args visible to the guest */
	sync_global_to_guest(vm, test_args);

	return vm;
}

static void test_vm_cleanup(struct kvm_vm *vm)
{
	close(gic_fd);
	kvm_vm_free(vm);
}

static void test_print_help(char *name)
{
	pr_info("Usage: %s [-h] [-n nr_vcpus] [-i iterations] [-p timer_period_ms]\n",
		name);
	pr_info("\t-n: Number of vCPUs to configure (default: %u; max: %u)\n",
		NR_VCPUS_DEF, KVM_MAX_VCPUS);
	pr_info("\t-i: Number of iterations per stage (default: %u)\n",
		NR_TEST_ITERS_DEF);
	pr_info("\t-p: Periodicity (in ms) of the guest timer (default: %u)\n",
		TIMER_TEST_PERIOD_MS_DEF);
	pr_info("\t-m: Frequency (in ms) of vCPUs to migrate to different pCPU. 0 to turn off (default: %u)\n",
		TIMER_TEST_MIGRATION_FREQ_MS);
	pr_info("\t-o: Counter offset (in counter cycles, default: 0)\n");
	pr_info("\t-h: print this help screen\n");
}

static bool parse_args(int argc, char *argv[])
{
	int opt;

	while ((opt = getopt(argc, argv, "hn:i:p:m:o:")) != -1) {
		switch (opt) {
		case 'n':
			test_args.nr_vcpus = atoi_positive("Number of vCPUs", optarg);
			if (test_args.nr_vcpus > KVM_MAX_VCPUS) {
				pr_info("Max allowed vCPUs: %u\n",
					KVM_MAX_VCPUS);
				goto err;
			}
			break;
		case 'i':
			test_args.nr_iter = atoi_positive("Number of iterations", optarg);
			break;
		case 'p':
			test_args.timer_period_ms = atoi_positive("Periodicity", optarg);
			break;
		case 'm':
			test_args.migration_freq_ms = atoi_non_negative("Frequency", optarg);
			break;
		case 'o':
			test_args.offset.counter_offset = strtol(optarg, NULL, 0);
			test_args.offset.reserved = 0;
			break;
		case 'h':
		default:
			goto err;
		}
	}

	return true;

err:
	test_print_help(argv[0]);
	return false;
}

int main(int argc, char *argv[])
{
	struct kvm_vm *vm;

	if (!parse_args(argc, argv))
		exit(KSFT_SKIP);

	__TEST_REQUIRE(!test_args.migration_freq_ms || get_nprocs() >= 2,
		       "At least two physical CPUs needed for vCPU migration");

	vm = test_vm_create();
	test_run(vm);
	test_vm_cleanup(vm);

	return 0;
}
