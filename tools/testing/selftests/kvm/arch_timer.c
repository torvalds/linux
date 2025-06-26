// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch_timer.c - Tests the arch timer IRQ functionality
 *
 * The guest's main thread configures the timer interrupt and waits
 * for it to fire, with a timeout equal to the timer period.
 * It asserts that the timeout doesn't exceed the timer period plus
 * a user configurable error margin(default to 100us)
 *
 * On the other hand, upon receipt of an interrupt, the guest's interrupt
 * handler validates the interrupt by checking if the architectural state
 * is in compliance with the specifications.
 *
 * The test provides command-line options to configure the timer's
 * period (-p), number of vCPUs (-n), iterations per stage (-i) and timer
 * interrupt arrival error margin (-e). To stress-test the timer stack
 * even more, an option to migrate the vCPUs across pCPUs (-m), at a
 * particular rate, is also provided.
 *
 * Copyright (c) 2021, Google LLC.
 */
#include <stdlib.h>
#include <pthread.h>
#include <linux/sizes.h>
#include <linux/bitmap.h>
#include <sys/sysinfo.h>

#include "timer_test.h"
#include "ucall_common.h"

struct test_args test_args = {
	.nr_vcpus = NR_VCPUS_DEF,
	.nr_iter = NR_TEST_ITERS_DEF,
	.timer_period_ms = TIMER_TEST_PERIOD_MS_DEF,
	.migration_freq_ms = TIMER_TEST_MIGRATION_FREQ_MS,
	.timer_err_margin_us = TIMER_TEST_ERR_MARGIN_US,
	.reserved = 1,
};

struct kvm_vcpu *vcpus[KVM_MAX_VCPUS];
struct test_vcpu_shared_data vcpu_shared_data[KVM_MAX_VCPUS];

static pthread_t pt_vcpu_run[KVM_MAX_VCPUS];
static unsigned long *vcpu_done_map;
static pthread_mutex_t vcpu_done_map_lock;

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

	pr_info("PASS(vCPU-%d).\n", vcpu_idx);

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
	uint32_t new_pcpu = test_get_pcpu();

	pr_debug("Migrating vCPU: %u to pCPU: %u\n", vcpu_idx, new_pcpu);

	ret = __pin_task_to_cpu(pt_vcpu_run[vcpu_idx], new_pcpu);

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

static void test_print_help(char *name)
{
	pr_info("Usage: %s [-h] [-n nr_vcpus] [-i iterations] [-p timer_period_ms]\n"
		"\t\t    [-m migration_freq_ms] [-o counter_offset]\n"
		"\t\t    [-e timer_err_margin_us]\n", name);
	pr_info("\t-n: Number of vCPUs to configure (default: %u; max: %u)\n",
		NR_VCPUS_DEF, KVM_MAX_VCPUS);
	pr_info("\t-i: Number of iterations per stage (default: %u)\n",
		NR_TEST_ITERS_DEF);
	pr_info("\t-p: Periodicity (in ms) of the guest timer (default: %u)\n",
		TIMER_TEST_PERIOD_MS_DEF);
	pr_info("\t-m: Frequency (in ms) of vCPUs to migrate to different pCPU. 0 to turn off (default: %u)\n",
		TIMER_TEST_MIGRATION_FREQ_MS);
	pr_info("\t-o: Counter offset (in counter cycles, default: 0) [aarch64-only]\n");
	pr_info("\t-e: Interrupt arrival error margin (in us) of the guest timer (default: %u)\n",
		TIMER_TEST_ERR_MARGIN_US);
	pr_info("\t-h: print this help screen\n");
}

static bool parse_args(int argc, char *argv[])
{
	int opt;

	while ((opt = getopt(argc, argv, "hn:i:p:m:o:e:")) != -1) {
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
		case 'e':
			test_args.timer_err_margin_us = atoi_non_negative("Error Margin", optarg);
			break;
		case 'o':
			test_args.counter_offset = strtol(optarg, NULL, 0);
			test_args.reserved = 0;
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
